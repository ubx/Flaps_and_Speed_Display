Import("env")

def fix_include_flag(flags):
    """
    Ensure '-include' is immediately followed by 'lvgl_port_alignment.h'.
    PlatformIO/IDF sometimes mis-orders these and the header becomes a second input file.
    """
    try:
        inc_idx = flags.index("-include")
    except ValueError:
        return flags

    # find the token that ends with lvgl_port_alignment.h
    hdr_idx = None
    for i, t in enumerate(flags):
        if isinstance(t, str) and t.replace("\\", "/").endswith("lvgl_port_alignment.h"):
            hdr_idx = i
            break

    if hdr_idx is None:
        return flags

    # if it's already right after -include, nothing to do
    if hdr_idx == inc_idx + 1:
        return flags

    # move header token to immediately after '-include'
    hdr = flags.pop(hdr_idx)
    # if header was before -include, removing it shifts inc_idx by -1
    if hdr_idx < inc_idx:
        inc_idx -= 1
    flags.insert(inc_idx + 1, hdr)
    return flags

# Patch CCFLAGS/CXXFLAGS in-place
for key in ("CCFLAGS", "CXXFLAGS", "CPPFLAGS"):
    if key in env:
        env[key] = fix_include_flag(list(env[key]))
