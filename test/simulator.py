#!/usr/bin/env python3
import time
import socket
import struct
import sys
import argparse

# CAN frame format: 4 bytes ID, 1 byte length, 3 bytes padding, 8 bytes data
CAN_FRAME_FMT = "=IB3x8s"

IAS_CAN_ID = 315
ALT_CAN_ID = 322
FLAPS_CAN_ID = 340
DRY_AND_BALLAST_MASS_CAN_ID = 1515

FLAPS_VALUES = [94, 167, 243, 84, 156, 191, 230, 250]


def parse_can_id(id_hex):
    # Standard ID (11-bit)
    return int(id_hex, 16)


def send_can_frame(sock, can_id, data):
    # Padding the data to 8 bytes if needed
    data = data.ljust(8, b'\x00')
    can_pkt = struct.pack(CAN_FRAME_FMT, can_id, len(data), data)
    try:
        sock.send(can_pkt)
    except socket.error as e:
        print(f"Error sending CAN frame: {e}", file=sys.stderr)


def build_ias_frame(ias_kmh):
    # IAS is encoded as m/s in the last four bytes.
    return b"\x00\x00\x00\x00" + struct.pack(">f", ias_kmh / 3.6)


def build_alt_frame(alt_m):
    return b"\x00\x00\x00\x00" + struct.pack(">f", alt_m)


def build_flaps_frame(flaps):
    return bytes([0, 0, 0, 0, flaps])


def build_dry_and_ballast_mass_frame(mass):
    return b"\x00\x00\x00\x00" + struct.pack(">H", mass)


def emit_sweep(sock, time_gap):
    alt = 0.0
    for dry_and_ballast_mass in range(3800, 6001, 200):
        for flaps in FLAPS_VALUES:
            for ias in range(10, 281, 10):
                messages = [
                    (
                        DRY_AND_BALLAST_MASS_CAN_ID,
                        build_dry_and_ballast_mass_frame(dry_and_ballast_mass),
                        f"dry_and_ballast_mass: {dry_and_ballast_mass / 10 } kg",
                    ),
                    (
                        FLAPS_CAN_ID,
                        build_flaps_frame(flaps),
                        f"flap: {flaps}",
                    ),
                    (
                        IAS_CAN_ID,
                        build_ias_frame(ias),
                        f"ias: {ias:.0f} km/h",
                    ),
                    (
                        ALT_CAN_ID,
                        build_alt_frame(alt),
                        f"alt: {alt:.0f} m",
                    ),
                ]

                for can_id, data, msg_info in messages:
                    send_can_frame(sock, can_id, data)
                    print(f"Sent ID {can_id:03X} Data {data.hex().upper()} ({msg_info})")
                
                alt += 10.0
                if time_gap and time_gap > 0:
                    time.sleep(time_gap)


def main():
    parser = argparse.ArgumentParser(
        description="Generate CAN frames for mass, flaps, and IAS on a SocketCAN interface.")
    parser.add_argument("--time-gap", type=float, default=1.0, help="Fixed time gap between sent messages in seconds.")
    parser.add_argument("--loop", action="store_true", help="Loop the generated sweep infinitely.")
    args = parser.parse_args()
    interface = "can0"

    # Open CAN socket
    try:
        sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        sock.bind((interface,))
    except socket.error as e:
        print(f"Could not open CAN interface {interface}: {e}")
        print("Note: You might need to set up vcan0 if you don't have a real CAN interface.")
        sys.exit(1)

    print(f"Generating CAN sweep on {interface}...")

    while True:
        emit_sweep(sock, args.time_gap)

        if not args.loop:
            break

    print("Finished CAN sweep.")


if __name__ == "__main__":
    main()
