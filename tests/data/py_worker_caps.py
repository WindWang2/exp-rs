#!/usr/bin/env python3
"""Capability-negotiating fake worker (Platform 8.0 WP-F).

Same wire contract as py_worker_fake.py, but the ready handshake DECLARES a
reduced capability surface: max_rank 5, no multi-input, float32-only inputs.
Used to prove the provider adopts worker-declared capabilities instead of its
historical defaults (handshake negotiation, never assumptions).
"""
import base64
import json
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
    print(
        json.dumps(
            {
                "protocol": "exp-rs-infer/1",
                "event": "ready",
                "capabilities": {
                    "max_rank": 5,
                    "multi_input": False,
                    "input_dtypes": ["float32"],
                    "output_dtypes": ["float32"],
                },
            }
        ),
        flush=True,
    )
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
