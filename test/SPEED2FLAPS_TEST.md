### SPEED2FLAPS_TEST

`speed2flaps_teste.py` is a PySide6-based graphical control panel used to simulate flight data (IAS, Mass, and Flaps) and transmit it over a SocketCAN interface. This tool is designed to test the `Flaps_and_Speed_Display` firmware by mimicking real-time CAN messages from a flight computer.

#### Features
- **IAS Control**: Adjust Indicated Airspeed (40–279 km/h). Sends CAN ID `315` (float, m/s).
- **Mass Control**: Adjust Glider Mass (390–600 kg). Sends CAN ID `1515` (uint16, Hg).
- **Flaps Control**: Select from predefined flap sensor voltage values. Sends CAN ID `340` (uint8).
- **Real-time Transmission**: Automatically sends CAN frames to the `can0` interface upon any value change.

#### Requirements
- Python 3
- PySide6
- A Linux system with `can0` or `vcan0` configured.

#### Usage
```bash
cd ..
python3 test/speed2flaps_teste.py
```
