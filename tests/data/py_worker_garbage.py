#!/usr/bin/env python3
"""Garbage-answer fake Python inference worker (stdlib only).

Announces the exp-rs-infer/1 ready line like py_worker_fake.py, then answers
every inference request with a NON-JSON line — as if a traceback leaked onto
stdout — and stays ALIVE. Pins the malformed-response error contract in
python_worker_provider: an alive worker that answers garbage must surface as
output-invalid ("malformed response"), never as the phantom crash
"exited unexpectedly", and must never be handed a replay (the stream read
position past the first newline is lost, so no realignment is possible).
"""
import json
import sys
import time


def main():
    print(json.dumps({"protocol": "exp-rs-infer/1", "event": "ready"}), flush=True)
    for line in sys.stdin:
        if not line.strip():
            continue
        print("Traceback (most recent call last): garbage on the wire — not JSON", flush=True)
        # Stay alive so the failure class is decided by the GARBAGE, not by
        # an exit: that is the whole point of this fixture.
        time.sleep(5)


if __name__ == "__main__":
    main()
