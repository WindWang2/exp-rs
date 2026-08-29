"""Standalone regression tests for the worker_daemon proxy drain (#649).

Run: python tests/test_worker_daemon_proxy.py
Exits non-zero on failure. Not wired into ctest (pure Python; the daemon is
importable without starting it thanks to the __main__ guard).
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src", "python", "scripts"))

import worker_daemon  # noqa: E402


class FakeSocket:
    """Replays scripted recv() payloads and records sendall() calls."""

    def __init__(self, responses):
        self._responses = list(responses)
        self.sent = []
        self.recv_calls = 0

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, _n):
        self.recv_calls += 1
        return self._responses.pop(0) if self._responses else b""


class ProxyDrainTest(unittest.TestCase):
    def setUp(self):
        worker_daemon._pending_proxy_buf = b""
        worker_daemon._pending_proxy_lines = []

    def test_stale_queued_line_does_not_spin(self):
        # A queued line whose id never matches must not rotate forever: the
        # drain must fall through to recv() (the old pop+append loop spun).
        worker_daemon._pending_proxy_lines = ['{"id": 9999, "result": {}}']
        sock = FakeSocket([b'{"id": 8001, "result": {"extent": [1, 2, 3, 4]}}\n'])
        proxy = worker_daemon.SicnuMapCanvasProxy(sock)
        self.assertEqual(proxy._get_state(), {"extent": [1, 2, 3, 4]})
        self.assertEqual(sock.recv_calls, 1)
        # The stale line is preserved for the main dispatch loop, in order.
        self.assertEqual(worker_daemon._pending_proxy_lines, ['{"id": 9999, "result": {}}'])

    def test_multibyte_split_across_recv_survives(self):
        # A CJK value split mid-sequence across recv boundaries must not
        # become U+FFFD: framing is byte-accurate end-to-end.
        line = '{"id": 8001, "result": {"name": "多边形分类结果"}}\n'.encode("utf-8")
        cut = line.index("类".encode("utf-8")) + 1  # split inside a character
        sock = FakeSocket([line[:cut], line[cut:]])
        proxy = worker_daemon.SicnuMapCanvasProxy(sock)
        self.assertEqual(proxy._get_state(), {"name": "多边形分类结果"})

    def test_carryover_keeps_bytes_for_next_call(self):
        # An incomplete tail carries over as raw bytes; a subsequent call
        # finishes the line and returns the undamaged payload.
        first = b'{"id": 8001, "result": {"extent": [0, 0, 1, 1]}}\n'
        tail_of_second = '{"id": 8001, "result": {"name": "多'.encode("utf-8")
        rest = '边形"}}\n'.encode("utf-8")
        sock = FakeSocket([first, tail_of_second, rest])
        proxy = worker_daemon.SicnuMapCanvasProxy(sock)
        self.assertEqual(proxy._get_state(), {"extent": [0, 0, 1, 1]})
        self.assertEqual(proxy._get_state(), {"name": "多边形"})
        self.assertIsInstance(worker_daemon._pending_proxy_buf, (bytes, bytearray))


if __name__ == "__main__":
    unittest.main(verbosity=2)
