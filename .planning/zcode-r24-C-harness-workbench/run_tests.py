#!/usr/bin/env python3
"""r24c test driver — runs each test binary with the canonical DLL env and
reports REAL exit codes (cmd.exe for-loop %ERRORLEVEL% block-expansion once
produced false "exit 0" lines; this driver exists so that cannot recur)."""
import os
import subprocess
import sys

WT = r"C:\Users\wangj.KEVIN\projects\exp-rs-r24_c-20261027T022556Z"
BUILD = os.path.join(WT, "build-r24_c")
ENV = dict(os.environ)
ENV["PATH"] = (
    BUILD
    + r";C:\deps\Qt\6.8.0\msvc2022_64\bin;"
    + os.path.join(BUILD, r"vcpkg_installed\x64-windows\bin")
    + r";C:\deps\qca-install\bin;C:\deps\kc-install\bin;"
    + ENV["PATH"]
)
ENV["QT_QPA_PLATFORM"] = "offscreen"
ENV.setdefault("PROJ_DATA", os.path.join(BUILD, r"vcpkg_installed\x64-windows\share\proj"))

def run(exe, args=()):
    path = os.path.join(BUILD, exe + ".exe")
    if not os.path.exists(path):
        return 9009, "<missing>"
    p = subprocess.run(
        [path, *args], cwd=BUILD, env=ENV,
        capture_output=True, text=True, timeout=1200, errors="replace",
    )
    tail = (p.stdout or "").strip().splitlines()[-1:] or (p.stderr or "").strip().splitlines()[-1:]
    return p.returncode, tail[0] if tail and tail[0] else ""

if __name__ == "__main__":
    binaries = sys.argv[1:]
    failures = 0
    for b in binaries:
        rc, last = run(b)
        status = "PASS" if rc == 0 else f"FAIL({rc})"
        print(f"{b}: {status} {last}")
        if rc != 0:
            failures += 1
    sys.exit(1 if failures else 0)
