"""R35-T05 worker daemon request-identity and input-bound regressions.

Deterministic oracles for four production fixes, all against the REAL
worker_daemon module (no daemon process needed — the proxy paths and the
shm mount are exercised directly):

* WP11  late proxy responses are not served to the next call (per-call
        monotonic request ids; fixed 8001/8003 used to accept stale state)
* WP13  complete lines with invalid UTF-8 are dropped, not laundered into
        U+FFFD placeholders that then pass as legal JSON paths/names
* WP14  _mount_shm_array validates the segment's OWN 32-byte header
        (dtype/dims/capacity) instead of mounting by the request alone;
        a refused mount closes its mapping handle (no fd leak)
* WP15  _json_safe refuses an oversized ndarray BEFORE materializing it
        via tolist()

Run: python3 tests/test_r35_t05_worker_daemon_identity.py
"""
import json
import os
import socket
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src", "python", "scripts"))

try:
    import numpy  # noqa: F401  (worker_daemon imports it unconditionally)
    import worker_daemon
    HAVE_NUMPY = True
except ImportError:
    worker_daemon = None
    HAVE_NUMPY = False

try:
    from multiprocessing import shared_memory
    HAVE_SHM = True
except ImportError:
    shared_memory = None
    HAVE_SHM = False


def fd_count():
    """Open-fd count for leak assertions; None where /proc is unavailable."""
    try:
        return len(os.listdir("/proc/self/fd"))
    except OSError:
        return None


class ScriptedSocket:
    """FakeSocket that can also raise (socket.timeout) from recv()."""

    def __init__(self, recv_plan):
        self._recv_plan = list(recv_plan)
        self.sent = []
        self.timeout = None

    def sendall(self, data):
        self.sent.append(data)

    def settimeout(self, value):
        self.timeout = value

    def recv(self, _n):
        item = self._recv_plan.pop(0) if self._recv_plan else b""
        if isinstance(item, BaseException):
            raise item
        return item


@unittest.skipUnless(HAVE_NUMPY, "worker_daemon requires numpy")
class ProxyRequestIdentityTest(unittest.TestCase):
    """WP11: request/response correlation across timeouts and late answers."""

    def setUp(self):
        worker_daemon._pending_proxy_buf = b""
        worker_daemon._pending_proxy_lines = []
        worker_daemon._proxy_request_seq = 8000

    @staticmethod
    def sent_ids(sock):
        return [json.loads(raw.decode("utf-8"))["id"] for raw in sock.sent]

    def test_late_response_is_not_served_to_the_next_call(self):
        # The reproduced mis-delivery: call A times out; A's response
        # arrives late (after B was sent). With the old fixed id both calls
        # were 8001 and B accepted A's stale answer. B must get B's answer
        # and A's late line must be left for the dispatch loop to drop.
        late_a = b'{"id": 8001, "result": {"generation": "stale-A"}}\n'
        for_b = b'{"id": 8002, "result": {"generation": "B"}}\n'
        sock = ScriptedSocket([socket.timeout(), late_a + for_b])
        proxy = worker_daemon.SicnuMapCanvasProxy(sock)

        self.assertEqual(proxy._get_state(), {})  # A: bounded-wait sentinel
        self.assertEqual(proxy._get_state(), {"generation": "B"})  # B: own answer
        ids = self.sent_ids(sock)
        self.assertEqual(ids, [8001, 8002])
        self.assertNotEqual(ids[0], ids[1])
        # A's late answer is preserved for the main loop (which drops pure
        # result lines) — it was consumed by nobody.
        self.assertEqual(
            worker_daemon._pending_proxy_lines,
            ['{"id": 8001, "result": {"generation": "stale-A"}}'],
        )

    def test_activeLayer_uses_per_call_ids_too(self):
        late_a = b'{"id": 8001, "result": {"layer": "stale"}}\n'
        for_b = b'{"id": 8002, "result": {"layer": "current"}}\n'
        sock = ScriptedSocket([socket.timeout(), late_a + for_b])
        iface = worker_daemon.SicnuPythonIface(sock)

        self.assertIsNone(iface.activeLayer())  # timeout sentinel stays None
        self.assertEqual(iface.activeLayer(), {"layer": "current"})
        self.assertEqual(self.sent_ids(sock), [8001, 8002])

    def test_sequential_calls_get_distinct_ids(self):
        sock = ScriptedSocket([
            b'{"id": 8001, "result": {"n": 1}}\n',
            b'{"id": 8002, "result": {"n": 2}}\n',
        ])
        proxy = worker_daemon.SicnuMapCanvasProxy(sock)
        self.assertEqual(proxy._get_state(), {"n": 1})
        self.assertEqual(proxy._get_state(), {"n": 2})
        self.assertEqual(self.sent_ids(sock), [8001, 8002])


@unittest.skipUnless(HAVE_NUMPY, "worker_daemon requires numpy")
class StrictUtf8FramingTest(unittest.TestCase):
    """WP13: complete lines must decode strictly — no U+FFFD laundering."""

    def setUp(self):
        worker_daemon._pending_proxy_buf = b""
        worker_daemon._pending_proxy_lines = []
        worker_daemon._proxy_request_seq = 8000

    def test_invalid_utf8_line_is_dropped_not_rewritten(self):
        # 0xff can never appear in legal UTF-8. The old errors="replace"
        # rewrote it to U+FFFD, the line parsed as legal JSON, and a
        # corrupted path/name was accepted as a REAL answer.
        poisoned = b'{"id": 8001, "result": {"name": "bad\xffpath"}}\n'
        healthy = b'{"id": 8001, "result": {"name": "legitimate"}}\n'
        sock = ScriptedSocket([poisoned + healthy])
        proxy = worker_daemon.SicnuMapCanvasProxy(sock)

        self.assertEqual(proxy._get_state(), {"name": "legitimate"})
        # The poisoned line was dropped entirely — it is not queued for the
        # main loop and no U+FFFD survived anywhere.
        self.assertEqual(worker_daemon._pending_proxy_lines, [])
        self.assertNotIn("\ufffd".encode("utf-8"), b"".join(sock.sent))

    def test_legal_multibyte_split_is_still_preserved(self):
        # The #649 contract this must NOT regress: a legal CJK sequence
        # split across recv boundaries stays intact (strict decoding only
        # ever runs on COMPLETE lines).
        line = '{"id": 8001, "result": {"name": "多边形分类结果"}}\n'.encode("utf-8")
        cut = line.index("类".encode("utf-8")) + 1
        sock = ScriptedSocket([line[:cut], line[cut:]])
        proxy = worker_daemon.SicnuMapCanvasProxy(sock)
        self.assertEqual(proxy._get_state(), {"name": "多边形分类结果"})


@unittest.skipUnless(HAVE_SHM, "multiprocessing.shared_memory unavailable")
@unittest.skipUnless(HAVE_NUMPY, "worker_daemon requires numpy")
class ShmMountValidationTest(unittest.TestCase):
    """WP14: the segment's own header is the mount authority."""

    HEADER = struct.Struct("=16s4i")  # uuid[16] + width/height/bands/dtype

    def _create_segment(self, width, height, bands, dtype_code, payload, extra_tail=0):
        seg = shared_memory.SharedMemory(
            create=True, size=self.HEADER.size + len(payload) + extra_tail
        )
        header = self.HEADER.pack(b"r35t05shmprobe000", width, height, bands, dtype_code)
        seg.buf[: self.HEADER.size] = header
        seg.buf[self.HEADER.size : self.HEADER.size + len(payload)] = payload
        self.addCleanup(self._release, seg)
        return seg

    @staticmethod
    def _release(seg):
        try:
            seg.close()
        finally:
            try:
                seg.unlink()
            except FileNotFoundError:
                pass

    def test_consistent_header_mounts_and_reads_values(self):
        import numpy as np

        payload = np.arange(4, dtype="<f4").tobytes()  # 2x2 float32
        seg = self._create_segment(2, 2, 1, 0, payload)
        shm, arr = worker_daemon._mount_shm_array(seg.name, 2, 2, 1, 0)
        try:
            self.assertEqual(arr.shape, (2, 2, 1))
            self.assertEqual(arr.dtype, np.float32)
            self.assertEqual(float(arr.sum()), float(np.arange(4, dtype="<f4").sum()))
        finally:
            shm.close()

    def test_requested_dims_none_fall_back_to_header(self):
        import numpy as np

        payload = np.arange(6, dtype="<f4").tobytes()  # header says 3x2x1
        seg = self._create_segment(3, 2, 1, 0, payload)
        shm, arr = worker_daemon._mount_shm_array(seg.name, None, None, None, 0)
        try:
            self.assertEqual(arr.shape, (2, 3, 1))  # (height, width, bands)
        finally:
            shm.close()

    def test_dtype_mismatch_is_refused_without_retyping(self):
        # The reproduced corruption: header said UInt8, the request said
        # Float32, and the buffer was silently read as float32.
        payload = bytes([1, 2, 3, 4, 5, 6, 7, 8])
        seg = self._create_segment(2, 2, 1, 1, payload)  # header dtype = UInt8
        baseline = fd_count()
        with self.assertRaises(ValueError) as ctx:
            worker_daemon._mount_shm_array(seg.name, 2, 2, 1, 0)  # requests Float32
        self.assertIn("dtype", str(ctx.exception))
        if baseline is not None:
            self.assertEqual(fd_count(), baseline)  # refused mount leaks no fd

    def test_dim_mismatch_is_refused(self):
        payload = bytes(16)
        seg = self._create_segment(2, 2, 1, 0, payload)
        with self.assertRaises(ValueError) as ctx:
            worker_daemon._mount_shm_array(seg.name, 4, 2, 1, 0)
        self.assertIn("width", str(ctx.exception))

    def test_short_mapping_is_refused(self):
        seg = shared_memory.SharedMemory(create=True, size=16)  # < 32-byte header
        self.addCleanup(self._release, seg)
        with self.assertRaises(ValueError):
            worker_daemon._mount_shm_array(seg.name, 2, 2, 1, 0)

    def test_capacity_mismatch_is_refused(self):
        # Header claims 4x4 float32 (96 payload bytes) but the mapping only
        # holds 8 payload bytes.
        seg = self._create_segment(4, 4, 1, 0, bytes(8))
        with self.assertRaises(ValueError) as ctx:
            worker_daemon._mount_shm_array(seg.name, 4, 4, 1, 0)
        self.assertIn("need", str(ctx.exception))


@unittest.skipUnless(HAVE_NUMPY, "worker_daemon requires numpy")
class JsonSafeBudgetTest(unittest.TestCase):
    """WP15: oversized arrays are refused before tolist() materializes."""

    def test_oversized_array_refused_before_tolist(self):
        import numpy as np

        calls = {"tolist": 0}

        class TracedArray(np.ndarray):
            def tolist(self):
                calls["tolist"] += 1
                return super().tolist()

        big = np.zeros(
            worker_daemon._JSON_REPLY_BUDGET_BYTES // worker_daemon._JSON_ELEMENT_TEXT_BYTES + 1,
            dtype=np.float32,
        ).view(TracedArray)
        with self.assertRaises(ValueError) as ctx:
            worker_daemon._json_safe(big)
        self.assertIn("reply budget", str(ctx.exception))
        self.assertEqual(calls["tolist"], 0)  # refused BEFORE materializing

    def test_small_results_still_supported(self):
        import numpy as np

        self.assertEqual(worker_daemon._json_safe(np.int64(7)), 7)
        self.assertEqual(worker_daemon._json_safe(np.arange(3)), [0, 1, 2])
        self.assertEqual(
            worker_daemon._json_safe({"a": np.float32(1.5), "b": (np.uint8(2),)}),
            {"a": 1.5, "b": [2]},
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
