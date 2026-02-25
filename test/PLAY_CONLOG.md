### CAN Log Player Tool (`play_canlog.py`)

This document describes the `play_canlog.py` test tool, which simulates a CAN bus by playing back recorded messages from `canlog.log`.

### Overview
The `play_canlog.py` script reads the standard SocketCAN log format and sends the messages to the `can0` interface. It maintains the original timing gaps (2 seconds in the current log) between messages and provides decoded information on the terminal.

### Prerequisites

#### 1. Linux SocketCAN
The script uses native Linux SocketCAN support. It requires a `can0` interface to be available.

#### 2. Virtual CAN (vcan0) Setup
If you don't have a physical CAN interface, you can set up a virtual one and map it to `can0`. Run the following commands:
```bash
sudo modprobe vcan
sudo ip link add dev can0 type vcan
sudo ip link set up can0
```

### Usage

1. **Make the script executable**:
   ```bash
   chmod +x test/play_canlog.py
   ```

2. **Run the script**:
   ```bash
   ./test/play_canlog.py
   ```

3. **Optional Arguments**:
   - `--time-gap SECONDS`: Override the timing from the log file with a fixed delay between messages.
     ```bash
     ./test/play_canlog.py --time-gap 0.5
     ```

### Input File: `canlog.log`
The script reads from `test/canlog.log`. The log contains:
- **Flap positions** (ID 154 / 0x154): Values from `test_flaputils.cpp`.
- **Mass** (ID 1515 / 0x5EB): Constant value 4100.
- **IAS** (ID 315 / 0x13B): Ranging from 10 to 80 m/s with an increment of 3.

### Decoded Output
For each message sent, the script prints:
- The absolute timestamp from the log.
- The CAN ID and raw hex data.
- The human-readable parameter name and value (decoded according to `src/main.cpp`).

**Example output**:
```text
Playing test/canlog.log on can0...
[1740476040.000000] Sent ID 154 Data 000000005E000000 (flap: 94)
[1740476042.000000] Sent ID 5EB Data 0000000010040000 (dry_and_ballast_mass: 4100 Hg)
[1740476044.000000] Sent ID 13B Data 0000000041200000 (ias: 10.00 m/s)
...
```

### Notes
- The script uses **standard 11-bit CAN IDs** (non-extended).
- Multi-byte values (IAS, Mass) are encoded in **Big-Endian** format at byte index 4 of the CAN payload.
