#!/usr/bin/env python3

import json
import math
import argparse
import sys


def interpolate_speed(weight, wk, polar_data):
    weights = polar_data["speedpolar"]["optimale_fluggeschwindigkeit_kmh"]["gewicht_kg"]
    ranges = polar_data["speedpolar"]["optimale_fluggeschwindigkeit_kmh"]["bereiche"]

    # find correct flap entry
    entry = next((r for r in ranges if r["wk"] == wk), None)
    if entry is None:
        raise ValueError(f"Flap setting '{wk}' not found")

    speeds = entry["geschwindigkeit"]

    # exact match
    if str(weight) in speeds:
        return tuple(speeds[str(weight)])

    # find bounding weights
    lower_w = None
    upper_w = None

    for w in weights:
        if w <= weight:
            lower_w = w
        if w >= weight and upper_w is None:
            upper_w = w

    # clamp
    if lower_w is None:
        lower_w = weights[0]
    if upper_w is None:
        upper_w = weights[-1]

    if lower_w == upper_w:
        return tuple(speeds[str(lower_w)])

    v1_min, v1_max = speeds[str(lower_w)]
    v2_min, v2_max = speeds[str(upper_w)]

    # sqrt scaling interpolation
    w_ratio = (math.sqrt(weight) - math.sqrt(lower_w)) / (
            math.sqrt(upper_w) - math.sqrt(lower_w)
    )

    v_min = v1_min + (v2_min - v1_min) * w_ratio
    v_max = v1_max + (v2_max - v1_max) * w_ratio

    return round(v_min, 1), round(v_max, 1)


def load_json(path):
    try:
        with open(path, "r") as f:
            return json.load(f)
    except Exception as e:
        print(f"Error loading JSON: {e}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="Glider speed interpolation tool")

    parser.add_argument("json_file", help="Path to JSON polar file")
    parser.add_argument("--weight", type=float, required=True, help="Aircraft weight in kg")
    parser.add_argument("--wk", required=True, help="Flap setting (e.g. L, +2, 0, -1, S)")

    args = parser.parse_args()

    polar_data = load_json(args.json_file)

    try:
        v_min, v_max = interpolate_speed(args.weight, args.wk, polar_data)
        print(f"Weight: {args.weight} kg")
        print(f"Flap setting: {args.wk}")
        print(f"Recommended speed: {v_min} – {v_max} km/h")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
