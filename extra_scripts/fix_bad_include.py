Import("env")

BAD = "lvgl_port_alignment.h"

def scrub(seq):
    # seq can be list or string
    parts = seq.split() if isinstance(seq, str) else list(seq)
    out = []
    i = 0
    while i < len(parts):
        a = parts[i]

        # remove "-include" with missing/invalid argument
        if a == "-include":
            nxt = parts[i+1] if i+1 < len(parts) else None
            if nxt is None or nxt.startswith("-"):
                i += 1
                continue
            # drop "-include <lvgl_port_alignment.h>" too (if it ever appears correctly)
            if nxt.endswith("/" + BAD) or nxt == BAD:
                i += 2
                continue
            out.extend([a, nxt])
            i += 2
            continue

        # remove the header if it appears as a positional input file
        if a.endswith("/" + BAD) or a == BAD:
            i += 1
            continue

        out.append(a)
        i += 1
    return out

# Scrub flags
env.Replace(
    CCFLAGS=scrub(env.get("CCFLAGS", [])),
    CXXFLAGS=scrub(env.get("CXXFLAGS", [])),
)

print(">>> fix_bad_include.py active: removed stray -include and lvgl_port_alignment.h-as-input")
