import sys
import socket
import struct
from PySide6.QtCore import Qt, Signal, QTimer
from PySide6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QGroupBox, QGridLayout,
    QLabel, QSlider, QSpinBox
)

FLAPS_VALUES = [94, 167, 243, 84, 156, 191, 230, 250]

# CAN frame format: 4 bytes ID, 1 byte length, 3 bytes padding, 8 bytes data
CAN_FRAME_FMT = "=IB3x8s"


def send_can_frame(sock, can_id, data):
    # Padding the data to 8 bytes if needed
    data = data.ljust(8, b'\x00')
    can_pkt = struct.pack(CAN_FRAME_FMT, can_id, len(data), data)
    try:
        sock.send(can_pkt)
    except socket.error as e:
        print(f"Error sending CAN frame: {e}", file=sys.stderr)


class ControlPanel(QWidget):
    # Per-value signals
    iasChanged = Signal(int)     # km/h
    massChanged = Signal(int)    # kg
    flapsChanged = Signal(int)   # V
    windSpeedChanged = Signal(int) # km/h
    windDirectionChanged = Signal(int) # deg
    headingChanged = Signal(int)       # deg

    # Combined signal: dict with current values
    valuesChanged = Signal(dict)

    def __init__(self, sock=None):
        super().__init__()
        self.setWindowTitle("Control Panel")
        self.sock = sock
        self._state = {
            "ias": 40,
            "mass": 390,
            "flaps": FLAPS_VALUES[0],
            "wind_speed": 0,
            "wind_direction": 0,
            "heading": 0,
        }
        self._initializing = True

        root = QVBoxLayout(self)

        box = QGroupBox("Controls")
        grid = QGridLayout(box)
        grid.setColumnStretch(1, 1)

        # ---------- IAS (40..279 km/h) ----------
        self.ias_slider = QSlider(Qt.Horizontal)
        self.ias_slider.setRange(0, 279)
        self.ias_spin = QSpinBox()
        self.ias_spin.setRange(0, 279)
        self.ias_unit = QLabel("km/h")
        self.ias_unit.setMinimumWidth(5)

        self.ias_slider.valueChanged.connect(self.ias_spin.setValue)
        self.ias_spin.valueChanged.connect(self.ias_slider.setValue)
        self.ias_spin.valueChanged.connect(self._on_ias_changed)

        grid.addWidget(QLabel("IAS"), 0, 0)
        grid.addWidget(self.ias_slider, 0, 1)
        grid.addWidget(self.ias_spin, 0, 2)
        grid.addWidget(self.ias_unit, 0, 3)

        # ---------- Mass (390..600 kg) ----------
        self.mass_slider = QSlider(Qt.Horizontal)
        self.mass_slider.setRange(390, 600)
        self.mass_spin = QSpinBox()
        self.mass_spin.setRange(390, 600)
        self.mass_unit = QLabel("kg")
        self.mass_unit.setMinimumWidth(45)

        self.mass_slider.valueChanged.connect(self.mass_spin.setValue)
        self.mass_spin.valueChanged.connect(self.mass_slider.setValue)
        self.mass_spin.valueChanged.connect(self._on_mass_changed)

        grid.addWidget(QLabel("Dry + Ballast Mass"), 1, 0)
        grid.addWidget(self.mass_slider, 1, 1)
        grid.addWidget(self.mass_spin, 1, 2)
        grid.addWidget(self.mass_unit, 1, 3)

        # ---------- Flaps (discrete values, unit V) ----------
        self.flaps_slider = QSlider(Qt.Horizontal)
        self.flaps_slider.setRange(0, len(FLAPS_VALUES) - 1)
        self.flaps_slider.setSingleStep(1)
        self.flaps_slider.setPageStep(1)
        self.flaps_slider.setTickInterval(1)
        self.flaps_slider.setTickPosition(QSlider.TicksBelow)

        self.flaps_value = QLabel()
        self.flaps_value.setMinimumWidth(60)
        self.flaps_value.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.flaps_unit = QLabel("V")
        self.flaps_unit.setMinimumWidth(45)

        self.flaps_slider.valueChanged.connect(self._on_flaps_index_changed)

        grid.addWidget(QLabel(f"Flaps ({', '.join(map(str, FLAPS_VALUES))})"), 2, 0)
        grid.addWidget(self.flaps_slider, 2, 1)
        grid.addWidget(self.flaps_value, 2, 2)
        grid.addWidget(self.flaps_unit, 2, 3)

        # ---------- Wind Speed (0..100 km/h) ----------
        self.wind_speed_slider = QSlider(Qt.Horizontal)
        self.wind_speed_slider.setRange(0, 100)
        self.wind_speed_spin = QSpinBox()
        self.wind_speed_spin.setRange(0, 100)
        self.wind_speed_unit = QLabel("km/h")
        self.wind_speed_unit.setMinimumWidth(45)

        self.wind_speed_slider.valueChanged.connect(self.wind_speed_spin.setValue)
        self.wind_speed_spin.valueChanged.connect(self.wind_speed_slider.setValue)
        self.wind_speed_spin.valueChanged.connect(self._on_wind_speed_changed)

        grid.addWidget(QLabel("Wind Speed"), 3, 0)
        grid.addWidget(self.wind_speed_slider, 3, 1)
        grid.addWidget(self.wind_speed_spin, 3, 2)
        grid.addWidget(self.wind_speed_unit, 3, 3)

        # ---------- Wind Direction (-180..180 deg) ----------
        self.wind_dir_slider = QSlider(Qt.Horizontal)
        self.wind_dir_slider.setRange(-180, 180)
        self.wind_dir_spin = QSpinBox()
        self.wind_dir_spin.setRange(-180, 180)
        self.wind_dir_unit = QLabel("deg")
        self.wind_dir_unit.setMinimumWidth(45)

        self.wind_dir_slider.valueChanged.connect(self.wind_dir_spin.setValue)
        self.wind_dir_spin.valueChanged.connect(self.wind_dir_slider.setValue)
        self.wind_dir_spin.valueChanged.connect(self._on_wind_direction_changed)

        grid.addWidget(QLabel("Wind Direction"), 4, 0)
        grid.addWidget(self.wind_dir_slider, 4, 1)
        grid.addWidget(self.wind_dir_spin, 4, 2)
        grid.addWidget(self.wind_dir_unit, 4, 3)

        # ---------- Heading (0..360 deg) ----------
        self.heading_slider = QSlider(Qt.Horizontal)
        self.heading_slider.setRange(0, 360)
        self.heading_spin = QSpinBox()
        self.heading_spin.setRange(0, 360)
        self.heading_unit = QLabel("deg")
        self.heading_unit.setMinimumWidth(45)

        self.heading_slider.valueChanged.connect(self.heading_spin.setValue)
        self.heading_spin.valueChanged.connect(self.heading_slider.setValue)
        self.heading_spin.valueChanged.connect(self._on_heading_changed)

        grid.addWidget(QLabel("Heading"), 5, 0)
        grid.addWidget(self.heading_slider, 5, 1)
        grid.addWidget(self.heading_spin, 5, 2)
        grid.addWidget(self.heading_unit, 5, 3)

        root.addWidget(box)
        root.addStretch(1)

        # Defaults
        self.ias_spin.setValue(40)
        self.mass_spin.setValue(390)
        self.flaps_slider.setValue(0)
        self._update_flaps_label(0)
        self.wind_speed_spin.setValue(0)
        self.wind_dir_spin.setValue(0)
        self.heading_spin.setValue(0)

        # Timer for re-sending all values every 5 seconds when nothing changes
        self.resend_timer = QTimer(self)
        self.resend_timer.setInterval(5000)
        self.resend_timer.timeout.connect(self._emit_all)
        self.resend_timer.start()

        # Emit initial snapshot once
        self._initializing = False
        self._emit_all()

    # ---- Public helper: current values snapshot ----
    def current_values(self) -> dict:
        return dict(self._state)

    # ---- Internal: signal emitters ----
    def _on_ias_changed(self, v: int):
        self._state["ias"] = int(v)
        if not self._initializing:
            self._emit_all()

    def _on_mass_changed(self, v: int):
        self._state["mass"] = int(v)
        if not self._initializing:
            self._emit_all()

    def _on_wind_speed_changed(self, v: int):
        self._state["wind_speed"] = int(v)
        if not self._initializing:
            self._emit_all()

    def _on_wind_direction_changed(self, v: int):
        self._state["wind_direction"] = int(v)
        if not self._initializing:
            self._emit_all()

    def _on_heading_changed(self, v: int):
        self._state["heading"] = int(v)
        if not self._initializing:
            self._emit_all()

    def _on_flaps_index_changed(self, idx: int):
        self._update_flaps_label(idx)
        self._state["flaps"] = int(self._current_flaps_value())
        if not self._initializing:
            self._emit_all()

    def _emit_all(self):
        # Reset timer whenever we emit (on change or timeout)
        self.resend_timer.start()

        vals = self.current_values()
        self.iasChanged.emit(vals["ias"])
        self.massChanged.emit(vals["mass"])
        self.flapsChanged.emit(vals["flaps"])
        self.windSpeedChanged.emit(vals["wind_speed"])
        self.windDirectionChanged.emit(vals["wind_direction"])
        self.headingChanged.emit(vals["heading"])
        self.valuesChanged.emit(vals)
        self._send_all_can_frames(vals)

    def _send_all_can_frames(self, vals: dict):
        if not self.sock:
            return

        # ID 315: IAS (km/h) -> m/s (/ 3.6)
        # main.cpp: get_float(msg.data) reads big-endian float starting at index 4
        ias_data = bytearray(8)
        struct.pack_into(">f", ias_data, 4, vals["ias"] / 3.6)
        send_can_frame(self.sock, 315, ias_data)

        # ID 1515: dry_and_ballast_mass (Hg -> kg * 10)
        mass_data = bytearray(8)
        struct.pack_into(">H", mass_data, 4, vals["mass"] * 10)
        send_can_frame(self.sock, 1515, mass_data)

        # ID 340: flap
        flaps_data = bytearray(8)
        flaps_data[4] = vals["flaps"] & 0xFF
        send_can_frame(self.sock, 340, flaps_data)

        # ID 333: wind_speed (km/h)
        wind_speed_data = bytearray(8)
        struct.pack_into(">f", wind_speed_data, 4, float(vals["wind_speed"] / 3.6))
        send_can_frame(self.sock, 333, wind_speed_data)

        # ID 334: wind_direction (deg)
        wind_dir_data = bytearray(8)
        struct.pack_into(">f", wind_dir_data, 4, float(vals["wind_direction"]))
        send_can_frame(self.sock, 334, wind_dir_data)

        # ID 321: heading (deg)
        heading_data = bytearray(8)
        struct.pack_into(">f", heading_data, 4, float(vals["heading"]))
        send_can_frame(self.sock, 321, heading_data)

    # ---- Flaps helpers ----
    def _current_flaps_value(self) -> int:
        idx = int(self.flaps_slider.value())
        idx = max(0, min(idx, len(FLAPS_VALUES) - 1))
        return FLAPS_VALUES[idx]

    def _update_flaps_label(self, idx: int):
        idx = max(0, min(int(idx), len(FLAPS_VALUES) - 1))
        self.flaps_value.setText(str(FLAPS_VALUES[idx]))


def main():
    app = QApplication(sys.argv)

    # Open CAN socket
    interface = "can0"
    try:
        sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        sock.bind((interface,))
        print(f"CAN interface {interface} opened.")
    except socket.error as e:
        print(f"Could not open CAN interface {interface}: {e}")
        print("Note: You might need to set up vcan0 if you don't have a real CAN interface.")
        sock = None

    w = ControlPanel(sock)

    def on_ias_changed(v):
        print(f"IAS changed: {v} km/h")

    def on_mass_changed(v):
        print(f"Mass changed: {v} kg")

    def on_flaps_changed(v):
        print(f"Flaps changed: {v} V")

    def on_wind_speed_changed(v):
        print(f"Wind speed changed: {v} km/h")

    def on_wind_direction_changed(v):
        print(f"Wind direction changed: {v} deg")

    def on_heading_changed(v):
        print(f"Heading changed: {v} deg")

    # Example callbacks (replace with your logic / Node-RED / MQTT / etc.)
    w.iasChanged.connect(on_ias_changed)
    w.massChanged.connect(on_mass_changed)
    w.flapsChanged.connect(on_flaps_changed)
    w.windSpeedChanged.connect(on_wind_speed_changed)
    w.windDirectionChanged.connect(on_wind_direction_changed)
    w.headingChanged.connect(on_heading_changed)
    w.valuesChanged.connect(lambda d: print(f"ALL: {d}"))

    w.resize(780, 330)
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
