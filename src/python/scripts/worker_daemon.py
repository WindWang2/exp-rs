#!/usr/bin/env python3
# src/python/scripts/worker_daemon.py — Out-of-Process Python Worker Daemon for SICNU GEO RS
import sys
import os
import argparse
import socket
import json
import struct
import mmap
import numpy as np

callbacks = {}

def process_shm(shm_key, multiply_factor=2.0):
    shm_name = shm_key.lstrip('/')
    shm_file = f"/dev/shm/{shm_name}"
    if not os.path.exists(shm_file):
        raise FileNotFoundError(f"Shared memory file not found: {shm_file}")

    with open(shm_file, "r+b") as f:
        mm = mmap.mmap(f.fileno(), 0)
        header_data = mm[:64]
        uuid, width, height, bands, data_type, ref_count, data_size = struct.unpack("36siiiiiQ", header_data)

        element_count = width * height * bands
        if element_count == 0:
            element_count = data_size // 4

        arr = np.frombuffer(mm, dtype=np.float32, count=element_count, offset=64)
        arr *= multiply_factor
        del arr
        mm.close()

    return {"status": "success", "element_count": element_count}

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
                    elif method == "shm_process":
                        shm_key = params.get("shm_key")
                        factor = float(params.get("multiply", 2.0))
                        res = process_shm(shm_key, factor)
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": res
                        }
                    elif method == "ui.test_register_action":
                        cb_id = params.get("callback_id", "cb_test_001")
                        callbacks[cb_id] = True
                        req_msg = {
                            "jsonrpc": "2.0",
                            "method": "ui.add_plugin_menu",
                            "params": {
                                "menu_title": "测试菜单",
                                "action_title": "运行测试动作",
                                "callback_id": cb_id
                            },
                            "id": req_id
                        }
                        s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))
                        continue
                    elif method == "ui.on_action_triggered":
                        cb_id = params.get("callback_id")
                        if cb_id in callbacks:
                            callbacks[cb_id] = "triggered"
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "action_executed", "callback_id": cb_id}
                        }
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
                    err_resp = {
                        "jsonrpc": "2.0",
                        "id": msg.get("id"),
                        "error": {"message": str(ex)}
                    }
                    s.sendall((json.dumps(err_resp) + "\n").encode("utf-8"))
        except Exception as e:
            sys.stderr.write(f"WorkerDaemon loop error: {e}\n")
            break

    s.close()

if __name__ == "__main__":
    main()
