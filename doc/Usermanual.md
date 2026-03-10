# Flaps & Speed Display User Manual

## Purpose

The **Flaps & Speed Display** is a touchscreen flight display for the ESP32-S3 AMOLED unit. It receives live data from the CAN bus and shows:

- indicated airspeed (IAS)
- current flap setting
- recommended flap setting
- calculated flying weight
- display brightness settings

The display is controlled entirely by touch gestures and on-screen buttons.

## Startup

After power-up, the unit shows a short splash screen with:

- application name
- firmware version
- git revision

It then switches to the normal operating screens.

## Touch Navigation

The display has four screens. Navigation is done with swipe gestures:

- **Swipe up or down**: switch between the two main flying screens
- **Swipe right**: open the settings/detail screens
- **Swipe right again**: switch between the two settings/detail screens
- **Swipe left**: return from the settings/detail screens to the main flying screen

## Screen Overview

### 1. Speed Screen

![Speed screen](./Speed_round.png)

This is the primary airspeed display.

- The large number in the center is the **indicated airspeed** in **km/h**
- The white pointer shows the same IAS on the circular scale
- The scale range is **40 to 280 km/h**
- The pointer movement is intentionally damped for stable, instrument-like motion

Use this screen when you want the clearest possible IAS indication.

### 2. Flaps Screen

![Flaps screen](./flaps_round.png)

This screen combines airspeed with flap guidance.

- The large text in the center is the **actual flap position symbol**
- The white pointer still shows **IAS**
- The green arc marks the valid speed bands for the flap schedule
- The highlighted arc segment shows the **target flap range**
- The white triangle indicates the correction direction:
  - triangle above: select a **higher** flap setting
  - triangle below: select a **lower** flap setting
  - no triangle: actual and target flap settings match

Typical flap symbols are `L`, `+2`, `+1`, `0`, `-1`, `-2`, `S`, and `S1`.

### 3. Settings / Brightness Screen

![Brightness screen](./Brightness_round.png)

This screen is used to adjust the display brightness.

- Press `+` to increase brightness
- Press `-` to decrease brightness

Brightness changes in **10% steps**.

- minimum brightness: **30%**
- maximum brightness: **100%**

### 4. Live Params Screen

![Live parameters screen](./Life_Params_round.png)

This screen shows the values used internally for flap guidance.

- **IAS**: indicated airspeed in km/h
- **Weight**: current flying weight used for the flap calculation
- **Flap Actual**: current detected flap symbol and index
- **Flap Target**: recommended flap symbol and index

This is the best screen for troubleshooting or checking why a flap recommendation is being made.

## How Flap Guidance Works

The unit compares:

- current IAS from CAN bus data
- current flap position from CAN bus data
- current weight from CAN bus data
- the configured flap schedule in [`spiffs_data/flapDescriptor.json`](/media/andreas/data2/workspace2/Flaps_and_Speed_Display/spiffs_data/flapDescriptor.json)

## Stale Data Indication

If no relevant live data is received for **10 seconds**, the display marks the active flying screen as stale.

![Stale indication on flaps screen](./Flaps-Stale_round.png)

The stale condition is shown by a large **red X** across the active screen.

This indicates that the shown IAS/flap information should no longer be trusted until valid CAN data is received again.

## Normal Use

1. Power on the unit.
2. Wait until the splash screen finishes.
3. Use the **Speed** screen for normal IAS monitoring.
4. Swipe to the **Flaps** screen when flap guidance is needed.
5. Open **Live Params** if you want to verify actual/target flap values.
6. Open **Settings** to adjust brightness for cockpit conditions.

## Notes

- All speed values are displayed in **km/h**
- Flap recommendations depend on the flap polar stored in the JSON configuration
- If flap input data does not match a known position, the flap indication may show no valid symbol
- The current `flapDescriptor` and the guidance described in this manual are based on the Ventus 3 reference shown below
- ![Ventus 3 reference](./Ventus3-OptSpeed.png)