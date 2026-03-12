import math
import numpy as np
import time
import socket
import struct
import sys
import collections


def estimate_wind(track_deg, gs, tas):
    """
    Estimate constant wind vector from multiple samples.

    Inputs:
        track_deg : iterable of GPS track angles in degrees (0..360 or -180..+180)
        gs        : iterable of ground speeds in m/s
        tas       : iterable of true airspeeds in m/s

    Returns dict with:
        wx, wy            : wind vector components
        wind_speed        : wind magnitude
        wind_to_deg       : direction the wind vector points TO, in deg (-180..+180)
        wind_from_deg     : meteorological/aviation direction wind comes FROM, in deg (-180..+180)
        residual_rms      : RMS fit error in speed units
        n                 : number of samples used
    """

    track_deg = np.asarray(track_deg, dtype=float)
    gs = np.asarray(gs, dtype=float)
    tas = np.asarray(tas, dtype=float)

    if not (len(track_deg) == len(gs) == len(tas)):
        raise ValueError("track_deg, gs, tas must have the same length")

    if len(track_deg) < 3:
        raise ValueError("Need at least 3 samples")

    # Convert track angle to radians
    trk = np.deg2rad(track_deg)

    # Ground vector components
    gx = gs * np.cos(trk)
    gy = gs * np.sin(trk)

    # Linear system:
    # -2*gx*Wx -2*gy*Wy + C = tas^2 - gx^2 - gy^2
    A = np.column_stack((-2.0 * gx, -2.0 * gy, np.ones_like(gx)))
    b = tas**2 - gx**2 - gy**2

    # Least squares solve
    sol, _, _, _ = np.linalg.lstsq(A, b, rcond=None)
    wx, wy, _c = sol

    wind_speed = math.hypot(wx, wy)

    # Direction wind vector points TO
    wind_to_deg = math.degrees(math.atan2(wy, wx))
    wind_to_deg = ((wind_to_deg + 180.0) % 360.0) - 180.0

    # Direction wind comes FROM
    wind_from_deg = wind_to_deg + 180.0
    wind_from_deg = ((wind_from_deg + 180.0) % 360.0) - 180.0

    # Residual error: ||G - W|| should equal TAS
    tas_fit = np.sqrt((gx - wx)**2 + (gy - wy)**2)
    residual = tas_fit - tas
    residual_rms = float(np.sqrt(np.mean(residual**2)))

    return {
        "wx": float(wx),
        "wy": float(wy),
        "wind_speed": float(wind_speed),
        "wind_to_deg": float(wind_to_deg),
        "wind_from_deg": float(wind_from_deg),
        "residual_rms": residual_rms,
        "n": int(len(track_deg)),
    }


def main():
    # CAN frame format: 4 bytes ID, 1 byte length, 3 bytes padding, 8 bytes data
    CAN_FRAME_FMT = "=IB3x8s"

    interface = "can0"
    if len(sys.argv) > 1:
        interface = sys.argv[1]

    try:
        sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        sock.bind((interface,))
    except socket.error as e:
        print(f"Could not open CAN interface {interface}: {e}")
        sys.exit(1)

    print(f"Listening for wind estimation data on {interface}...")

    # Buffers to store samples
    # We need samples of (track_deg, gs, tas) taken at the same time or close to each other.
    # Since these are broadcast on CAN at different intervals, we'll store the latest value
    # for each and take a "snapshot" when all are available.

    latest_tas = None
    latest_gs = None
    latest_track = None

    samples_tas = collections.deque(maxlen=100)
    samples_gs = collections.deque(maxlen=100)
    samples_track = collections.deque(maxlen=100)

    last_estimate_time = time.time()

    while True:
        try:
            can_pkt = sock.recv(16)
            can_id, length, data = struct.unpack(CAN_FRAME_FMT, can_pkt)
            
            # IDs as in main.cpp
            if can_id == 316:  # tas
                latest_tas = struct.unpack(">f", data[4:8])[0]
            elif can_id == 1039:  # gps_ground_speed
                latest_gs = struct.unpack(">f", data[4:8])[0]
            elif can_id == 1040:  # gps_true_track
                latest_track = struct.unpack(">f", data[4:8])[0]

            # If we have all values, we can record a sample. 
            # To avoid oversampling, maybe we only take a sample when track changes significantly 
            # or at fixed intervals. 
            # For now, let's take a sample every time we get a fresh set of values (e.g. after track update)
            if can_id == 1040 and latest_tas is not None and latest_gs is not None:
                samples_tas.append(latest_tas)
                samples_gs.append(latest_gs)
                samples_track.append(latest_track)

            # Periodically estimate wind
            current_time = time.time()
            if current_time - last_estimate_time > 2.0 and len(samples_tas) >= 3:
                try:
                    result = estimate_wind(list(samples_track), list(samples_gs), list(samples_tas))
                    print(f"Wind: {result['wind_speed']:.1f} m/s from {result['wind_from_deg']:.1f} deg (n={result['n']}, rms={result['residual_rms']:.2f})")
                except Exception as e:
                    print(f"Estimation error: {e}")
                last_estimate_time = current_time

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Error: {e}")

    print("Exiting...")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--test":
        track_deg = [-90, -45, 0, 45, 90, 135, 180, -135]
        gs = [110, 118, 125, 120, 112, 102, 95, 100]
        tas = [105, 105, 105, 105, 105, 105, 105, 105]

        result = estimate_wind(track_deg, gs, tas)
        print(result)
    else:
        main()
