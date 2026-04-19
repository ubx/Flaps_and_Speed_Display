# Flaps and Speed Display

An ESP32-S3-based flight instrument displays ([ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)) for flaps and speed data.
It uses an AMOLED display with LVGL to provide a graphical gauge for indicated airspeed (IAS) \
and flap position, receiving real-time data via the CAN bus (TWAI).

## Housing
[3D model](housing)

## PCB
[KiCad project](https://github.com/ubx/Flaps_and_Speed_Display_HW)

### todo
- ~~adjust speed gauge size and layout~~
- ~~replace the numeric speed field~~
- ~~read and display the actual flap position (flap)~~
- ~~calculate and display the optimal flap position (flaputils::get_optimal_flap)~~
- ~~show display stale when nothing is received after 10 sec~~
- ~~design a graphical actual/optimal display~~
- ~~on start show about screenwith firmware and version~~
- setup screen ~~with brightness~~, ~~select polare~~...
- ~~implement update ventus3_defaut.json OTA with BLE~~
- demo mode
- ~~special flap positions for start and landing~~
- ~~3D print housing~~
- ~~PCB board~~
- ~~User Manual~~
- ~~Altitude screen~~
- PCB rework, make it more professional!

Next steps:
- extent polare data (JSON files) with speed limits (min, best, Vne)
- improve speed screen with speed limits
- improve speed screen #2 similar to a real ASI