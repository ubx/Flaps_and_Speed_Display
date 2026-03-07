# Simulator

`test/simulator.py` generates CAN frames directly on `can0`. It does not read from a log file.

## Sweep

The script iterates through this parameter sweep:

- `dry_and_ballast_mass`: `380` to `600 kg`, increment `20`
- `flaps`: `94, 167, 243, 84, 156, 191, 230, 250`
- `ias`: `40` to `280 km/h`, increment `10`

For each `(dry_and_ballast_mass, flaps, ias)` combination, it sends these CAN frames in order:

1. `dry_and_ballast_mass` on CAN ID `1515`
2. `flaps` on CAN ID `340`
3. `ias` on CAN ID `315`

## Encoding

- `dry_and_ballast_mass`: 2-byte payload, stored as big-endian unsigned 16-bit value
- `flaps`: 1-byte payload
- `ias`: 8-byte payload, input is in `km/h`, encoded as big-endian float in `m/s` in bytes `4..7`

On the wire:

- `dry_and_ballast_mass` is sent with DLC `2`
- `flaps` is sent with DLC `1`
- `ias` is sent with DLC `8`

For SocketCAN transport, the internal frame buffer is still padded to 8 bytes as required, but the transmitted DLC matches the actual payload size.

## Timing

- `--time-gap <seconds>`: delay between each generated combination
- `--loop`: repeat the full sweep indefinitely

Without `--loop`, the script runs the sweep once and exits.

## Example

```bash
cd ..
python3 test/simulator.py --time-gap 1.0 --loop
```
