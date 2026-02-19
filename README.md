# Flaps and Speed Display

An ESP32-S3 based flight instrument display for flaps and speed data. 
It uses an AMOLED display with LVGL to provide a graphical gauge for indicated airspeed (IAS) \
and flaps position, receiving real-time data via the CAN bus (TWAI).

### todo
- adjust speed gauge size and layout
- replace the numeric speed field
- read and display the actual flap position (flap)
- calculate and display the optimal flap position (flaputils::get_optimal_flap)
- show display invalid when nothing is received after 10 sec
- design a graphical actual/optimal display
- on start start display firmare and version 
- 3D print housing
- PCB board 
