#!/usr/bin/env python3
# src/python/scripts/worker_daemon.py — Out-of-Process Python Worker Daemon for SICNU GEO RS
import sys
import os
import argparse
import socket
import json

def main():
    parser = argparse.ArgumentParser(description="SICNU GEO RS Python Worker Daemon")
    parser.add_argument("--socket", required=True, help="Socket name / path for IPC")
    args = parser.parse_args()

    socket_path = args.socket
    if not socket_path.startswith("/") and sys.platform != "win32":
        socket_path = f"/tmp/{socket_path}"

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.connect(socket_path)
    except Exception as e:
        sys.stderr.write(f"WorkerDaemon: Failed to connect to socket {socket_path}: {e}\n")
        sys.exit(1)

    buffer = ""
    while True:
        try:
            data = s.recv(4096)
            if not data:
                break
            buffer += data.decode("utf-8")
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                try:
                    msg = json.loads(line)
                    req_id = msg.get("id")
                    method = msg.get("method")
                    params = msg.get("params", {})

                    if method == "ping":
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "pong", "pid": os.getpid()}
                        }
                        payload = (json.dumps(resp) + "\n").encode("utf-8")
                        s.sendall(payload)
                    else:
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "ok"}
                        }
                        payload = (json.dumps(resp) + "\n").encode("utf-8")
                        s.sendall(payload)
                except Exception as ex:
                    sys.stderr.write(f"WorkerDaemon processing error: {ex}\n")
        except Exception as e:
            sys.stderr.write(f"WorkerDaemon loop error: {e}\n")
            break

    s.close()

if __name__ == "__main__":
    main()
