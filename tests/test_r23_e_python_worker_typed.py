#!/usr/bin/env python3
"""test_r23_e_python_worker_typed.py — R23-E WP2 regression guards for the
#1662 sweep fixes in the optional Python worker compat layer (#1608/#1609,
commit 58b935f7 area) plus the daemon-side typed-error contracts.

Run: python3 tests/test_r23_e_python_worker_typed.py
Also wired into ctest (skips loudly when numpy or AF_UNIX is unavailable —
the daemon imports numpy at module top and speaks a POSIX unix socket by
design; a Windows named-pipe transport does not exist).

Guards, each with a real failure mode if the fix regresses:
  * _json_safe coercion (#1608): numpy scalars/ndarrays/nested containers in
    plugin returns serialize as plain JSON instead of TypeError → the pre-fix
    dispatch loop answered -32700 "parse error" for a VALID request.
  * unencodable returns (#1608): answer typed -32000 for that algorithm, not
    -32700, and the daemon keeps serving.
  * menu-callback cleanup on unload (#1609): before the fix, callbacks kept
    the only strong reference to the action — ui.on_action_triggered still
    FIRED it after unload_plugin (marker file present). After the fix the
    callback is popped and the trigger never runs.
  * executor cleanup on unload (#755/#1609 sibling): a stale py: id answers
    -32602 Unknown algorithm instead of executing in the reused worker.
  * bogus __shm_key__ segment: typed -32603 refusal (daemon leg of #1556).
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "src", "python", "scripts"))

REPO = os.path.abspath(os.path.join(HERE, ".."))

FIXTURE_INIT = '''
import os

_MARKER_DIR = os.environ.get("R23E_FIXTURE_MARKER_DIR", "/tmp")


class FakeAction:
    def __init__(self):
        self.title = "R23E Action"

    def text(self):
        return "R23E Action"

    def trigger(self):
        # #1609 discriminator: the daemon must NEVER reach this after
        # unload_plugin popped the callback registry entry.
        with open(os.path.join(_MARKER_DIR, "r23e_triggered.txt"), "w") as fh:
            fh.write("triggered")


class FixturePlugin:
    def __init__(self, cb_id):
        self.cb_id = cb_id


def classFactory(iface):
    action = FakeAction()
    cb_id = "cb_%d" % id(action)
    with open(os.path.join(_MARKER_DIR, "r23e_cb_id.txt"), "w") as fh:
        fh.write(cb_id)
    iface.addPluginToMenu("R23E Menu", action)

    def np_scalar(params):
        import numpy as np
        return {"count": np.int64(7), "mean": np.float32(2.5)}

    def ndarray_result(params):
        import numpy as np
        return {"matrix": np.arange(4).reshape(2, 2)}

    def unencodable(params):
        return object()

    iface.registerProcessingAlgorithm("py:r23e_np_scalar", "np scalar",
                                      execute_fn=np_scalar)
    iface.registerProcessingAlgorithm("py:r23e_ndarray", "ndarray",
                                      execute_fn=ndarray_result)
    iface.registerProcessingAlgorithm("py:r23e_unencodable", "unencodable",
                                      execute_fn=unencodable)
    return FixturePlugin(cb_id)
'''


def _numpy_available():
    try:
        import numpy  # noqa: F401
        return True
    except ImportError:
        return False


class DaemonHarness:
    """Spawns the REAL worker daemon against a private unix socket and
    emulates the host side: every reverse-direction request (menu/algorithm
    registration proxies) gets an immediate ok reply so bounded proxy waits
    never stall the load."""

    def __init__(self, marker_dir):
        self.marker_dir = marker_dir
        self.tmp = tempfile.mkdtemp(prefix="r23e_daemon_")
        self.socket_path = os.path.join(self.tmp, "daemon.sock")
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(self.socket_path)
        self.listener.listen(4)
        self.conn = None
        self.buf = b""
        env = dict(os.environ)
        # Propagate THIS interpreter's import path (numpy may live in a
        # nonstandard site dir on dev hosts) to the daemon child.
        env["PYTHONPATH"] = os.pathsep.join(
            p for p in sys.path if p and os.path.isdir(p)
        )
        env["R23E_FIXTURE_MARKER_DIR"] = marker_dir
        self.proc = subprocess.Popen(
            [sys.executable, os.path.join(REPO, "src", "python", "scripts",
                                          "worker_daemon.py"),
             "--socket", self.socket_path],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def connect(self, timeout=15.0):
        self.listener.settimeout(timeout)
        self.conn, _ = self.listener.accept()
        self.conn.settimeout(timeout)
        # Confirm the daemon lives before the fixture load.
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
        # Reverse-direction proxy requests may interleave before the answer;
        # reply ok to each and keep the request id for the matcher.
        deadline = time.time() + 15.0
        while time.time() < deadline:
            line = self._read_line_raw()
            if line is None:
                raise AssertionError("daemon closed the socket")
            msg = json.loads(line)
            if "method" in msg and "id" in msg:
                reply = {"jsonrpc": "2.0", "id": msg["id"],
                         "result": {"status": "ok"}}
                self.conn.sendall((json.dumps(reply) + "\n").encode("utf-8"))
                continue
            if msg.get("id") == req["id"]:
                return msg
        raise AssertionError(f"timeout waiting for answer to {method}")

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
        # The daemon socket dir is ours alone — clean it (review R2 N3: the
        # suite used to leak 3 temp dirs per test on every run).
        shutil.rmtree(self.tmp, ignore_errors=True)


@unittest.skipUnless(_numpy_available(),
                     "numpy unavailable — the worker daemon imports numpy at "
                     "module top; install numpy to run the real-daemon legs")
@unittest.skipUnless(hasattr(socket, "AF_UNIX"),
                     "AF_UNIX unavailable — the daemon transport is "
                     "POSIX-only by design (Windows named pipe not implemented)")
class PythonWorkerTypedContracts(unittest.TestCase):

    def setUp(self):
        import worker_daemon  # noqa: F401  (proves the module imports)
        self.marker_dir = tempfile.mkdtemp(prefix="r23e_markers_")
        self.addCleanup(shutil.rmtree, self.marker_dir, ignore_errors=True)
        plugin_root = tempfile.mkdtemp(prefix="r23e_plugin_")
        self.addCleanup(shutil.rmtree, plugin_root, ignore_errors=True)
        plugin_dir = os.path.join(plugin_root, "r23e_fixture_plugin")
        os.makedirs(plugin_dir)
        with open(os.path.join(plugin_dir, "__init__.py"), "w") as fh:
            fh.write(FIXTURE_INIT)
        self.plugin_dir = plugin_dir
        self.harness = DaemonHarness(self.marker_dir)
        self.addCleanup(self.harness.close)
        self.harness.connect()
        reply = self.harness.request(
            "load_plugin", {"plugin_dir": plugin_dir,
                            "package_name": "r23e_fixture_plugin"})
        self.assertEqual(reply.get("result", {}).get("status"), "loaded",
                         msg=json.dumps(reply))
        with open(os.path.join(self.marker_dir, "r23e_cb_id.txt")) as fh:
            self.cb_id = fh.read().strip()

    def marker(self, name):
        return os.path.join(self.marker_dir, name)

    # ---- #1608: numpy coercion ------------------------------------------

    def test_json_safe_coerces_numpy_values(self):
        import numpy as np
        import worker_daemon
        js = worker_daemon._json_safe
        out = js(np.int64(7))
        self.assertIsInstance(out, int)
        self.assertEqual(out, 7)
        self.assertIsInstance(js(np.float32(2.5)), float)
        self.assertEqual(js(np.arange(4)), [0, 1, 2, 3])
        nested = js({"a": np.float64(1.0), "b": (np.int32(2), [np.bool_(True)])})
        self.assertEqual(nested, {"a": 1.0, "b": [2, [True]]})
        self.assertNotIsInstance(nested["a"], np.generic)
        # numpy dict keys hit the same wall — coerced too
        keyed = js({np.int64(3): "x"})
        self.assertEqual(keyed, {3: "x"})
        # pass-through: json.dumps still decides (typed -32000 downstream)
        sentinel = object()
        self.assertIs(js(sentinel), sentinel)

    def test_execute_numpy_returns_serialize_as_plain_json(self):
        reply = self.harness.request(
            "processing.execute_algorithm",
            {"id": "py:r23e_np_scalar", "params": {}})
        result = reply.get("result", {}).get("result")
        self.assertEqual(result, {"count": 7, "mean": 2.5},
                         msg=json.dumps(reply))
        reply = self.harness.request(
            "processing.execute_algorithm",
            {"id": "py:r23e_ndarray", "params": {}})
        matrix = reply["result"]["result"]["matrix"]
        self.assertEqual(matrix, [[0, 1], [2, 3]])
        self.assertIsInstance(matrix[0][0], int)  # plain JSON, not a numpy echo

    def test_unencodable_return_is_a_typed_algorithm_failure(self):
        reply = self.harness.request(
            "processing.execute_algorithm",
            {"id": "py:r23e_unencodable", "params": {}})
        error = reply.get("error", {})
        self.assertEqual(error.get("code"), -32000, msg=json.dumps(reply))
        self.assertIn("non-JSON-serializable", error.get("message", ""))
        # The daemon keeps serving (the pre-fix escape was -32700 from the
        # dispatch loop's own encoder plus a lost request).
        pong = self.harness.request("ping", {})
        self.assertEqual(pong["result"]["status"], "pong")

    # ---- #1609: unload cleanup -------------------------------------------

    def test_menu_callback_fires_before_unload_and_is_gone_after(self):
        trigger_marker = self.marker("r23e_triggered.txt")
        if os.path.exists(trigger_marker):
            os.remove(trigger_marker)
        # Positive path: the registered callback still routes.
        reply = self.harness.request(
            "ui.on_action_triggered", {"callback_id": self.cb_id})
        self.assertEqual(reply["result"]["status"], "action_executed")
        self.assertTrue(os.path.exists(trigger_marker),
                        "pre-unload trigger must run the action")

        os.remove(trigger_marker)
        reply = self.harness.request(
            "unload_plugin", {"package_name": "r23e_fixture_plugin"})
        self.assertEqual(reply["result"]["status"], "unloaded")

        # #1609: after unload the callback registry entry must be gone, so
        # the trigger can never fire into the unloaded plugin again (and the
        # action object is no longer pinned by `callbacks`).
        reply = self.harness.request(
            "ui.on_action_triggered", {"callback_id": self.cb_id})
        self.assertEqual(reply["result"]["status"], "action_executed")  # protocol ack
        self.assertFalse(
            os.path.exists(trigger_marker),
            "ui.on_action_triggered after unload_plugin must NOT reach the "
            "unloaded plugin's action (#1609: callbacks popped on unload)")

        # Executor cleanup sibling (#755): stale py: id is a typed refusal.
        reply = self.harness.request(
            "processing.execute_algorithm",
            {"id": "py:r23e_np_scalar", "params": {}})
        self.assertEqual(reply.get("error", {}).get("code"), -32602,
                         msg=json.dumps(reply))
        self.assertTrue(self.harness.alive())

    # ---- daemon-side typed refusals (#1556 daemon leg) --------------------

    def test_bogus_shm_key_is_a_typed_refusal(self):
        reply = self.harness.request(
            "processing.execute_algorithm",
            {"id": "py:r23e_np_scalar",
             "params": {"__shm_key__": "r23e_no_such_segment",
                        "__shm_width__": 2, "__shm_height__": 2,
                        "__shm_bands__": 1, "__shm_dtype__": "float32"}})
        error = reply.get("error", {})
        self.assertEqual(error.get("code"), -32603, msg=json.dumps(reply))
        self.assertIn("Shared memory segment not found", error.get("message", ""))
        self.assertTrue(self.harness.alive())


if __name__ == "__main__":
    unittest.main(verbosity=2)
