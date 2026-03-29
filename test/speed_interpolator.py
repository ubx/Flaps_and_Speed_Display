#!/usr/bin/env python3

import json
import math
import argparse
import sys


def interpolate_speed(weight, wk, polar_data):
    weights = polar_data["weights"]
    speedpolar = polar_data["speedpolar"]

    # find correct flap entry
    entry = next((r for r in speedpolar if r["wk"] == wk), None)
    if entry is None:
        raise ValueError(f"Flap setting '{wk}' not found")

    # exact match
    try:
        weight_idx = weights.index(weight)
        return tuple(entry["ranges"][weight_idx])
    except ValueError:
        pass

    # find bounding weights
    lower_idx = None
    upper_idx = None

    for i, w in enumerate(weights):
        if w <= weight:
            lower_idx = i
        if w >= weight and upper_idx is None:
            upper_idx = i

    # clamp
    if lower_idx is None:
        lower_idx = 0
    if upper_idx is None:
        upper_idx = len(weights) - 1

    if lower_idx == upper_idx:
        return tuple(entry["ranges"][lower_idx])

    v1_min, v1_max = entry["ranges"][lower_idx]
    v2_min, v2_max = entry["ranges"][upper_idx]
    lower_w = weights[lower_idx]
    upper_w = weights[upper_idx]

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
