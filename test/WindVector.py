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


def weighted_lstsq(A, b, w):
    """
    Solve weighted least squares:
        minimize sum_i w_i * (A_i x - b_i)^2
    """
    w = np.asarray(w, dtype=float)
    if np.any(w <= 0):
        raise ValueError("All weights must be > 0")

    sw = np.sqrt(w)
    Aw = A * sw[:, None]
    bw = b * sw
    sol, _, rank, svals = np.linalg.lstsq(Aw, bw, rcond=None)
    return sol, rank, svals


def robust_scale_mad(x, eps=1e-6):
    """
    Robust scale estimate from MAD.
    Returns sigma ~= std for Gaussian data.
    """
    x = np.asarray(x, dtype=float)
    med = np.median(x)
    mad = np.median(np.abs(x - med))
    sigma = 1.4826 * mad
    return max(float(sigma), eps)


def huber_weights(residuals, k=1.5, scale=None, eps=1e-9):
    """
    Huber robust weights for IRLS.

    residuals : array
    k         : threshold in sigma units
    scale     : robust scale; if None, estimated from residuals

    Returns weights in (0, 1].
    """
    r = np.asarray(residuals, dtype=float)
    if scale is None:
        scale = robust_scale_mad(r)

    t = np.abs(r) / max(scale, eps)
    w = np.ones_like(t)
    mask = t > k
    w[mask] = (k / np.maximum(t[mask], eps))
    return w, scale


def estimate_wind(track_deg, gs, tas,
                  min_tas=1.0,
                  min_gs=1.0,
                  max_residual_keep=None,
                  weights=None,
                  robust=True,
                  robust_k=1.5,
                  robust_max_iter=10,
                  robust_tol=1e-3):
    """
    Estimate a constant wind vector from multiple samples.

    Inputs:
        track_deg : iterable of GPS track angles in degrees (0..360 or -180..+180)
        gs        : iterable of ground speeds in m/s
        tas       : iterable of true airspeeds in m/s

    Optional:
        min_tas           : reject samples with TAS < min_tas
        min_gs            : reject samples with GS < min_gs
        max_residual_keep : optional final hard residual cutoff in m/s
        weights           : optional base per-sample weights, same length as input
        robust            : enable robust IRLS weighting
        robust_k          : Huber threshold in sigma units
        robust_max_iter   : max IRLS iterations
        robust_tol        : convergence tolerance on [wx, wy, C]

    Returns dict with:
        wx, wy
        wind_speed
        wind_to_deg
        wind_from_deg
        residual_rms
        weighted_residual_rms
        residual_mean
        residual_max_abs
        robust_scale
        robust_iterations
        robust_weight_min
        robust_weight_max
        rank
        condition_number
        heading_spread_deg
        n_input
        n_used
        quality
        samples_used_mask
    """

    track_deg = np.asarray(track_deg, dtype=float)
    gs = np.asarray(gs, dtype=float)
    tas = np.asarray(tas, dtype=float)

    if not (len(track_deg) == len(gs) == len(tas)):
        raise ValueError("track_deg, gs, tas must have the same length")

    n_input = len(track_deg)
    if n_input < 3:
        raise ValueError("Need at least 3 samples")

    if weights is None:
        weights = np.ones(n_input, dtype=float)
    else:
        weights = np.asarray(weights, dtype=float)
        if len(weights) != n_input:
            raise ValueError("weights must have same length as inputs")

    track_wrapped = np.asarray([wrap_deg_180(x) for x in track_deg], dtype=float)

    valid = (
            np.isfinite(track_wrapped) &
            np.isfinite(gs) &
            np.isfinite(tas) &
            np.isfinite(weights) &
            (gs >= min_gs) &
            (tas >= min_tas) &
            (weights > 0.0)
    )

    if np.count_nonzero(valid) < 3:
        raise ValueError("Not enough valid samples after filtering")

    def build_problem(mask):
        trk = np.deg2rad(track_wrapped[mask])
        gs_m = gs[mask]
        tas_m = tas[mask]

        gx = gs_m * np.cos(trk)
        gy = gs_m * np.sin(trk)

        # -2*gx*Wx -2*gy*Wy + C = tas^2 - gx^2 - gy^2
        A = np.column_stack((-2.0 * gx, -2.0 * gy, np.ones_like(gx)))
        b = tas_m**2 - gx**2 - gy**2
        return gx, gy, tas_m, A, b

    def solve_one_pass(mask):
        gx, gy, tas_m, A, b = build_problem(mask)
        base_w = weights[mask].copy()

        # Initial plain weighted solve
        sol, rank, svals = weighted_lstsq(A, b, base_w)

        robust_w = np.ones_like(base_w)
        robust_scale = 0.0
        robust_iterations = 0

        if robust:
            prev_sol = sol.copy()

            for it in range(robust_max_iter):
                wx, wy, _c = sol
                tas_fit = np.sqrt((gx - wx) ** 2 + (gy - wy) ** 2)
                res = tas_fit - tas_m

                robust_w, robust_scale = huber_weights(res, k=robust_k)
                total_w = base_w * robust_w

                sol, rank, svals = weighted_lstsq(A, b, total_w)
                robust_iterations = it + 1

                if np.max(np.abs(sol - prev_sol)) < robust_tol:
                    break
                prev_sol = sol.copy()

        wx, wy, _c = sol
        tas_fit = np.sqrt((gx - wx) ** 2 + (gy - wy) ** 2)
        res = tas_fit - tas_m

        total_w = base_w * robust_w

        cond = float("inf")
        if len(svals) >= 2 and np.min(svals) > 0:
            cond = float(np.max(svals) / np.min(svals))

        return {
            "wx": float(wx),
            "wy": float(wy),
            "res": res,
            "base_w": base_w,
            "robust_w": robust_w,
            "total_w": total_w,
            "robust_scale": float(robust_scale),
            "robust_iterations": int(robust_iterations),
            "rank": int(rank),
            "condition_number": cond,
        }

    fit = solve_one_pass(valid)

    # Optional final hard rejection after robust solve
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
    total_w = fit["total_w"]
    robust_w = fit["robust_w"]

    wind_speed = math.hypot(wx, wy)
    wind_to_deg = wrap_deg_180(math.degrees(math.atan2(wy, wx)))
    wind_from_deg = wrap_deg_180(wind_to_deg + 180.0)

    residual_rms = float(np.sqrt(np.mean(res**2)))
    weighted_residual_rms = float(np.sqrt(np.sum(total_w * res**2) / np.sum(total_w)))
    residual_mean = float(np.mean(res))
    residual_max_abs = float(np.max(np.abs(res)))

    used_tracks = track_wrapped[valid]
    if len(used_tracks) >= 2:
        c = np.mean(np.cos(np.deg2rad(used_tracks)))
        s = np.mean(np.sin(np.deg2rad(used_tracks)))
        R = math.hypot(c, s)
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

    if weighted_residual_rms > 5.0:
        quality_parts.append("fit noisy")
    elif weighted_residual_rms > 2.0:
        quality_parts.append("fit fair")
    else:
        quality_parts.append("fit good")

    if robust and np.min(robust_w) < 0.3:
        quality_parts.append("outliers suppressed")

    quality = ", ".join(quality_parts)

    return {
        "wx": float(wx),
        "wy": float(wy),
        "wind_speed": float(wind_speed),
        "wind_to_deg": float(wind_to_deg),
        "wind_from_deg": float(wind_from_deg),
        "residual_rms": residual_rms,
        "weighted_residual_rms": weighted_residual_rms,
        "residual_mean": residual_mean,
        "residual_max_abs": residual_max_abs,
        "robust_scale": float(fit["robust_scale"]),
        "robust_iterations": int(fit["robust_iterations"]),
        "robust_weight_min": float(np.min(robust_w)),
        "robust_weight_max": float(np.max(robust_w)),
        "rank": fit["rank"],
        "condition_number": fit["condition_number"],
        "heading_spread_deg": heading_spread_deg,
        "n_input": int(n_input),
        "n_used": int(np.count_nonzero(valid)),
        "quality": quality,
        "samples_used_mask": valid.tolist(),
    }


def prune_old_samples(samples, now_s, window_sec):
    """Remove samples older than the sliding time window."""
    cutoff = now_s - window_sec
    while samples and samples[0]["t"] < cutoff:
        samples.popleft()


def make_age_weights(samples, now_s, tau_sec):
    """
    Exponential weighting by age:
        w = exp(-age / tau_sec)
    """
    if tau_sec <= 0:
        raise ValueError("tau_sec must be > 0")

    weights = []
    for s in samples:
        age = max(0.0, now_s - s["t"])
        w = math.exp(-age / tau_sec)
        weights.append(w)
    return weights


def main():
    # CAN frame format: 4 bytes ID, 1 byte length, 3 bytes padding, 8 bytes data
    CAN_FRAME_FMT = "=IB3x8s"

    interface = "can0"
    if len(sys.argv) > 1 and not sys.argv[1].startswith("--"):
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

    # Each sample: {"t": ..., "track": ..., "gs": ..., "tas": ...}
    samples = collections.deque()

    last_estimate_time = time.time()
    last_sample_time = 0.0
    last_sampled_track = None

    window_sec = 30.0
    weight_tau_sec = 12.0
    sample_interval_s = 0.5
    min_track_step_deg = 3.0
    estimate_period_s = 2.0

    while True:
        try:
            can_pkt = sock.recv(16)
            can_id, length, data = struct.unpack(CAN_FRAME_FMT, can_pkt)

            if can_id == 316:  # tas
                latest_tas = struct.unpack(">f", data[4:8])[0]
            elif can_id == 1039:  # gps_ground_speed
                latest_gs = struct.unpack(">f", data[4:8])[0]
            elif can_id == 1040:  # gps_true_track
                latest_track = struct.unpack(">f", data[4:8])[0]

            now = time.time()

            if can_id == 1040 and latest_tas is not None and latest_gs is not None:
                track_now = wrap_deg_180(latest_track)

                allow_by_time = (now - last_sample_time) >= sample_interval_s
                allow_by_angle = (
                        last_sampled_track is None or
                        abs(wrap_deg_180(track_now - last_sampled_track)) >= min_track_step_deg
                )

                if allow_by_time or allow_by_angle:
                    samples.append({
                        "t": now,
                        "track": track_now,
                        "gs": latest_gs,
                        "tas": latest_tas,
                    })
                    last_sample_time = now
                    last_sampled_track = track_now

            prune_old_samples(samples, now, window_sec)

            if (now - last_estimate_time) >= estimate_period_s:
                if len(samples) >= 3:
                    track_arr = [s["track"] for s in samples]
                    gs_arr = [s["gs"] for s in samples]
                    tas_arr = [s["tas"] for s in samples]
                    weights = make_age_weights(samples, now, weight_tau_sec)

                    try:
                        result = estimate_wind(
                            track_arr,
                            gs_arr,
                            tas_arr,
                            min_tas=5.0,
                            min_gs=5.0,
                            max_residual_keep=8.0,   # optional hard backup cutoff
                            weights=weights,
                            robust=True,
                            robust_k=1.5,
                            robust_max_iter=10,
                            robust_tol=1e-3,
                        )

                        age_oldest = now - samples[0]["t"]
                        age_newest = now - samples[-1]["t"]
                        w_min = min(weights)
                        w_max = max(weights)

                        print(
                            "Wind: "
                            f"{result['wind_speed']:.1f} m/s "
                            f"from {result['wind_from_deg']:.1f} deg "
                            f"(used={result['n_used']}/{result['n_input']}, "
                            f"wrms={result['weighted_residual_rms']:.2f}, "
                            f"cond={result['condition_number']:.1f}, "
                            f"spread={result['heading_spread_deg']:.1f}, "
                            f"rscale={result['robust_scale']:.2f}, "
                            f"rw={result['robust_weight_min']:.2f}..{result['robust_weight_max']:.2f}, "
                            f"agew={w_min:.3f}..{w_max:.3f}, "
                            f"window={age_oldest:.1f}s..{age_newest:.1f}s ago, "
                            f"{result['quality']})"
                        )
                    except Exception as e:
                        print(f"Estimation error: {e}")
                else:
                    print(f"Wind: not enough samples in last {window_sec:.0f}s")

                last_estimate_time = now

        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Error: {e}")

    print("Exiting...")


if __name__ == "__main__":
    if "--test" in sys.argv:
        track_deg = [-120, -90, -60, -30, 0, 30, 60, 90, 120, 150]
        gs = [92, 96, 102, 108, 111, 109, 103, 97, 91, 88]
        tas = [100, 100, 100, 100, 100, 100, 100, 100, 100, 130]  # deliberate outlier
        weights = np.linspace(0.3, 1.0, len(track_deg))

        result = estimate_wind(
            track_deg,
            gs,
            tas,
            min_tas=1.0,
            min_gs=1.0,
            max_residual_keep=8.0,
            weights=weights,
            robust=True,
            robust_k=1.5,
            robust_max_iter=10,
            robust_tol=1e-3,
        )
        print(result)
    else:
        main()