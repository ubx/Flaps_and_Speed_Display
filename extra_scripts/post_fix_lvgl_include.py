# post_fix_lvgl_include.py
Import("env")
import re

TARGET = "lvgl_port_alignment.h"

def to_list(v):
    if v is None:
        return []
    if isinstance(v, str):
        return v.split()
    try:
        return list(v)
    except TypeError:
        return [v]

def strip_bad(tokens):
    out = []
    for t in tokens:
        if t == "-include":
            continue
        if isinstance(t, str) and (t.endswith("/" + TARGET) or t == TARGET):
            continue
        out.append(t)
    return out

print(">>> post_fix_lvgl_include.py active (COM patch)")

# 1) Remove from common flag lists (in case it *is* present somewhere)
for key in ("CCFLAGS", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "ASFLAGS", "BUILD_FLAGS"):
    if key in env:
        env[key] = strip_bad(to_list(env[key]))

# 2) Remove from command templates (this is what your log shows is happening)
#    We remove BOTH the naked "-include" token and any token that ends with lvgl_port_alignment.h
def patch_com(s: str) -> str:
    # remove ' -include ' (as a standalone token)
    s = re.sub(r'(^|\s)-include(\s|$)', r'\1', s)
    # remove any path token that ends with lvgl_port_alignment.h
    s = re.sub(r'(^|\s)\S*' + re.escape("/" + TARGET) + r'(\s|$)', r'\1', s)
    s = re.sub(r'(^|\s)' + re.escape(TARGET) + r'(\s|$)', r'\1', s)
    # normalize whitespace
    s = re.sub(r'\s+', ' ', s).strip()
    return s

for key in ("CXXCOM", "CCCOM", "SHCXXCOM", "SHCCCOM"):
    if key in env and isinstance(env[key], str):
        env[key] = patch_com(env[key])
