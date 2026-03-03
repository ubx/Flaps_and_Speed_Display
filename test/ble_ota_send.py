import asyncio
import argparse
import os
import zlib
from bleak import BleakClient, BleakScanner

# UUIDs must match src/ble_ota.cpp (kControlUuid / kDataUuid).
OTA_CTRL_UUID = "2ae4d630-7d4b-2dae-8b4f-14310bb05d39"
OTA_DATA_UUID = "2ae4d630-7d4b-2dae-8b4f-14310cb05d39"


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
    ap.add_argument("--bin", required=True, help="Firmware .bin path")
    ap.add_argument("--chunk", type=int, default=240, help="Chunk size (try 240, fallback 180)")
    ap.add_argument("--data-with-response", action="store_true",
                    help="Use write-with-response for DATA too (slower, more reliable)")
    ap.add_argument("--no-reboot", action="store_true",
                    help="Do not send reboot command after successful upload")
    args = ap.parse_args()

    if not os.path.exists(args.bin):
        raise SystemExit(f"File not found: {args.bin}")

    addr = args.address or await find_by_name(args.name)
    fw = open(args.bin, "rb").read()
    total = len(fw)
    expected_crc32 = zlib.crc32(fw, 0xFFFFFFFF) & 0xFFFFFFFF
    print(f"Target: {addr}")
    print(f"Firmware: {total} bytes")
    print(f"CRC32: 0x{expected_crc32:08x}")

    async with BleakClient(addr) as client:
        # Optional: print negotiated MTU if available (not always supported)
        try:
            mtu = await client.get_mtu()
            print(f"MTU: {mtu}")
        except Exception:
            pass

        # BEGIN (reliable): [cmd=0x01][image_size_u32_le][crc32_u32_le]
        start = b"\x01" + total.to_bytes(4, "little") + expected_crc32.to_bytes(4, "little")
        await client.write_gatt_char(OTA_CTRL_UUID, start, response=True)

        # DATA
        sent = 0
        chunk = args.chunk
        resp = True if args.data_with_response else False

        while sent < total:
            part = fw[sent:sent + chunk]
            await client.write_gatt_char(OTA_DATA_UUID, part, response=resp)
            sent += len(part)

            # progress every 64 KiB
            if sent % (64 * 1024) < chunk:
                print(f"Sent {sent}/{total}")

        # END (reliable)
        await client.write_gatt_char(OTA_CTRL_UUID, b"\x02", response=True)
        if args.no_reboot:
            print("Done. Firmware accepted; reboot skipped (--no-reboot).")
        else:
            # REBOOT (reliable). The device may reset before ATT write response returns.
            try:
                await client.write_gatt_char(OTA_CTRL_UUID, b"\x04", response=True)
                print("Done. Reboot command sent; ESP32 should boot into new firmware.")
            except Exception as exc:
                msg = str(exc)
                if "ATT error: 0x0e" in msg or "Not connected" in msg or "disconnected" in msg.lower():
                    print("Done. Firmware accepted and device rebooted (BLE link dropped during reboot).")
                else:
                    raise


if __name__ == "__main__":
    asyncio.run(main())
