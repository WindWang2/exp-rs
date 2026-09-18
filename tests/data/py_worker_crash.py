#!/usr/bin/env python3
"""Crash-test worker: ready handshake, ONE successful inference, then a hard
exit. Used to prove the provider maps worker death to the ProviderCrash
failure kind.

The handshake DECLARES a non-empty capabilities block: the #1056 concurrency
test relies on the bounded restart REWRITING a populated negotiated object
while a foreign thread snapshots it (an empty-to-empty rewrite would race
over nothing)."""
import base64
import json
import os
import struct
import sys


def main():
    print(
        json.dumps(
            {
                "protocol": "exp-rs-infer/1",
                "event": "ready",
                "capabilities": {
                    "max_rank": 5,
                    "multi_input": True,
                    "input_dtypes": ["float32"],
                    "providers": ["crashfake-ep"],
                    "runtime_version": "crashfake-1",
                },
            }
        ),
        flush=True,
    )
    line = sys.stdin.readline()
    req = json.loads(line)
    data = struct.pack("<1f", 42.0)
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
    print(json.dumps(resp), flush=True)
    os._exit(0)  # hard exit: the NEXT request must see a dead worker


if __name__ == "__main__":
    main()
