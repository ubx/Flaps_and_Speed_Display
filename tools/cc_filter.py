#!/usr/bin/env python3
import os
import sys

BAD_BASENAME = "lvgl_port_alignment.h"

def is_bad_header(s: str) -> bool:
    return s.endswith("/" + BAD_BASENAME) or s == BAD_BASENAME

def main() -> int:
    # Launcher mode: argv[1] is the real compiler
    # SCons-wrapper mode: real compiler comes from env var
    if len(sys.argv) >= 2 and ("/" in sys.argv[1] or sys.argv[1].endswith(("gcc", "g++", "cc", "c++"))):
        real = sys.argv[1]
        in_args = sys.argv[2:]
        mode = "cmake-launcher"
    else:
        real = os.environ.get("PIO_REAL_CXX") or os.environ.get("PIO_REAL_CC")
        in_args = sys.argv[1:]
        mode = "scons-wrapper"

    print(f">>> cc_filter.py CALLED ({mode}) real={real}", file=sys.stderr)
    print(">>> argv head:", sys.argv[:8], file=sys.stderr)

    if not real:
        print("cc_filter.py: missing real compiler (argv[1] or PIO_REAL_CXX/PIO_REAL_CC)", file=sys.stderr)
        return 1

    out = [real]
    i = 0
    while i < len(in_args):
        a = in_args[i]

        # Handle "-include <something>"
        if a == "-include":
            nxt = in_args[i + 1] if i + 1 < len(in_args) else None

            # broken "-include" (no filename or next token is another flag): drop only "-include"
            if nxt is None or nxt.startswith("-"):
                i += 1
                continue

            # "-include lvgl_port_alignment.h": drop both
            if is_bad_header(nxt):
                i += 2
                continue

            # keep the pair
            out.extend([a, nxt])
            i += 2
            continue

        # Also drop stray appearance of the header as a positional "file"
        if is_bad_header(a):
            i += 1
            continue

        out.append(a)
        i += 1

    # Replace current process with the real compiler
    os.execv(real, out)
    return 0  # unreachable

if __name__ == "__main__":
    raise SystemExit(main())
