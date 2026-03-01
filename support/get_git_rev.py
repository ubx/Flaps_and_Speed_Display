Import("env")
import subprocess

try:
    git_rev = subprocess.check_output(
        ["git", "rev-parse", "--short=8", "HEAD"],
        stderr=subprocess.STDOUT
    ).decode().strip()
except Exception:
    git_rev = "nogit"

env.Replace(PROGNAME=f"_{git_rev}")
