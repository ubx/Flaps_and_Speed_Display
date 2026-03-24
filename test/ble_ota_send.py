import asyncio
import argparse
import os
import zlib
from bleak import BleakClient, BleakScanner

# UUIDs must match src/ble_ota.cpp (kControlUuid / kDataUuid).
OTA_CTRL_UUID = "2ae4d630-7d4b-2dae-8b4f-14310bb05d39"
OTA_DATA_UUID = "2ae4d630-7d4b-2dae-8b4f-14310cb05d39"
CMD_START = b"\x01"
CMD_FINISH = b"\x02"
CMD_REBOOT = b"\x04"
TARGET_APP_OTA = 0x00
TARGET_SPIFFS_FILE = 0x02


async def find_by_name(name: str, timeout: float = 8.0) -> str:
    devs = await BleakScanner.discover(timeout=timeout)
    for d in devs:
        if d.name == name:
            return d.address
    raise SystemExit(f"Device '{name}' not found. Seen: {[d.name for d in devs]}")


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="Flaps-OTA", help="BLE adv name")
    ap.add_argument("--address", help="Device address (skip scan)")
    ap.add_argument("--bin", help="Firmware .bin path (app OTA)")
    ap.add_argument("--spiffs-file", help="Local file to upload into SPIFFS over BLE")
    ap.add_argument("--spiffs-remote", default="/spiffs/ventus3_defaut.json",
                    help="Destination path on device for --spiffs-file")
    ap.add_argument("--chunk", type=int, default=240, help="Chunk size (try 240, fallback 180)")
    ap.add_argument("--data-with-response", action="store_true",
                    help="Use write-with-response for DATA too (slower, more reliable)")
    ap.add_argument("--no-reboot", action="store_true",
                    help="Do not send reboot command after successful upload")
    args = ap.parse_args()

    if bool(args.bin) == bool(args.spiffs_file):
        raise SystemExit("Choose exactly one mode: --bin <firmware.bin> OR --spiffs-file <path>")

    is_spiffs_mode = args.spiffs_file is not None
    local_path = args.spiffs_file if is_spiffs_mode else args.bin
    if not os.path.exists(local_path):
        raise SystemExit(f"File not found: {local_path}")

    if is_spiffs_mode:
        remote_path_bytes = args.spiffs_remote.encode("utf-8")
        if len(remote_path_bytes) == 0 or len(remote_path_bytes) > 127:
            raise SystemExit("--spiffs-remote must be between 1 and 127 UTF-8 bytes")
    else:
        remote_path_bytes = b""

    addr = args.address or await find_by_name(args.name)
    with open(local_path, "rb") as f:
        payload = f.read()
    total = len(payload)
    expected_crc32 = zlib.crc32(payload, 0xFFFFFFFF) & 0xFFFFFFFF
    target_mode = TARGET_SPIFFS_FILE if is_spiffs_mode else TARGET_APP_OTA
    print(f"Target: {addr}")
    if is_spiffs_mode:
        print(f"SPIFFS file: {local_path} -> {args.spiffs_remote}")
    else:
        print(f"Firmware: {local_path}")
    print(f"Payload size: {total} bytes")
    print(f"CRC32: 0x{expected_crc32:08x}")

    async with BleakClient(addr) as client:
        # Optional: print negotiated MTU if available (not always supported)
        try:
            mtu = await client.get_mtu()
            print(f"MTU: {mtu}")
        except Exception:
            pass

        # BEGIN (reliable): [cmd][size][crc][optional target/path metadata]
        start = CMD_START + total.to_bytes(4, "little") + expected_crc32.to_bytes(4, "little")
        if is_spiffs_mode:
            start += bytes([target_mode, len(remote_path_bytes)]) + remote_path_bytes
        await client.write_gatt_char(OTA_CTRL_UUID, start, response=True)

        # DATA
        sent = 0
        chunk = args.chunk
        resp = True if args.data_with_response else False

        while sent < total:
            part = payload[sent:sent + chunk]
            await client.write_gatt_char(OTA_DATA_UUID, part, response=resp)
            sent += len(part)

            # progress every 64 KiB
            if sent % (64 * 1024) < chunk:
                print(f"Sent {sent}/{total}")

        # END (reliable)
        await client.write_gatt_char(OTA_CTRL_UUID, CMD_FINISH, response=True)
        if is_spiffs_mode:
            print("Done. SPIFFS file uploaded successfully.")
        elif args.no_reboot:
            print("Done. Firmware accepted; reboot skipped (--no-reboot).")
        else:
            # REBOOT (reliable). The device may reset before ATT write response returns.
            try:
                await client.write_gatt_char(OTA_CTRL_UUID, CMD_REBOOT, response=True)
                print("Done. Reboot command sent; ESP32 should boot into new firmware.")
            except Exception as exc:
                msg = str(exc)
                if "ATT error: 0x0e" in msg or "Not connected" in msg or "disconnected" in msg.lower():
                    print("Done. Firmware accepted and device rebooted (BLE link dropped during reboot).")
                else:
                    raise


if __name__ == "__main__":
    asyncio.run(main())
