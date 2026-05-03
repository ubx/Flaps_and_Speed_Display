# BLE OTA Sender (`test/ble_ota_send.py`)

This script sends data to the ESP32 BLE OTA service.

It supports two modes:

1. App firmware OTA (`--bin`)
2. SPIFFS file upload (`--spiffs-file` or `--spiffs-dir`)

Exactly one mode must be selected.

## Requirements

- Device is running firmware with BLE OTA service enabled.
- Device advertises as `Flaps-OTA` (or pass `--name` / `--address`).
- Python environment has `bleak` installed.

## Basic Usage

### 1) Firmware OTA (app partition)

```bash
cd ..
python test/ble_ota_send.py --bin .pio/build/esp32-s3-flaps/firmware.bin
```

Behavior:

- Sends `START` with size + CRC32.
- Streams firmware chunks over BLE.
- Sends `FINISH`.
- Sends `REBOOT` unless `--no-reboot` is set.

### 2) Upload SPIFFS files

#### Single file (`ventus3_defaut.json`)

```bash
cd ..
python test/ble_ota_send.py --spiffs-file spiffs_data/ventus3_defaut.json
```

Default remote target path: `/spiffs/ventus3_defaut.json`

#### Multiple files

```bash
cd ..
python test/ble_ota_send.py \
  --spiffs-file spiffs_data/ventus3_defaut.json \
  --spiffs-file spiffs_data/ventus3_3T_SE.json
```

#### All JSON files in a directory

```bash
cd ..
python test/ble_ota_send.py --spiffs-dir spiffs_data
```

Custom remote path (only for single file):

```bash
cd ..
python test/ble_ota_send.py \
  --spiffs-file spiffs_data/ventus3_defaut.json \
  --spiffs-remote /spiffs/ventus3_defaut.json
```

Behavior:

- Sends `START` with SPIFFS target metadata.
- Streams file content over BLE.
- Sends `FINISH`.
- Does not reboot in SPIFFS mode.

## Options

- `--name <ble_name>`: BLE advertised name (default: `Flaps-OTA`)
- `--address <mac_or_addr>`: connect directly without scanning
- `--bin <firmware.bin>`: firmware OTA mode
- `--spiffs-file <local_path>`: SPIFFS upload mode (can be repeated)
- `--spiffs-dir <dir_path>`: upload all .json files from directory
- `--spiffs-remote <remote_path>`: destination path for single file (default: `/spiffs/<filename>`)
- `--chunk <n>`: transfer chunk size in bytes (default: `240`)
- `--data-with-response`: send DATA writes with response (slower, can be more reliable)
- `--no-reboot`: skip reboot after firmware upload

## Notes

- For firmware mode, a BLE disconnect right after the reboot command can be normal.
- For SPIFFS mode, destination must be under `/spiffs/`.
