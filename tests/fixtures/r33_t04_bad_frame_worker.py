#!/usr/bin/env python3
# r33_t04_bad_frame_worker.py — R33-T04 WP04/WP05 test helper.
#
# Speaks the LocalWorkerHost wire protocol v1 (worker side) over stdin/stdout
# line-delimited JSON, with intentionally MALFORMED payload members for the
# test-only algorithm ids:
#
#   r33t04:bad_progress   → progress frame whose "value" is a STRING
#   r33t04:bad_result     → result frame whose "jobId" is a NUMBER
#   r33t04:ok             → a fully legal result frame
#   anything else         → legal typed error frame (unknown algorithm)
#
# Legal framing (v=1, op present, string members) is preserved everywhere
# else, so the host's JSON gate passes and the typed per-op admission is
# what must catch these frames.
import json
import sys


def send(frame):
    sys.stdout.write(json.dumps(frame, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def main():
    send({"v": 1, "op": "ready", "caps": ["progress", "cancelAck"]})
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            frame = json.loads(line)
        except ValueError:
            continue
        op = frame.get("op")
        if op != "run":
            if op == "cancel":
                job = frame.get("jobId", "")
                send({"v": 1, "op": "ack", "jobId": job, "kind": "cancel"})
                send({"v": 1, "op": "error", "jobId": job, "message": "cancelled",
                      "code": "cancelled"})
            elif op == "shutdown":
                return
            continue
        job = frame.get("jobId", "")
        algo = frame.get("algorithmId", "")
        if algo == "r33t04:bad_progress":
            send({"v": 1, "op": "progress", "jobId": job, "value": "high",
                  "message": "malformed"})
            # Wait for the host to tear us down.
            import time
            time.sleep(30)
        elif algo == "r33t04:bad_result":
            send({"v": 1, "op": "result", "jobId": 5, "payload": {}})
            import time
            time.sleep(30)
        elif algo == "r33t04:ok":
            send({"v": 1, "op": "progress", "jobId": job, "value": 0.5})
            send({"v": 1, "op": "result", "jobId": job,
                  "payload": {"status": "ok"}})
        else:
            send({"v": 1, "op": "error", "jobId": job,
                  "message": "unknown algorithm: " + str(algo)})


if __name__ == "__main__":
    main()
