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
                            "id": req_id
                        }
                        s.sendall((json.dumps(req_msg) + "\n").encode("utf-8"))
                        continue
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
                    elif method == "crash_test":
                        sys.stderr.write("WorkerDaemon simulating segfault crash!\n")
                        sys.stderr.flush()
                        os._exit(139)
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
