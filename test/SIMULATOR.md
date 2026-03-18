# Simulator

`test/simulator.py` generates CAN frames directly on `can0`. It does not read from a log file.

## Sweep

The script iterates through this parameter sweep:

- `dry_and_ballast_mass`: `380` to `600 kg` (sent as `3800` to `6000`), increment `20` (`200`)
- `flaps`: `94, 167, 243, 84, 156, 191, 230, 250`
- `ias`: `10` to `280 km/h`, increment `10`

For each `(dry_and_ballast_mass, flaps, ias)` combination, it sends these CAN frames in order:

1. `dry_and_ballast_mass` on CAN ID `1515`
2. `flaps` on CAN ID `340`
3. `ias` on CAN ID `315`
4. `alt` on CAN ID `322`

## Encoding

- `dry_and_ballast_mass`: stored as big-endian unsigned 16-bit value in bytes `4..5` (value is 10 * kg)
- `flaps`: stored in byte `4`
- `ias`: input is in `km/h`, encoded as big-endian float in `m/s` in bytes `4..7`
- `alt`: input is in `m`, encoded as big-endian float in bytes `4..7`

Unused bytes are set to `0`.

## Timing

- `--time-gap <seconds>`: delay between each generated combination
- `--loop`: repeat the full sweep indefinitely

Without `--loop`, the script runs the sweep once and exits.

## Example

```bash
cd ..
python3 test/simulator.py --time-gap 1.0 --loop
```
