#!/usr/bin/env python3
"""Stderr-chattering Python inference worker (stdlib only) — R35-T02 WP01 fixture.

Prints the exp-rs-infer/1 ready line, then on the first request keeps stderr
LOUD (a line every 150 ms, for a bounded 8 s) while stdout stays silent. The
deadline loop drains stderr at the same cadence; the chatter must neither
postpone the stdout deadline nor grow host memory unboundedly. Not a model:
fixture for the wire deadline contract.
"""
import sys
import time


def main():
    print('{"protocol": "exp-rs-infer/1", "event": "ready"}', flush=True)
    line = sys.stdin.readline()  # drain the request so its write never blocks
    del line
    deadline = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        sys.stderr.write("stderr chatter stdout silent %f\n" % time.monotonic())
        sys.stderr.flush()
        time.sleep(0.15)
    time.sleep(60.0)


if __name__ == "__main__":
    main()
