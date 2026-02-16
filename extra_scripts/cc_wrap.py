Import("env")
import os

proj = env.subst("$PROJECT_DIR")
wrapper = os.path.join(proj, "tools", "cc_filter.py")
print(">>> wrapper:", wrapper)
# Remember the real compiler and point CC/CXX to the wrapper
real_cxx = env.subst("$CXX")
real_cc  = env.subst("$CC")

env["ENV"]["PIO_REAL_CXX"] = real_cxx
env["ENV"]["PIO_REAL_CC"]  = real_cc

env.Replace(CXX=wrapper, CC=wrapper)

# Force SCons to call our wrapper via the command templates too
if "CXXCOM" in env:
    env["CXXCOM"] = env["CXXCOM"].replace("$CXX", "$CXX").replace("${CXX}", "${CXX}")
if "CCCOM" in env:
    env["CCCOM"] = env["CCCOM"].replace("$CC", "$CC").replace("${CC}", "${CC}")

# Hard override (more direct)
env.Replace(
    CCCOM="$CC $CCFLAGS $_CCCOMCOM -c $SOURCE -o $TARGET",
    CXXCOM="$CXX $CXXFLAGS $_CCCOMCOM -c $SOURCE -o $TARGET",
)

print(">>> cc_wrap.py: using compiler wrapper")
print(">>> real CXX:", real_cxx)
print(">>> real CC :", real_cc)
