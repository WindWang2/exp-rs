#!/usr/bin/env python3
"""Reference fake Python inference worker (stdlib only).

Implements the exp-rs-infer/1 newline-JSON worker contract used by
python_worker_provider: prints one ready line, then answers one response line
per request line. The "model" is a deterministic known answer: the output is
the SUM of every input tensor's LAST element (last channel, last pixel),
written into a 1x1x1x1 float32 tensor. Exercises the wire contract end to end
without any ML dependency.
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
    print(json.dumps({"protocol": "exp-rs-infer/1", "event": "ready"}), flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        req = json.loads(line)
        try:
            if req.get("artifact", "").endswith("poison.bin"):
                raise RuntimeError("worker refuses poisoned weights")
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
