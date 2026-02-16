Import("env")
import re

BAD = "lvgl_port_alignment.h"

def scrub_flags(flags):
    if isinstance(flags, str):
        parts = flags.split()
    else:
        parts = list(flags)

    out = []
    i = 0
    while i < len(parts):
        a = parts[i]

        # remove broken "-include" without a path
        if a == "-include":
            nxt = parts[i+1] if i+1 < len(parts) else None
            if nxt is None or nxt.startswith("-"):
                i += 1
                continue
            # drop the lvgl header include pair too
            if nxt.endswith("/" + BAD) or nxt == BAD:
                i += 2
                continue
            out.extend([a, nxt])
            i += 2
            continue

        # drop accidental header appearing as an input file
        if a.endswith("/" + BAD) or a == BAD:
            i += 1
            continue

        out.append(a)
        i += 1
    return out

# scrub the common flag lists
env.Replace(
    CCFLAGS=scrub_flags(env.get("CCFLAGS", [])),
    CXXFLAGS=scrub_flags(env.get("CXXFLAGS", [])),
)

# scrub command templates (string form)
for k in ("CCCOM", "CXXCOM"):
    if k in env:
        s = env[k]
        s = re.sub(r"\s-include(\s+-[A-Za-z0-9_-]+)", r"\1", s)  # kill "-include -something"
        s = s.replace(BAD, "")  # last resort
        env[k] = s

print(">>> fix_include_flag.py active: scrubbed -include and lvgl_port_alignment.h")
