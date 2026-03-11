import math
import numpy as np


def estimate_wind(track_deg, gs, tas):
    """
    Estimate constant wind vector from multiple samples.

    Inputs:
        track_deg : iterable of GPS track angles in degrees (-180..+180)
        gs        : iterable of ground speeds
        tas       : iterable of true airspeeds

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


if __name__ == "__main__":
    track_deg = [-90, -45, 0, 45, 90, 135, 180, -135]
    gs = [110, 118, 125, 120, 112, 102, 95, 100]
    tas = [105, 105, 105, 105, 105, 105, 105, 105]

    result = estimate_wind(track_deg, gs, tas)
    print(result)
