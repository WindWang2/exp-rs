#!/usr/bin/env python3
# scripts/export_lab_map_mcp.py — headless cartography export for lab 11 via
# the GUI binary's --mcp mode (agent tool protocol, line-delimited JSON-RPC
# over stdio; ADR 0022).
#
# The runner pipeline (lab11_cartographic_mapping.pipeline.json) produces the
# thematic raster; this script composes the committed MapSpec document and
# exports the PNG through the governed export action:
#   cartography:compose {mapspec}   -> compiled layout
#   cartography:export {layout, format, directory, file_name, dpi}
#
# Usage:
#   QT_QPA_PLATFORM=offscreen python3 scripts/export_lab_map_mcp.py \
#       --binary build/sicnu_geo_rs \
#       --mapspec data/labs/mapspecs/lab11_thematic_map.mapspec.json \
#       --directory data/labs/_tmp/out/lab11/map
#
# The thematic raster must already be loaded into the app's project as layer
# "lab11_thematic_mask" (load it in the GUI session, or run the script inside
# a session that has the project open). Frame `layers` in the MapSpec must
# match that layer name.

import argparse
import json
import os
import subprocess
import sys
import threading


class McpClient:
    """Minimal newline-delimited JSON-RPC client for the --mcp stdio server."""

    def __init__(self, proc):
        self.proc = proc
        self.next_id = 0
        self.pending = {}
        self.notifications = []
        self.lock = threading.Lock()
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for raw in self.proc.stdout:
            line = raw.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            with self.lock:
                if "id" in msg and msg["id"] in self.pending:
                    self.pending[msg["id"]].append(msg)
                else:
                    self.notifications.append(msg)

    def call(self, method, params=None, timeout=120):
        self.next_id += 1
        rid = self.next_id
        req = {"jsonrpc": "2.0", "id": rid, "method": method}
        if params is not None:
            req["params"] = params
        with self.lock:
            self.pending[rid] = []
        self.proc.stdin.write(json.dumps(req) + "\n")
        self.proc.stdin.flush()
        import time
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self.lock:
                if self.pending.get(rid):
                    msg = self.pending[rid].pop(0)
                    if "error" in msg:
                        raise RuntimeError(f"{method} failed: {msg['error']}")
                    return msg.get("result")
            time.sleep(0.1)
        raise TimeoutError(f"{method} timed out after {timeout}s")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", required=True, help="path to the GUI binary (sicnu_geo_rs)")
    ap.add_argument("--mapspec", required=True, help="path to the MapSpec JSON")
    ap.add_argument("--directory", required=True, help="export directory")
    ap.add_argument("--dpi", type=int, default=200)
    args = ap.parse_args()

    with open(args.mapspec, "r", encoding="utf-8") as fh:
        mapspec = json.load(fh)
    os.makedirs(args.directory, exist_ok=True)

    proc = subprocess.Popen(
        [args.binary, "--mcp"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, bufsize=1,
    )
    try:
        client = McpClient(proc)
        client.call("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "lab11-export", "version": "1.0"},
        })

        # Compliance gate first: validate + preflight before touching export.
        client.call("tools/call", {"name": "cartography:validate", "arguments": {"mapspec": mapspec}})
        client.call("tools/call", {"name": "cartography:preflight", "arguments": {"mapspec": mapspec}})

        composed = client.call("tools/call", {
            "name": "cartography:compose", "arguments": {"mapspec": mapspec}})
        payload = composed.get("content", [{}])[0].get("text", "{}")
        layout = json.loads(payload).get("layout")
        if not layout:
            sys.exit("compose returned no layout id — check that layer "
                     "'lab11_thematic_mask' is loaded in the project")

        result = client.call("tools/call", {
            "name": "cartography:export",
            "arguments": {"layout": layout, "format": "png",
                          "directory": args.directory, "file_name": "lab11_thematic_map",
                          "dpi": args.dpi}},
            timeout=300)
        print(json.dumps(result, ensure_ascii=False, indent=2))
    finally:
        proc.terminate()


if __name__ == "__main__":
    main()
