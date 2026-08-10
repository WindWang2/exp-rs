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


def _mount_shm_array(key, width, height, bands, dtype_code):
    """ADR 0064 - attach to a POSIX shared-memory segment created by the C++
    side and mount its payload as a zero-copy numpy array (no copy).

    Layout: a 32-byte header (uuid[16] + width/height/bands/dtype int32)
    followed by the payload. Returns (shm, ndarray). The caller MUST
    shm.close() (and best-effort shm.unlink()) in a finally once done with the
    array - the buffer stays valid only while the mapping is open.

    Shared by the `shm.read` checksum probe and the `processing.execute_algorithm`
    `__shm_key__` delivery path so the mount logic has a single owner.
    """
    from multiprocessing import shared_memory
    # Must stay in sync with SharedMemorySegment::DType (shared_memory_segment.h).
    dtype_map = {0: np.float32, 1: np.uint8, 2: np.int32, 3: np.uint16, 4: np.float64}
    np_dtype = dtype_map.get(dtype_code, np.float32)
    shm = shared_memory.SharedMemory(name=key)
    # Header is 32 bytes: uuid[16] + 4*int32. Payload follows.
    arr = np.ndarray((height, width, bands), dtype=np_dtype, buffer=shm.buf, offset=32)
    return shm, arr


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
    import tempfile
    parser = argparse.ArgumentParser(description="SICNU GEO RS Python Worker Daemon")
    parser.add_argument("--socket", required=True, help="Socket name / path for IPC")
    args = parser.parse_args()

    socket_path = args.socket
    if not os.path.isabs(socket_path):
        socket_path = os.path.join(tempfile.gettempdir(), socket_path)

    # The C++ side (python_ipc_server) speaks QLocalSocket: a unix socket on
    # POSIX, a named pipe on Windows. Python can connect to the POSIX unix
    # socket directly; the Windows named-pipe transport is not implemented
    # here, so fail fast with a clear message instead of pretending an
    # AF_INET fallback works (there is no TCP listener on the C++ side).
    if not hasattr(socket, "AF_UNIX"):
        sys.stderr.write(
            "WorkerDaemon: AF_UNIX unavailable on this platform — the out-of-process "
            "Python worker is not supported here\n"
        )
        sys.exit(1)

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
                                exec_params = params.get("params", {})
                                # ADR 0064 zero-copy delivery: when the C++ side
                                # has migrated a raster input into a shared-memory
                                # segment, it sends __shm_key__ (+ width/height/
                                # bands/dtype) instead of the file path. Mount the
                                # payload as a numpy array (no copy) and expose it
                                # to the plugin as params["__shm_array__"]. Plugins
                                # that don't read __shm_array__ are unaffected.
                                # Lifetime: close+best-effort unlink before reply,
                                # consistent with the shm.read sync contract.
                                shm_handles = []
                                if isinstance(exec_params, dict) and exec_params.get("__shm_key__"):
                                    try:
                                        shm_handle, arr = _mount_shm_array(
                                            exec_params["__shm_key__"],
                                            exec_params.get("width"),
                                            exec_params.get("height"),
                                            exec_params.get("bands"),
                                            exec_params.get("dtype"),
                                        )
                                        shm_handles.append(shm_handle)
                                        exec_params = dict(exec_params)
                                        exec_params["__shm_array__"] = arr
                                    except FileNotFoundError:
                                        resp = {
                                            "jsonrpc": "2.0",
                                            "id": req_id,
                                            "error": {
                                                "code": -32603,
                                                "message": f"Shared memory segment not found: {exec_params.get('__shm_key__')}"
                                            }
                                        }
                                        shm_handles = []
                                        exec_params = None
                                # ADR 0064 tile-by-tile delivery: when the C++
                                # side split a tall raster into row-chunk tiles,
                                # __shm_tiles__ is a manifest (list of per-tile
                                # {key,width,height,bands,dtype,row} objects).
                                # Mount every tile's segment and expose the list
                                # of zero-copy arrays as __shm_tiles__, which is
                                # what the plugin's __shm_tiles__ consumer expects.
                                if isinstance(exec_params, dict) and exec_params.get("__shm_tiles__"):
                                    manifest = exec_params["__shm_tiles__"]
                                    try:
                                        tiles = []
                                        for tile in manifest:
                                            shm_handle, arr = _mount_shm_array(
                                                tile.get("key"),
                                                tile.get("width"),
                                                tile.get("height"),
                                                tile.get("bands"),
                                                tile.get("dtype"),
                                            )
                                            shm_handles.append(shm_handle)
                                            tiles.append(arr)
                                        exec_params = dict(exec_params)
                                        exec_params["__shm_tiles__"] = tiles
                                    except FileNotFoundError:
                                        resp = {
                                            "jsonrpc": "2.0",
                                            "id": req_id,
                                            "error": {
                                                "code": -32603,
                                                "message": f"Shared memory tile segment not found"
                                            }
                                        }
                                        for h in shm_handles:
                                            try:
                                                h.close()
                                                h.unlink()
                                            except Exception:
                                                pass
                                        shm_handles = []
                                        exec_params = None
                                if exec_params is not None:
                                    try:
                                        exec_result = algo_executors[algo_id](exec_params)
                                    finally:
                                        for shm_handle in shm_handles:
                                            shm_handle.close()
                                            try:
                                                shm_handle.unlink()
                                            except FileNotFoundError:
                                                pass
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
                    elif method == "processing.test_register_shm_algorithm":
                        # ADR 0064 zero-copy delivery test fixture: registers an
                        # algorithm that consumes the __shm_array__ delivered via
                        # __shm_key__ and returns its sum + shape + dtype
                        # (byte-exact zero-copy proof), or - when no array was
                        # delivered - reports that it fell back to the file-path
                        # path.
                        def _shm_sum_fn(p):
                            arr = p.get("__shm_array__")
                            tiles = p.get("__shm_tiles__")
                            if tiles is not None:
                                # Tile-by-tile zero-copy delivery: __shm_tiles__
                                # is a list of per-tile numpy arrays.
                                return {"via": "tiles",
                                        "tile_count": len(tiles),
                                        "sums": [float(t.sum()) for t in tiles],
                                        "shapes": [list(t.shape) for t in tiles]}
                            if arr is not None:
                                return {"via": "shm",
                                        "sum": float(arr.sum()),
                                        "shape": list(arr.shape),
                                        "dtype": str(arr.dtype)}
                            return {"via": "path", "input": p.get("input")}
                        iface_obj = SicnuPythonIface(s)
                        iface_obj.registerProcessingAlgorithm(
                            "py:shm_sum",
                            "SHM Sum Test",
                            execute_fn=_shm_sum_fn,
                        )
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"status": "algorithm_registered"}
                        }
                    elif method == "shm.read":
                        # ADR 0064: zero-copy shared memory read. The C++ side
                        # creates a QSharedMemory segment, writes a Header
                        # (32 bytes: uuid[16] + width/height/bands/dtype int32)
                        # followed by the payload. We attach via
                        # multiprocessing.shared_memory, skip the header, and
                        # mount the payload as a numpy array (no copy).
                        #
                        # Synchronization: access to this segment is serialized
                        # by the shm.read request/response boundary, and every
                        # segment carries a unique key, so there is never
                        # concurrent read/write on the same buffer. No lock is
                        # needed — and adding one would force a copy here,
                        # defeating the zero-copy mount.
                        #
                        # Lifetime (ADR 0064 leak fix): the POSIX shm object
                        # persists until unlinked. The C++ creator unlinks it
                        # on detach(); we also best-effort unlink here so the
                        # object is reclaimed even if the C++ side exits
                        # first. shm.unlink() is safe to call from the reader
                        # once we have closed our local mapping.
                        key = params.get("key")
                        width = params.get("width")
                        height = params.get("height")
                        bands = params.get("bands")
                        dtype_code = params.get("dtype")
                        try:
                            shm, arr = _mount_shm_array(key, width, height, bands, dtype_code)
                            try:
                                result = {
                                    "status": "ok",
                                    "checksum": float(arr.sum()),
                                    "shape": list(arr.shape),
                                }
                            finally:
                                shm.close()
                            # Best-effort reclaim: unlink the backing object
                            # now that this reader is done with it. Either the
                            # C++ owner or this reader unlinking is sufficient;
                            # both calling is harmless (ENOENT is swallowed).
                            try:
                                shm.unlink()
                            except FileNotFoundError:
                                pass
                        except FileNotFoundError:
                            result = {"status": "error", "message": f"Shared memory segment not found: {key}"}
                        resp = {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": result,
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
