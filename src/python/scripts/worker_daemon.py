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

import importlib

callbacks = {}
loaded_plugins = {}
algo_executors = {}

class SicnuMapCanvasProxy:
    def __init__(self, socket_conn):
        self._s = socket_conn

    def _get_state(self):
        req_msg = {
            "jsonrpc": "2.0",
            "method": "canvas.get_state",
            "params": {},
            "id": 8001
        }
        self._s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))
        buf = ""
        while True:
            data = self._s.recv(4096)
            if not data:
                break
            buf += data.decode("utf-8")
            if "\n" in buf:
                line, _ = buf.split("\n", 1)
                return json.loads(line).get("result", {})
        return {}

    def extent(self):
        st = self._get_state()
        return st.get("extent", [0, 0, 0, 0])

    def scale(self):
        st = self._get_state()
        return st.get("scale", 1.0)


class SicnuMessageBarProxy:
    def __init__(self, socket_conn):
        self._s = socket_conn

    def pushMessage(self, title, text, level="info"):
        req_msg = {
            "jsonrpc": "2.0",
            "method": "ui.push_message_bar",
            "params": {
                "title": title,
                "text": text,
                "level": level
            },
            "id": 8002
        }
        self._s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))


class SicnuPythonIface:
    def __init__(self, socket_conn):
        self._s = socket_conn
        self._canvas_proxy = SicnuMapCanvasProxy(socket_conn)
        self._message_bar_proxy = SicnuMessageBarProxy(socket_conn)

    def addPluginToMenu(self, title, action):
        cb_id = f"cb_{id(action)}"
        callbacks[cb_id] = action
        action_text = action.text() if hasattr(action, "text") and callable(action.text) else str(action)
        req_msg = {
            "jsonrpc": "2.0",
            "method": "ui.add_plugin_menu",
            "params": {
                "menu_title": title,
                "action_title": action_text,
                "callback_id": cb_id
            },
            "id": 9999
        }
        self._s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))

    def activeLayer(self):
        req_msg = {
            "jsonrpc": "2.0",
            "method": "catalog.get_active_layer",
            "params": {},
            "id": 8003
        }
        self._s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))
        buf = ""
        while True:
            data = self._s.recv(4096)
            if not data:
                break
            buf += data.decode("utf-8")
            if "\n" in buf:
                line, _ = buf.split("\n", 1)
                return json.loads(line).get("result", {})
        return None

    def mapCanvas(self):
        return self._canvas_proxy

    def messageBar(self):
        return self._message_bar_proxy

    def addRasterLayer(self, path, name):
        req_msg = {
            "jsonrpc": "2.0",
            "method": "data.add_layer",
            "params": {"path": path, "name": name, "type": "raster"},
            "id": 8004
        }
        self._s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))

    def registerProcessingAlgorithm(self, algo_id, name="", group="Python Plugins", description="", execute_fn=None):
        if execute_fn is not None:
            algo_executors[algo_id] = execute_fn
        req_msg = {
            "jsonrpc": "2.0",
            "method": "processing.register_algorithm",
            "params": {
                "id": algo_id,
                "name": name,
                "group": group,
                "description": description
            },
            "id": 8005
        }
        self._s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))

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
                    if "method" not in msg and ("result" in msg or "error" in msg):
                        continue

                    req_id = msg.get("id")
                    method = msg.get("method")
                    params = msg.get("params", {})

                    if method == "ping":
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "pong", "pid": os.getpid()}
                        }
                    elif method == "load_plugin":
                        plugin_dir = params.get("plugin_dir")
                        package_name = params.get("package_name")
                        if plugin_dir:
                            parent_dir = os.path.dirname(os.path.abspath(plugin_dir))
                            if parent_dir not in sys.path:
                                sys.path.insert(0, parent_dir)
                        mod = importlib.import_module(package_name)
                        if hasattr(mod, "classFactory"):
                            iface_obj = SicnuPythonIface(s)
                            plugin_obj = mod.classFactory(iface_obj)
                            if hasattr(plugin_obj, "initGui"):
                                plugin_obj.initGui()
                            loaded_plugins[package_name] = plugin_obj
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "loaded", "package_name": package_name}
                        }
                    elif method == "unload_plugin":
                        package_name = params.get("package_name")
                        if package_name in loaded_plugins:
                            plugin_obj = loaded_plugins.pop(package_name)
                            if hasattr(plugin_obj, "unload"):
                                plugin_obj.unload()
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "unloaded", "package_name": package_name}
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
                            "id": 9999
                        }
                        s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "action_registered", "callback_id": cb_id}
                        }
                    elif method == "ui.on_action_triggered":
                        cb_id = params.get("callback_id")
                        if cb_id in callbacks:
                            cb = callbacks[cb_id]
                            if callable(cb):
                                cb()
                            elif hasattr(cb, "trigger") and callable(cb.trigger):
                                cb.trigger()
                            elif hasattr(cb, "run") and callable(cb.run):
                                cb.run()
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "action_executed", "callback_id": cb_id}
                        }
                    elif method == "processing.execute_algorithm":
                        algo_id = params.get("id")
                        if algo_id not in algo_executors:
                            resp = {
                                "jsonrpc": "2.0",
                                "id": req_id,
                                "error": {
                                    "code": -32602,
                                    "message": f"Unknown algorithm: {algo_id}"
                                }
                            }
                        else:
                            try:
                                exec_result = algo_executors[algo_id](params.get("params", {}))
                                resp = {
                                    "jsonrpc": "2.0",
                                    "id": req_id,
                                    "result": {"status": "ok", "result": exec_result}
                                }
                            except Exception as ex:
                                resp = {
                                    "jsonrpc": "2.0",
                                    "id": req_id,
                                    "error": {
                                        "code": -32000,
                                        "message": f"Algorithm {algo_id} failed: {ex}"
                                    }
                                }
                    elif method == "processing.test_register_algorithm":
                        iface_obj = SicnuPythonIface(s)
                        iface_obj.registerProcessingAlgorithm(
                            "py:echo_test",
                            "Echo Test",
                            execute_fn=lambda p: {"echo": p}
                        )
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "algorithm_registered"}
                        }
                    elif method == "crash_test":
                        sys.stderr.write("WorkerDaemon simulating segfault crash!\n")
                        sys.stderr.flush()
                        os._exit(139)
                    else:
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "error": {
                                "code": -32601,
                                "message": f"Method not found: {method}"
                            }
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
