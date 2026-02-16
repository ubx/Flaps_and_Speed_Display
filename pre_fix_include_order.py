# pre_fix_include_order.py
import shlex

TARGET_BASENAME = "lvgl_port_alignment.h"

def _to_list(v):
    if v is None:
        return []
    if isinstance(v, list):
        return v[:]
    if isinstance(v, str):
        # Split like a shell would (keeps quoted strings intact)
        return shlex.split(v)
    # Fallback
    return [str(v)]

def _from_list(orig, lst):
    # Keep original type (list preferred by SCons)
    return lst

def _fix_one_flag_list(flags):
    """
    Fix broken pattern:
        -include <option>
        ... later ... /path/to/lvgl_port_alignment.h
    into:
        -include /path/to/lvgl_port_alignment.h
        ... (and remove the later standalone header token)
    """
    if not flags:
        return flags

    # Find the standalone header token (usually absolute path)
    hdr_idx = None
    for i, f in enumerate(flags):
        if f.endswith("/" + TARGET_BASENAME) or f == TARGET_BASENAME:
            hdr_idx = i
            break

    # Find any "-include" occurrences
    i = 0
    while i < len(flags):
        if flags[i] == "-include":
            # If no next token -> remove it
            if i + 1 >= len(flags):
                flags.pop(i)
                continue

            nxt = flags[i + 1]

            # If next token is another option, it's broken.
            if nxt.startswith("-"):
                # If we have the header token elsewhere, move it here
                if hdr_idx is not None:
                    hdr = flags[hdr_idx]
                    # Remove header token from its old place first
                    if hdr_idx > i:
                        flags.pop(hdr_idx)
                    else:
                        flags.pop(hdr_idx)
                        i -= 1  # list shrank before i

                    # Insert header as the argument to -include
                    flags.insert(i + 1, hdr)
                    # done for this -include
                else:
                    # Can't repair → remove lonely -include
                    flags.pop(i)
                    continue
            else:
                # "-include <something>" is already valid
                pass
        i += 1

    return flags

def _patch_env(env):
    for key in ("CCFLAGS", "CXXFLAGS", "CPPFLAGS", "LINKFLAGS", "BUILD_FLAGS"):
        if key in env:
            orig = env[key]
            lst = _to_list(orig)
            lst2 = _fix_one_flag_list(lst)
            env[key] = _from_list(orig, lst2)

Import("env")
_patch_env(env)
