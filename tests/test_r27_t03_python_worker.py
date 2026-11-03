#!/usr/bin/env python3
"""test_r27_t03_python_worker.py — R27-T3 regression guards for the Python
worker/plugin compatibility layer.

Covers, each with a real failure mode when the fix regresses:
  * #1797 (SPP-2): a plugin whose initGui registers an algorithm and THEN
    fails answers a typed -32001 load error, and the registered executor is
    rolled back — the py: id answers -32602 Unknown algorithm instead of
    executing in the reused worker (the orphan of the old flow).
  * #1796 (SPP-1): a __shm_tiles__ mount whose SECOND tile has inconsistent
    dims answers a typed -32000 refusal AND the FIRST tile's segment is
    closed+unlinked by the daemon (the old code reclaimed only on
    FileNotFoundError and clean executes).
  * SPP-3 (#1806): an unknown __shm_dtype__ code is a typed refusal, never
    a silent float32 remount.
  * #1799 (SPP-7): in a SHARED worker, plugin B's publish_to_iface
    registers only plugin B's operator — the process-global OPERATORS table
    must not re-register plugin A's entries under B.

Run: python3 tests/test_r27_t03_python_worker.py
Wired into ctest; skips loudly without numpy/AF_UNIX like its r23 sibling.
"""
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(REPO, "src", "python", "scripts"))

PLUGIN_A = '''
import exprs

@exprs.operator(id="vendorA:alpha", display_name="Alpha")
def alpha(ctx, params):
    return {"who": "A"}

class PluginA:
    def __init__(self, iface):
        self.iface = iface
    def initGui(self):
        exprs.publish_to_iface(self.iface)

def classFactory(iface):
    return PluginA(iface)
'''

PLUGIN_B = '''
import exprs

@exprs.operator(id="vendorB:beta", display_name="Beta")
def beta(ctx, params):
    return {"who": "B"}

class PluginB:
    def __init__(self, iface):
        self.iface = iface
    def initGui(self):
        exprs.publish_to_iface(self.iface)

def classFactory(iface):
    return PluginB(iface)
'''

PLUGIN_FAILING_INITGUI = '''
class FailingPlugin:
    def __init__(self, iface):
        self.iface = iface
    def initGui(self):
        self.iface.registerProcessingAlgorithm(
            "py:r27_orphan", "Orphan", execute_fn=lambda p: {"pwned": True})
        raise RuntimeError("initGui exploded (R27-T3 fixture)")

def classFactory(iface):
    return FailingPlugin(iface)
'''


def _numpy_available():
    try:
        import numpy  # noqa: F401
        return True
    except ImportError:
        return False


class DaemonHarness:
    """The REAL worker daemon on a private unix socket; reverse-direction
    host requests are answered ok and RECORDED (algorithm registrations are
    what the #1799 assertions inspect)."""

    def __init__(self):
        self.tmp = tempfile.mkdtemp(prefix="r27t03_daemon_")
        self.socket_path = os.path.join(self.tmp, "daemon.sock")
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(self.socket_path)
        self.listener.listen(4)
        self.conn = None
        self.buf = b""
        self.host_requests = []  # (method, params) the daemon proxied
        env = dict(os.environ)
        env["PYTHONPATH"] = os.pathsep.join(
            p for p in sys.path if p and os.path.isdir(p)
        )
        self.proc = subprocess.Popen(
            [sys.executable,
             os.path.join(REPO, "src", "python", "scripts", "worker_daemon.py"),
             "--socket", self.socket_path],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def connect(self, timeout=20.0):
        self.listener.settimeout(timeout)
        self.conn, _ = self.listener.accept()
        self.conn.settimeout(timeout)
        pong = self.request("ping", {})
        assert pong["result"]["status"] == "pong"

    def _read_line_raw(self):
        while b"\n" not in self.buf:
            chunk = self.conn.recv(65536)
            if not chunk:
                return None
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode("utf-8").strip() or None

    def request(self, method, params):
        req = {"jsonrpc": "2.0", "id": int(time.time() * 1000) % 1000000007,
               "method": method, "params": params}
        self.conn.sendall((json.dumps(req) + "\n").encode("utf-8"))
        deadline = time.time() + 20.0
        while time.time() < deadline:
            line = self._read_line_raw()
            if line is None:
                raise AssertionError("daemon closed the socket")
            msg = json.loads(line)
            if "method" in msg and "id" in msg:
                self.host_requests.append((msg["method"], msg.get("params", {})))
                reply = {"jsonrpc": "2.0", "id": msg["id"],
                         "result": {"status": "ok"}}
                self.conn.sendall((json.dumps(reply) + "\n").encode("utf-8"))
                continue
            if msg.get("id") == req["id"]:
                return msg
        raise AssertionError(f"timeout waiting for answer to {method}")

    def registered_algorithm_ids(self):
        ids = []
        for method, params in self.host_requests:
            if method == "processing.register_algorithm":
                ids.append(params.get("id"))
        return ids

    def alive(self):
        return self.proc.poll() is None

    def close(self):
        for stream in (self.proc.stdout, self.proc.stderr):
            try:
                if stream:
                    stream.close()
            except Exception:
                pass
        try:
            if self.conn:
                self.conn.close()
        except Exception:
            pass
        try:
            self.listener.close()
        except Exception:
            pass
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)


def _make_shm_segment(key, width, height, bands, dtype_code=0):
    """Creates a host-side segment in the daemon's expected layout:
    32-byte header (uuid[16] + width/height/bands/dtype int32) + payload."""
    from multiprocessing import shared_memory
    import numpy as np
    itemsize = {0: 4, 1: 1, 2: 4, 3: 2, 4: 8}[dtype_code]
    payload_bytes = width * height * bands * itemsize
    buf = bytearray(32 + payload_bytes)
    struct.pack_into("<iiii", buf, 16, width, height, bands, dtype_code)
    arr = np.ndarray((height, width, bands),
                     dtype={0: np.float32, 1: np.uint8, 2: np.int32,
                           3: np.uint16, 4: np.float64}[dtype_code],
                     buffer=bytes(buf), offset=32)
    shm = shared_memory.SharedMemory(name=key, create=True, size=len(buf))
    shm.buf[:] = buf
    return shm


@unittest.skipUnless(_numpy_available() and hasattr(socket, "AF_UNIX"),
                     "numpy + AF_UNIX required (POSIX worker by design)")
class R27T03PythonWorker(unittest.TestCase):
    def setUp(self):
        self.harness = DaemonHarness()
        self.harness.connect()
        self.plugin_dirs = []

    def tearDown(self):
        self.harness.close()
        import shutil
        for d in self.plugin_dirs:
            shutil.rmtree(d, ignore_errors=True)

    def _write_plugin(self, package_name, source):
        # QGIS-style package layout: a directory with __init__.py, loaded
        # with package_name = directory name (same as the r23 sibling).
        root = os.path.join(self.harness.tmp, package_name)
        os.makedirs(root, exist_ok=True)
        with open(os.path.join(root, "__init__.py"), "w") as fh:
            fh.write(source)
        self.plugin_dirs.append(os.path.dirname(root))
        return root

    def test_failing_initgui_rolls_back_registrations(self):
        plugin_file = self._write_plugin("r27_failing_plugin",
                                         PLUGIN_FAILING_INITGUI)
        resp = self.harness.request("load_plugin", {
            "plugin_dir": plugin_file,
            "package_name": "r27_failing_plugin",
        })
        self.assertIn("error", resp, resp)
        self.assertEqual(resp["error"]["code"], -32001)
        self.assertIn("failed to load", resp["error"]["message"])

        # The rollback popped the executor: the orphan py: id is UNKNOWN,
        # never executable in the reused worker.
        exec_resp = self.harness.request("processing.execute_algorithm", {
            "id": "py:r27_orphan", "params": {}})
        self.assertIn("error", exec_resp, exec_resp)
        self.assertEqual(exec_resp["error"]["code"], -32602)

        # And the daemon still serves.
        pong = self.harness.request("ping", {})
        self.assertEqual(pong["result"]["status"], "pong")

    def test_shm_tile_mount_failure_reclaims_prior_segments(self):
        from multiprocessing import shared_memory
        good = _make_shm_segment("r27t03_good_tile", 4, 4, 1)
        bad = _make_shm_segment("r27t03_bad_tile", 4, 4, 1)
        # bad tile's DECLARED dims exceed its buffer: np.ndarray(buffer=...)
        # raises ValueError (not FileNotFoundError) — the #1796 shape.
        manifest = [
            {"key": "r27t03_good_tile", "width": 4, "height": 4, "bands": 1,
             "dtype": 0},
            {"key": "r27t03_bad_tile", "width": 1 << 20, "height": 4,
             "bands": 1, "dtype": 0},
        ]
        self.harness.request("processing.test_register_algorithm", {})
        resp = self.harness.request("processing.execute_algorithm", {
            "id": "py:echo_test",
            "params": {"__shm_tiles__": manifest},
        })
        self.assertIn("error", resp, resp)
        self.assertEqual(resp["error"]["code"], -32000)

        # The daemon unlinked the segment it HAD mounted successfully — the
        # leak of the pre-fix code was exactly this handle surviving.
        self.assertFalse(os.path.exists(os.path.join("/dev/shm",
                                                     "r27t03_good_tile")))
        pong = self.harness.request("ping", {})
        self.assertEqual(pong["result"]["status"], "pong")
        good.close()
        bad.close()
        try:
            good.unlink()
        except FileNotFoundError:
            pass
        try:
            bad.unlink()
        except FileNotFoundError:
            pass

    def test_unknown_dtype_code_is_refused_not_float32(self):
        from multiprocessing import shared_memory
        seg = _make_shm_segment("r27t03_dtype", 4, 4, 1, dtype_code=0)
        self.harness.request("processing.test_register_algorithm", {})
        resp = self.harness.request("processing.execute_algorithm", {
            "id": "py:echo_test",
            "params": {"__shm_key__": "r27t03_dtype", "width": 4,
                       "height": 4, "bands": 1, "__shm_dtype__": 99},
        })
        self.assertIn("error", resp, resp)
        self.assertEqual(resp["error"]["code"], -32000)
        self.assertIn("dtype", resp["error"]["message"])
        seg.close()
        try:
            seg.unlink()
        except FileNotFoundError:
            pass

    def test_publish_to_iface_is_per_plugin_scoped(self):
        file_a = self._write_plugin("r27_plugin_a", PLUGIN_A)
        file_b = self._write_plugin("r27_plugin_b", PLUGIN_B)
        load_a = self.harness.request("load_plugin", {
            "plugin_dir": file_a, "package_name": "r27_plugin_a"})
        self.assertIn("result", load_a, load_a)
        load_b = self.harness.request("load_plugin", {
            "plugin_dir": file_b, "package_name": "r27_plugin_b"})
        self.assertIn("result", load_b, load_b)

        registered = self.harness.registered_algorithm_ids()
        # Each publish registered exactly its own operator; the old
        # process-global publish re-registered A's entry during B's load.
        self.assertEqual(registered.count("vendorA:alpha"), 1, registered)
        self.assertEqual(registered.count("vendorB:beta"), 1, registered)
        self.assertNotIn("vendorB:beta",
                         registered[:registered.index("vendorA:alpha") + 1]
                         if "vendorA:alpha" in registered else registered)


if __name__ == "__main__":
    unittest.main(verbosity=2)
