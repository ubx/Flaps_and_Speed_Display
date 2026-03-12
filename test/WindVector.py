import math
import numpy as np
import time
import socket
import struct
import sys
import collections


def wrap_deg_180(angle_deg):
    """Wrap angle to [-180, +180)."""
    return ((angle_deg + 180.0) % 360.0) - 180.0


def estimate_wind(track_deg, gs, tas,
                  min_tas=1.0,
                  min_gs=1.0,
                  max_residual_keep=None):
    """
    Estimate a constant wind vector from multiple samples.

    Inputs:
        track_deg : iterable of GPS track angles in degrees (0..360 or -180..+180)
        gs        : iterable of ground speeds in m/s
        tas       : iterable of true airspeeds in m/s

    Optional:
        min_tas           : reject samples with TAS < min_tas
        min_gs            : reject samples with GS < min_gs
        max_residual_keep : optional 2nd-pass outlier rejection threshold in m/s

    Returns dict with:
        wx, wy              : wind vector components
        wind_speed          : wind magnitude
        wind_to_deg         : direction the wind vector points TO, deg [-180..+180)
        wind_from_deg       : aviation direction wind comes FROM, deg [-180..+180)
        residual_rms        : RMS fit error in m/s
        residual_mean       : mean fit residual in m/s
        residual_max_abs    : max absolute residual in m/s
        rank                : least-squares matrix rank
        condition_number    : lower is better
        heading_spread_deg  : rough measure of track spread
        n_input             : input sample count
        n_used              : used sample count
        quality             : human-readable quality hint
        samples_used_mask   : boolean mask of used samples
    """

    track_deg = np.asarray(track_deg, dtype=float)
    gs = np.asarray(gs, dtype=float)
    tas = np.asarray(tas, dtype=float)

    if not (len(track_deg) == len(gs) == len(tas)):
        raise ValueError("track_deg, gs, tas must have the same length")

    n_input = len(track_deg)
    if n_input < 3:
        raise ValueError("Need at least 3 samples")

    track_wrapped = np.asarray([wrap_deg_180(x) for x in track_deg], dtype=float)

    valid = (
            np.isfinite(track_wrapped) &
            np.isfinite(gs) &
            np.isfinite(tas) &
            (gs >= min_gs) &
            (tas >= min_tas)
    )

    if np.count_nonzero(valid) < 3:
        raise ValueError("Not enough valid samples after filtering")

    def solve_one_pass(mask):
        trk = np.deg2rad(track_wrapped[mask])
        gs_m = gs[mask]
        tas_m = tas[mask]

        gx = gs_m * np.cos(trk)
        gy = gs_m * np.sin(trk)

        # -2*gx*Wx -2*gy*Wy + C = tas^2 - gx^2 - gy^2
        A = np.column_stack((-2.0 * gx, -2.0 * gy, np.ones_like(gx)))
        b = tas_m**2 - gx**2 - gy**2

        sol, _, rank, svals = np.linalg.lstsq(A, b, rcond=None)
        wx, wy, _c = sol

        tas_fit = np.sqrt((gx - wx) ** 2 + (gy - wy) ** 2)
        res = tas_fit - tas_m

        cond = float("inf")
        if len(svals) >= 2 and np.min(svals) > 0:
            cond = float(np.max(svals) / np.min(svals))

        return {
            "wx": float(wx),
            "wy": float(wy),
            "res": res,
            "rank": int(rank),
            "condition_number": cond,
        }

    fit = solve_one_pass(valid)

    if max_residual_keep is not None:
        used_idx = np.where(valid)[0]
        keep_local = np.abs(fit["res"]) <= max_residual_keep

        refined = valid.copy()
        refined[used_idx] = keep_local

        if np.count_nonzero(refined) >= 3:
            fit = solve_one_pass(refined)
            valid = refined

    wx = fit["wx"]
    wy = fit["wy"]
    res = fit["res"]

    wind_speed = math.hypot(wx, wy)

    wind_to_deg = wrap_deg_180(math.degrees(math.atan2(wy, wx)))
    wind_from_deg = wrap_deg_180(wind_to_deg + 180.0)

    residual_rms = float(np.sqrt(np.mean(res**2)))
    residual_mean = float(np.mean(res))
    residual_max_abs = float(np.max(np.abs(res)))

    used_tracks = track_wrapped[valid]
    if len(used_tracks) >= 2:
        c = np.mean(np.cos(np.deg2rad(used_tracks)))
        s = np.mean(np.sin(np.deg2rad(used_tracks)))
        R = math.hypot(c, s)   # 1=tightly clustered, 0=well spread
        heading_spread_deg = float((1.0 - R) * 180.0)
    else:
        heading_spread_deg = 0.0

    quality_parts = []

    if np.count_nonzero(valid) < 6:
        quality_parts.append("few samples")
    else:
        quality_parts.append("sample count ok")

    if fit["rank"] < 3:
        quality_parts.append("poor geometry")
    elif fit["condition_number"] > 100:
        quality_parts.append("geometry weak")
    elif fit["condition_number"] > 30:
        quality_parts.append("geometry moderate")
    else:
        quality_parts.append("geometry good")

    if residual_rms > 5.0:
        quality_parts.append("fit noisy")
    elif residual_rms > 2.0:
        quality_parts.append("fit fair")
    else:
        quality_parts.append("fit good")

    quality = ", ".join(quality_parts)

    return {
        "wx": float(wx),
        "wy": float(wy),
        "wind_speed": float(wind_speed),
        "wind_to_deg": float(wind_to_deg),
        "wind_from_deg": float(wind_from_deg),
        "residual_rms": residual_rms,
        "residual_mean": residual_mean,
        "residual_max_abs": residual_max_abs,
        "rank": fit["rank"],
        "condition_number": fit["condition_number"],
        "heading_spread_deg": heading_spread_deg,
        "n_input": int(n_input),
        "n_used": int(np.count_nonzero(valid)),
        "quality": quality,
        "samples_used_mask": valid.tolist(),
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

    latest_tas = None
    latest_gs = None
    latest_track = None

    samples_tas = collections.deque(maxlen=100)
    samples_gs = collections.deque(maxlen=100)
    samples_track = collections.deque(maxlen=100)

    last_estimate_time = time.time()
    last_sample_time = 0.0
    sample_interval_s = 0.5      # prevent oversampling
    min_track_step_deg = 3.0     # keep new point if track changed enough

    last_sampled_track = None

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

            # Take a sample mainly on fresh track updates, but throttle it.
            if can_id == 1040 and latest_tas is not None and latest_gs is not None:
                now = time.time()
                track_now = wrap_deg_180(latest_track)

                allow_by_time = (now - last_sample_time) >= sample_interval_s
                allow_by_angle = (
                        last_sampled_track is None or
                        abs(wrap_deg_180(track_now - last_sampled_track)) >= min_track_step_deg
                )

                if allow_by_time or allow_by_angle:
                    samples_tas.append(latest_tas)
                    samples_gs.append(latest_gs)
                    samples_track.append(track_now)

                    last_sample_time = now
                    last_sampled_track = track_now

            current_time = time.time()
            if current_time - last_estimate_time > 2.0 and len(samples_tas) >= 3:
                try:
                    result = estimate_wind(
                        list(samples_track),
                        list(samples_gs),
                        list(samples_tas),
                        min_tas=5.0,
                        min_gs=5.0,
                        max_residual_keep=4.0
                    )

                    print(
                        "Wind: "
                        f"{result['wind_speed']:.1f} m/s "
                        f"from {result['wind_from_deg']:.1f} deg "
                        f"(used={result['n_used']}/{result['n_input']}, "
                        f"rms={result['residual_rms']:.2f}, "
                        f"cond={result['condition_number']:.1f}, "
                        f"spread={result['heading_spread_deg']:.1f}, "
                        f"{result['quality']})"
                    )
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

        result = estimate_wind(
            track_deg,
            gs,
            tas,
            min_tas=1.0,
            min_gs=1.0,
            max_residual_keep=4.0
        )
        print(result)
    else:
        main()