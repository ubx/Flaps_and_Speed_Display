### SPEED_INTERPOLATOR

`speed_interpolator.py` is a command-line tool designed to calculate the optimal speed range for a glider based on its current aircraft weight and flap setting. It uses a square-root scaling interpolation method to provide accurate speed recommendations across different weight configurations using data from a polar JSON file.

#### Features
- **Weight-Based Interpolation**: Automatically interpolates between provided weight points in the polar data using square-root scaling.
- **Flap Selection**: Supports various flap settings (e.g., L, +2, 0, -1, S) as defined in the JSON configuration.
- **Clamping**: Automatically clamps to the minimum or maximum weight available in the polar data if the specified weight is out of range.
- **JSON Integration**: Works with the standard glider polar JSON format used in the project.

#### Requirements
- Python 3

#### Usage
The script requires the path to a JSON polar file, the aircraft weight, and the flap setting.

```bash
python3 test/speed_interpolator.py <json_file> --weight <weight_kg> --wk <flap_setting>
```

**Example:**
```bash
cd ..
python3 test/speed_interpolator.py spiffs_data/ventus3_defaut.json --weight 500 --wk +1
```

**Output:**
```text
Weight: 500.0 kg
Flap setting: +1
Recommended speed: 89.6 – 101.2 km/h
```

#### JSON Data Structure
The tool expects a JSON file containing a `speedpolar` section with the following structure:
- `gewicht_kg`: A list of reference weights.
- `bereiche`: A list of flap setting entries, each containing:
    - `wk`: The flap setting identifier.
    - `geschwindigkeit`: A dictionary mapping weight strings to speed range pairs `[min_speed, max_speed]`.
