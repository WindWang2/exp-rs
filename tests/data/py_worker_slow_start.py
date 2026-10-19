#!/usr/bin/env python3
"""Slow-start Python inference worker (stdlib only).

Prints the exp-rs-infer/1 ready line, then SLEEPS without draining stdin past
the provider's request deadline, and only then serves requests like
py_worker_fake.py (output = sum of every input tensor's last element).

Used by the R18-B write-deadline regression in tests/test_provider_python.cpp:
a LIVE worker that stops accepting writes must fail the session closed — the
abandoned request's late answer may never be served to the next caller.
"""
import base64
import json
import struct
import sys
import time

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
    print(json.dumps({"protocol": "exp-rs-infer/1", "event": "ready"}), flush=True)
    # Outlast the caller's write deadline (tests pin runtime.provider.timeout_ms
    # to 1500 ms) AND the follow-up request below it, without reading stdin.
    time.sleep(6.0)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
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
