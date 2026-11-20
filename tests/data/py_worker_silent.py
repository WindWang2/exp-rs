#!/usr/bin/env python3
"""Silent Python inference worker (stdlib only) — R35-T02 WP01 fixture.

Prints the exp-rs-infer/1 ready line, then on the first request goes fully
silent on BOTH stdout and stderr: no bytes at all, process alive. The read
deadline must fire on the first poll window after it expires (this path worked
pre-fix too — it is the guard, not the RED case). Not a model: fixture for the
wire deadline contract.
"""
import sys
import time


def main():
    print('{"protocol": "exp-rs-infer/1", "event": "ready"}', flush=True)
    line = sys.stdin.readline()  # drain the request so its write never blocks
    del line
    time.sleep(60.0)


if __name__ == "__main__":
    main()
