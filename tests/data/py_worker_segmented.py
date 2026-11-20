#!/usr/bin/env python3
"""Slow-segmented Python inference worker (stdlib only) — R35-T02 WP01 fixture.

Prints the exp-rs-infer/1 ready line, then answers each request like
py_worker_fake.py (output = sum of every input tensor's last element, 1x1x1x1
float32) — but writes the response line in four small segments with 150 ms
pauses. A LEGAL slow frame that completes within the provider's absolute read
deadline must succeed: partial-frame semantics stay legal, only the deadline
itself is absolute. Not a model: fixture for the wire deadline contract.
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
        payload = json.dumps(resp)
        # Four segments, 150 ms apart: slow, progressive, complete (~450 ms).
        quarter = max(1, len(payload) // 4)
        for i in range(0, len(payload), quarter):
            sys.stdout.write(payload[i : i + quarter])
            sys.stdout.flush()
            time.sleep(0.15)
        sys.stdout.write("\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
