#!/usr/bin/env python3
"""R32-T04 WP05 test helper: a deliberately protocol-violating worker.

Speaks the sicnu worker line protocol (v1) on stdin/stdout. After a VALID
handshake it answers the first `run` with a frame chosen by argv[1]:

  bad_progress  {"v":1,"op":"progress","value":"not-a-number"}   (typed read)
  bad_op        {"v":1,"op":42}                                  (typed read)
  bad_result_id {"v":1,"op":"result","jobId":{"x":1},"payload":{}} (typed read)
  throwing_progress  a VALID progress frame, then a VALID result (the host's
                     onProgress callback is the one that throws)

Everything is line-delimited JSON exactly like the real worker, so the pool's
workerReadFrame/parseFrame layer accepts it and only the CONSUMER-side typed
admission (or exception safety) is exercised.
"""
import json
import sys

MODE = sys.argv[1] if len(sys.argv) > 1 else "bad_progress"


def send(obj):
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def main():
    send({"v": 1, "op": "ready", "caps": []})
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            frame = json.loads(line)
        except ValueError:
            continue
        op = frame.get("op")
        if op == "run":
            job_id = frame.get("jobId", "")
            if MODE == "bad_progress":
                send({"v": 1, "op": "progress", "value": "not-a-number",
                      "message": "typed violation", "jobId": job_id})
                send({"v": 1, "op": "result", "jobId": job_id, "payload": {}})
            elif MODE == "bad_op":
                send({"v": 1, "op": 42, "jobId": job_id})
                send({"v": 1, "op": "result", "jobId": job_id, "payload": {}})
            elif MODE == "bad_result_id":
                send({"v": 1, "op": "result", "jobId": {"x": 1}, "payload": {}})
            elif MODE == "throwing_progress":
                send({"v": 1, "op": "progress", "value": 0.5,
                      "message": "valid progress", "jobId": job_id})
                send({"v": 1, "op": "result", "jobId": job_id, "payload": {"done": True}})
            else:
                send({"v": 1, "op": "result", "jobId": job_id, "payload": {}})
        elif op == "shutdown":
            return


if __name__ == "__main__":
    main()
