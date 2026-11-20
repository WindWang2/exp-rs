#!/usr/bin/env python3
"""Trickling Python inference worker (stdlib only) — R35-T02 WP01 fixture.

Prints the exp-rs-infer/1 ready line, then on the FIRST request answers with a
trickle: a few stdout bytes WITHOUT a newline every 25 ms, for a bounded 8 s
window (plus stderr chatter every 150 ms), then goes silent. A byte trickle
that never completes a frame must still hit the provider's absolute read
deadline — never stall the exchange until the max_body_mb cap.

The 8 s bound keeps the RED run bounded too: the pre-fix readLine only checked
its deadline on the no-data path, so this worker delays that failure to ~8 s
(measured by tests/test_r35_t02_python_worker_deadline.cpp) instead of hanging
CTest until the body cap. Not a model: fixture for the wire deadline contract.
"""
import sys
import time


def main():
    print('{"protocol": "exp-rs-infer/1", "event": "ready"}', flush=True)
    line = sys.stdin.readline()  # drain the request so its write never blocks
    del line
    deadline = time.monotonic() + 8.0
    next_stderr = time.monotonic()
    while time.monotonic() < deadline:
        sys.stdout.write("no-newline ")
        sys.stdout.flush()
        now = time.monotonic()
        if now >= next_stderr:
            sys.stderr.write("trickle stderr chatter %f\n" % now)
            sys.stderr.flush()
            next_stderr = now + 0.15
        time.sleep(0.025)
    # Bounded fixture: after the window, stay silent (the caller has either hit
    # the deadline already or the exchange is long abandoned).
    time.sleep(60.0)


if __name__ == "__main__":
    main()
