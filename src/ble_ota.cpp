#include "ble_ota.hpp"

#ifndef NATIVE_TEST_BUILD

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_crc.h"
#include "esp_bt.h"
#include "nvs_flash.h"

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED && defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace
{
static const char* TAG = "ble_ota";

enum OtaCommand : uint8_t
{
    CMD_START = 0x01,  // [cmd][image_size_u32_le][crc32_u32_le]
    CMD_FINISH = 0x02, // [cmd]
    CMD_ABORT = 0x03,  // [cmd]
    CMD_REBOOT = 0x04  // [cmd]
};

enum OtaState : uint8_t
{
    STATE_IDLE = 0,
    STATE_IN_PROGRESS = 1,
    STATE_READY_TO_REBOOT = 2,
    STATE_ERROR = 3
};

struct __attribute__((packed)) OtaStatus
{
    uint8_t state;
    uint8_t error_code;
    uint16_t reserved;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t running_crc32;
};

static std::mutex s_mtx;
static OtaStatus s_status = {STATE_IDLE, 0, 0, 0, 0, 0};
static bool s_initialized = false;
static bool s_image_ready = false;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t* s_update_partition = nullptr;
static uint32_t s_expected_crc32 = 0;
static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_val_handle = 0;

static const ble_uuid128_t kServiceUuid =
    BLE_UUID128_INIT(0x39, 0x5d, 0xb0, 0x0a, 0x31, 0x14, 0x4f, 0x8b, 0xae, 0x2d, 0x4b, 0x7d, 0x30, 0xd6, 0xe4, 0x2a);
static const ble_uuid128_t kControlUuid =
    BLE_UUID128_INIT(0x39, 0x5d, 0xb0, 0x0b, 0x31, 0x14, 0x4f, 0x8b, 0xae, 0x2d, 0x4b, 0x7d, 0x30, 0xd6, 0xe4, 0x2a);
static const ble_uuid128_t kDataUuid =
    BLE_UUID128_INIT(0x39, 0x5d, 0xb0, 0x0c, 0x31, 0x14, 0x4f, 0x8b, 0xae, 0x2d, 0x4b, 0x7d, 0x30, 0xd6, 0xe4, 0x2a);
static const ble_uuid128_t kStatusUuid =
    BLE_UUID128_INIT(0x39, 0x5d, 0xb0, 0x0d, 0x31, 0x14, 0x4f, 0x8b, 0xae, 0x2d, 0x4b, 0x7d, 0x30, 0xd6, 0xe4, 0x2a);

static uint32_t read_le_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}

static uint8_t err_to_status(esp_err_t err)
{
    return (err == ESP_OK) ? 0 : 1;
}

static void update_status_locked(OtaState state, esp_err_t err)
{
    s_status.state = state;
    s_status.error_code = err_to_status(err);
}

static void notify_status_if_connected()
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_status_val_handle != 0)
    {
        ble_gatts_chr_updated(s_status_val_handle);
    }
}

static void abort_session_locked()
{
    if (s_status.state == STATE_IN_PROGRESS && s_ota_handle != 0)
    {
        esp_ota_abort(s_ota_handle);
    }
    s_ota_handle = 0;
    s_update_partition = nullptr;
    s_status.expected_size = 0;
    s_status.received_size = 0;
    s_status.running_crc32 = 0;
    s_expected_crc32 = 0;
    s_image_ready = false;
}

static esp_err_t start_session_locked(uint32_t image_size, uint32_t expected_crc32)
{
    abort_session_locked();

    s_update_partition = esp_ota_get_next_update_partition(nullptr);
    if (s_update_partition == nullptr)
    {
        ESP_LOGE(TAG, "No OTA app partition found. Add ota_0/ota_1 to partitions.csv.");
        update_status_locked(STATE_ERROR, ESP_ERR_NOT_FOUND);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_begin(s_update_partition, image_size, &s_ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        update_status_locked(STATE_ERROR, err);
        return err;
    }

    s_status.expected_size = image_size;
    s_status.received_size = 0;
    s_status.running_crc32 = UINT32_MAX;
    s_status.reserved = 0;
    s_expected_crc32 = expected_crc32;
    s_image_ready = false;
    update_status_locked(STATE_IN_PROGRESS, ESP_OK);
    ESP_LOGI(TAG, "BLE OTA started, expected_size=%lu, expected_crc32=0x%08lx",
             static_cast<unsigned long>(image_size), static_cast<unsigned long>(expected_crc32));
    return ESP_OK;
}

static esp_err_t write_chunk_locked(const uint8_t* data, uint16_t len)
{
    if (s_status.state != STATE_IN_PROGRESS || s_ota_handle == 0)
    {
        update_status_locked(STATE_ERROR, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    if (len == 0)
    {
        return ESP_OK;
    }

    if (s_status.expected_size != 0 && (s_status.received_size + len) > s_status.expected_size)
    {
        ESP_LOGE(TAG, "Incoming image exceeds expected size");
        abort_session_locked();
        update_status_locked(STATE_ERROR, ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_write(s_ota_handle, data, len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        abort_session_locked();
        update_status_locked(STATE_ERROR, err);
        return err;
    }

    s_status.running_crc32 = esp_crc32_le(s_status.running_crc32, data, len);
    s_status.received_size += len;
    return ESP_OK;
}

static esp_err_t finish_session_locked()
{
    if (s_status.state != STATE_IN_PROGRESS || s_ota_handle == 0 || s_update_partition == nullptr)
    {
        update_status_locked(STATE_ERROR, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_status.expected_size != 0 && s_status.received_size != s_status.expected_size)
    {
        ESP_LOGE(TAG, "Image size mismatch: got=%lu expected=%lu",
                 static_cast<unsigned long>(s_status.received_size),
                 static_cast<unsigned long>(s_status.expected_size));
        abort_session_locked();
        update_status_locked(STATE_ERROR, ESP_ERR_INVALID_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_expected_crc32 != 0 && s_status.running_crc32 != s_expected_crc32)
    {
        ESP_LOGE(TAG, "CRC mismatch: got=0x%08lx expected=0x%08lx",
                 static_cast<unsigned long>(s_status.running_crc32),
                 static_cast<unsigned long>(s_expected_crc32));
        abort_session_locked();
        update_status_locked(STATE_ERROR, ESP_ERR_INVALID_CRC);
        return ESP_ERR_INVALID_CRC;
    }

    esp_err_t err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        abort_session_locked();
        update_status_locked(STATE_ERROR, err);
        return err;
    }

    err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        abort_session_locked();
        update_status_locked(STATE_ERROR, err);
        return err;
    }

    s_ota_handle = 0;
    s_update_partition = nullptr;
    s_image_ready = true;
    update_status_locked(STATE_READY_TO_REBOOT, ESP_OK);
    ESP_LOGI(TAG, "BLE OTA image accepted and set as next boot partition");
    return ESP_OK;
}

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt* ctxt, void* arg)
{
    (void)arg;
    (void)attr_handle;
    const ble_uuid_t* chr_uuid = ctxt->chr->uuid;

    if (ble_uuid_cmp(chr_uuid, &kStatusUuid.u) == 0)
    {
        std::lock_guard<std::mutex> lock(s_mtx);
        return os_mbuf_append(ctxt->om, &s_status, sizeof(s_status)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    std::array<uint8_t, 512> buf{};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > buf.size())
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (ble_hs_mbuf_to_flat(ctxt->om, buf.data(), len, nullptr) != 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    std::lock_guard<std::mutex> lock(s_mtx);
    s_conn_handle = conn_handle;

    if (ble_uuid_cmp(chr_uuid, &kControlUuid.u) == 0)
    {
        if (len < 1)
        {
            update_status_locked(STATE_ERROR, ESP_ERR_INVALID_ARG);
            notify_status_if_connected();
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        const uint8_t cmd = buf[0];
        esp_err_t err = ESP_OK;

        if (cmd == CMD_START)
        {
            if (len < 9)
            {
                err = ESP_ERR_INVALID_ARG;
            }
            else
            {
                const uint32_t image_size = read_le_u32(&buf[1]);
                const uint32_t expected_crc32 = read_le_u32(&buf[5]);
                err = start_session_locked(image_size, expected_crc32);
            }
        }
        else if (cmd == CMD_FINISH)
        {
            if (len != 1)
            {
                err = ESP_ERR_INVALID_ARG;
            }
            else
            {
                err = finish_session_locked();
            }
        }
        else if (cmd == CMD_ABORT)
        {
            abort_session_locked();
            update_status_locked(STATE_IDLE, ESP_OK);
            err = ESP_OK;
        }
        else if (cmd == CMD_REBOOT)
        {
            if (!s_image_ready)
            {
                err = ESP_ERR_INVALID_STATE;
                update_status_locked(STATE_ERROR, err);
            }
            else
            {
                notify_status_if_connected();
                ESP_LOGI(TAG, "Rebooting into updated firmware");
                esp_restart();
            }
        }
        else
        {
            err = ESP_ERR_INVALID_ARG;
            update_status_locked(STATE_ERROR, err);
        }

        notify_status_if_connected();
        return err == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    }

    if (ble_uuid_cmp(chr_uuid, &kDataUuid.u) == 0)
    {
        esp_err_t err = write_chunk_locked(buf.data(), len);
        notify_status_if_connected();
        return err == ESP_OK ? 0 : BLE_ATT_ERR_UNLIKELY;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static ble_gatt_chr_def gatt_chars[4] = {};

static void configure_gatt_chars()
{
    gatt_chars[0].uuid = &kControlUuid.u;
    gatt_chars[0].access_cb = gatt_access_cb;
    gatt_chars[0].arg = nullptr;
    gatt_chars[0].descriptors = nullptr;
    gatt_chars[0].val_handle = nullptr;
    gatt_chars[0].flags = BLE_GATT_CHR_F_WRITE;
    gatt_chars[0].min_key_size = 0;
    gatt_chars[0].cpfd = nullptr;

    gatt_chars[1].uuid = &kDataUuid.u;
    gatt_chars[1].access_cb = gatt_access_cb;
    gatt_chars[1].arg = nullptr;
    gatt_chars[1].descriptors = nullptr;
    gatt_chars[1].val_handle = nullptr;
    gatt_chars[1].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;
    gatt_chars[1].min_key_size = 0;
    gatt_chars[1].cpfd = nullptr;

    gatt_chars[2].uuid = &kStatusUuid.u;
    gatt_chars[2].access_cb = gatt_access_cb;
    gatt_chars[2].arg = nullptr;
    gatt_chars[2].descriptors = nullptr;
    gatt_chars[2].val_handle = &s_status_val_handle;
    gatt_chars[2].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
    gatt_chars[2].min_key_size = 0;
    gatt_chars[2].cpfd = nullptr;

    gatt_chars[3] = {};
}

static const ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .characteristics = gatt_chars,
    },
    {0},
};

static void advertise();

static int gap_event_cb(ble_gap_event* event, void* arg)
{
    (void)arg;
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            std::lock_guard<std::mutex> lock(s_mtx);
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE client connected");
        }
        else
        {
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        {
            std::lock_guard<std::mutex> lock(s_mtx);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        ESP_LOGI(TAG, "BLE client disconnected");
        advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE MTU updated: %u", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void advertise()
{
    ble_hs_adv_fields fields{};
    const char* dev_name = "Flaps-OTA";
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    ble_uuid128_t uuids[] = {kServiceUuid};
    fields.uuids128 = uuids;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: rc=%d", rc);
        return;
    }

    // Keep device name in scan response to stay within 31-byte ADV payload limit.
    ble_hs_adv_fields rsp_fields{};
    rsp_fields.name = reinterpret_cast<const uint8_t*>(dev_name);
    rsp_fields.name_len = static_cast<uint8_t>(strlen(dev_name));
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: rc=%d", rc);
        return;
    }

    ble_gap_adv_params adv_params{};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, nullptr, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, nullptr);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: rc=%d", rc);
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset: reason=%d", reason);
}

static void on_sync()
{
    uint8_t addr_val[6] = {0};
    if (ble_hs_id_infer_auto(0, &s_own_addr_type) != 0)
    {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed");
        return;
    }
    if (ble_hs_id_copy_addr(s_own_addr_type, addr_val, nullptr) != 0)
    {
        ESP_LOGE(TAG, "ble_hs_id_copy_addr failed");
        return;
    }
    ESP_LOGI(TAG, "BLE ready, address: %02x:%02x:%02x:%02x:%02x:%02x",
             addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);
    advertise();
}

static void host_task(void* param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
} // namespace

esp_err_t ble_ota_init()
{
    std::lock_guard<std::mutex> lock(s_mtx);
    if (s_initialized)
    {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Free Classic BT memory when available to reduce RAM pressure before BLE init.
    esp_err_t mem_rel_err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (mem_rel_err != ESP_OK && mem_rel_err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "esp_bt_controller_mem_release(CLASSIC) returned: %s", esp_err_to_name(mem_rel_err));
    }

    err = nimble_port_init();
    if (err == ESP_ERR_INVALID_STATE)
    {
        // Already initialized by another component.
        ESP_LOGW(TAG, "nimble_port_init: already initialized");
        err = ESP_OK;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "nimble_port_init failed: %s. Check BT config and memory (CONFIG_BT_ENABLED, "
                 "CONFIG_BT_NIMBLE_ENABLED, controller BLE mode).",
                 esp_err_to_name(err));
        return err;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    configure_gatt_chars();

    int rc = ble_svc_gap_device_name_set("Flaps-OTA");
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(host_task);
    s_initialized = true;
    ESP_LOGI(TAG, "BLE OTA service started");
    return ESP_OK;
}

#else
esp_err_t ble_ota_init()
{
    ESP_LOGW("ble_ota", "BLE OTA disabled: enable CONFIG_BT_ENABLED and CONFIG_BT_NIMBLE_ENABLED");
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

#else
esp_err_t ble_ota_init()
{
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
