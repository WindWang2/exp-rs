#!/usr/bin/env python3
"""Crash-after-garbage fake Python inference worker (stdlib only).

First launch (marker file absent): announces the ready line, then answers
the FIRST inference request with a newline-terminated NON-JSON line — a
traceback leaking onto stdout — and immediately hard-exits. That is the
crash-mid-line shape of #1384: the garbage line is the crash's last words,
not a live worker's protocol answer, so the provider must classify it as a
CRASH (bounded restart + replay), never as a permanent output-invalid.

Relaunch (marker present, i.e. the replayed request lands on the restarted
worker): behaves exactly like py_worker_fake.py — deterministic sum answer
— so the replayed forward SUCCEEDS and the oracle can assert end-to-end
recovery. The marker path comes from SICNU_GARBAGE_CRASH_ONCE_MARKER.
"""
import base64
import json
import os
import struct
import sys

DTYPES = {
    "float32": "<%df",
    "float64": "<%dd",
    "int32": "<%di",
    "int64": "<%dq",
    "uint8": "<%dB",
    "int8": "<%db",
}


def last_element(t):
    fmt = DTYPES[t["dtype"]]
    raw = base64.b64decode(t["data_base64"])
    count = 1
    for d in t["shape"]:
        count *= int(d)
    values = struct.unpack(fmt % count, raw)
    return values[count - 1]


def main():
    marker = os.environ.get("SICNU_GARBAGE_CRASH_ONCE_MARKER", "")
    crash_once = marker and not os.path.exists(marker)
    if crash_once:
        with open(marker, "w") as fh:
            fh.write("crashed")
    print(json.dumps({"protocol": "exp-rs-infer/1", "event": "ready"}), flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        if crash_once:
            # Complete garbage line, then die: the line IS newline-terminated
            # (so readLine parses it) and the death follows within
            # milliseconds — inside the provider's settlement window.
            print("Traceback (most recent call last): dying mid-line", flush=True)
            sys.stdout.flush()
            os._exit(1)
        req = json.loads(line)
        try:
            total = float(sum(last_element(t) for t in req["inputs"]))
            data = struct.pack("<1f", total)
            resp = {
                "outputs": [
                    {
                        "name": "sum",
                        "shape": [1, 1, 1, 1],
                        "dtype": "float32",
                        "data_base64": base64.b64encode(data).decode(),
                    }
                ]
            }
        except Exception as exc:  # error contract: one "error" line
            resp = {"error": "worker error: %s" % exc}
        print(json.dumps(resp), flush=True)


if __name__ == "__main__":
    main()
