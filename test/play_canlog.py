#!/usr/bin/env python3
import time
import socket
import struct
import sys
import os
import argparse

# CAN frame format: 4 bytes ID, 1 byte length, 3 bytes padding, 8 bytes data
CAN_FRAME_FMT = "=IB3x8s"

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

def main():
    parser = argparse.ArgumentParser(description="Play a CAN log file on a SocketCAN interface.")
    parser.add_argument("--time-gap", type=float, help="Fixed time gap between messages in seconds. If not given, uses timestamps from log.")
    args = parser.parse_args()

    log_file = os.path.join(os.path.dirname(__file__), "canlog.log")
    interface = "can0"

    if not os.path.exists(log_file):
        print(f"Log file not found: {log_file}")
        sys.exit(1)

    # Open CAN socket
    try:
        sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        sock.bind((interface,))
    except socket.error as e:
        print(f"Could not open CAN interface {interface}: {e}")
        print("Note: You might need to set up vcan0 if you don't have a real CAN interface.")
        sys.exit(1)

    print(f"Playing {log_file} on {interface}...")

    last_ts = None
    start_real_time = time.time()
    
    with open(log_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Format: (timestamp) interface id#data
            try:
                ts_part, rest = line.split(') ', 1)
                ts = float(ts_part.lstrip('('))
                
                _, can_part = rest.split(' ', 1)
                id_hex, data_hex = can_part.split('#')
                
                can_id = parse_can_id(id_hex)
                data = bytes.fromhex(data_hex)
                
                # Timing logic
                if last_ts is not None:
                    if args.time_gap is not None:
                        time_diff = args.time_gap
                    else:
                        time_diff = ts - last_ts
                    # Wait for the next message
                    if time_diff > 0:
                        time.sleep(time_diff)
                
                last_ts = ts
                
                # Send the message
                send_can_frame(sock, can_id, data)
                
                # Decoding logic as described in main.cpp
                id_int = int(id_hex, 16)
                msg_info = ""
                
                if id_int == 315:
                    val = struct.unpack(">f", data[4:8])[0] * 3.6
                    msg_info = f"ias: {val:.0f} km/h"
                elif id_int == 316:
                    val = struct.unpack(">f", data[4:8])[0]
                    msg_info = f"tas: {val:.2f}"
                elif id_int == 317:
                    val = struct.unpack(">f", data[4:8])[0]
                    msg_info = f"cas: {val:.2f}"
                elif id_int == 322:
                    val = struct.unpack(">f", data[4:8])[0]
                    msg_info = f"alt: {val:.2f}"
                elif id_int == 340:
                    val = data[4]
                    msg_info = f"flap: {val}"
                elif id_int == 354:
                    val = struct.unpack(">f", data[4:8])[0]
                    msg_info = f"vario: {val:.2f}"
                elif id_int == 1036:
                    val = struct.unpack(">i", data[4:8])[0] / 1E7
                    msg_info = f"lat: {val:.7f}"
                elif id_int == 1037:
                    val = struct.unpack(">i", data[4:8])[0] / 1E7
                    msg_info = f"lon: {val:.7f}"
                elif id_int == 1039:
                    val = struct.unpack(">f", data[4:8])[0]
                    msg_info = f"gs: {val:.2f}"
                elif id_int == 1040:
                    val = struct.unpack(">f", data[4:8])[0]
                    msg_info = f"tt: {val:.2f}"
                elif id_int == 1515:
                    val = struct.unpack(">H", data[4:6])[0]
                    msg_info = f"dry_and_ballast_mass: {val} Hg"
                elif id_int == 1506:
                    val = struct.unpack(">H", data[4:6])[0]
                    msg_info = f"enl: {val}"
                else:
                    msg_info = "Unknown"

                # Print message as requested
                print(f"[{ts:.6f}] Sent ID {id_hex} Data {data_hex} ({msg_info})")
                
            except Exception as e:
                print(f"Error parsing line: {line}\nException: {e}", file=sys.stderr)

    print("Finished playing log.")

if __name__ == "__main__":
    main()
