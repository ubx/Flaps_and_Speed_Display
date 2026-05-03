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
    ap.add_argument("--spiffs-file", action="append", help="Local file(s) to upload into SPIFFS over BLE. Can be used multiple times.")
    ap.add_argument("--spiffs-dir", help="Directory containing JSON files to upload to SPIFFS")
    ap.add_argument("--spiffs-remote", help="Destination path on device (default: /spiffs/<filename>). Only for single file.")
    ap.add_argument("--chunk", type=int, default=240, help="Chunk size (try 240, fallback 180)")
    ap.add_argument("--data-with-response", action="store_true",
                    help="Use write-with-response for DATA too (slower, more reliable)")
    ap.add_argument("--no-reboot", action="store_true",
                    help="Do not send reboot command after successful upload")
    args = ap.parse_args()

    if bool(args.bin) and (bool(args.spiffs_file) or bool(args.spiffs_dir)):
        raise SystemExit("Choose either --bin OR --spiffs-file/--spiffs-dir")
    if not args.bin and not args.spiffs_file and not args.spiffs_dir:
        raise SystemExit("No input file specified. Use --bin, --spiffs-file, or --spiffs-dir")

    files_to_send = []

    if args.bin:
        files_to_send.append((args.bin, None, TARGET_APP_OTA))

    if args.spiffs_file:
        for f in args.spiffs_file:
            remote = args.spiffs_remote if (len(args.spiffs_file) == 1 and args.spiffs_remote) else f"/spiffs/{os.path.basename(f)}"
            files_to_send.append((f, remote, TARGET_SPIFFS_FILE))

    if args.spiffs_dir:
        if not os.path.isdir(args.spiffs_dir):
            raise SystemExit(f"Directory not found: {args.spiffs_dir}")
        for f in os.listdir(args.spiffs_dir):
            if f.endswith(".json"):
                full_path = os.path.join(args.spiffs_dir, f)
                files_to_send.append((full_path, f"/spiffs/{f}", TARGET_SPIFFS_FILE))

    addr = args.address or await find_by_name(args.name)
    
    print(f"Target: {addr}")
    for lp, rp, tm in files_to_send:
        if tm == TARGET_SPIFFS_FILE:
            print(f"  SPIFFS: {lp} -> {rp}")
        else:
            print(f"  Firmware: {lp}")

    async with BleakClient(addr) as client:
        try:
            mtu = await client.get_mtu()
            print(f"MTU: {mtu}")
        except Exception:
            pass

        for local_path, remote_path, target_mode in files_to_send:
            if not os.path.exists(local_path):
                print(f"File not found: {local_path}. Skipping.")
                continue

            with open(local_path, "rb") as f:
                payload = f.read()
            total = len(payload)
            expected_crc32 = zlib.crc32(payload, 0xFFFFFFFF) & 0xFFFFFFFF

            print(f"\nSending {local_path}...")
            if target_mode == TARGET_SPIFFS_FILE:
                print(f"Target: SPIFFS -> {remote_path}")
            else:
                print(f"Target: Firmware")
            print(f"Size: {total} bytes, CRC32: 0x{expected_crc32:08x}")

            # BEGIN
            start = CMD_START + total.to_bytes(4, "little") + expected_crc32.to_bytes(4, "little")
            if target_mode == TARGET_SPIFFS_FILE:
                remote_path_bytes = remote_path.encode("utf-8")
                if len(remote_path_bytes) > 127:
                    print(f"Error: Remote path too long: {remote_path}")
                    continue
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
                if sent % (64 * 1024) < chunk:
                    print(f"  Sent {sent}/{total}")

            # FINISH
            await client.write_gatt_char(OTA_CTRL_UUID, CMD_FINISH, response=True)
            print(f"Finished {local_path}")

        if args.bin and not args.no_reboot:
            # REBOOT (only for firmware mode)
            try:
                await client.write_gatt_char(OTA_CTRL_UUID, CMD_REBOOT, response=True)
                print("\nReboot command sent.")
            except Exception as exc:
                if "ATT error: 0x0e" in str(exc) or "disconnected" in str(exc).lower():
                    print("\nDevice rebooted (link dropped).")
                else:
                    raise


if __name__ == "__main__":
    asyncio.run(main())
