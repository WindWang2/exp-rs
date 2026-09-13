#!/usr/bin/env python3
"""check_mcp_stdio.py - one MCP discovery round-trip over stdio (goal D7/F).

Drives `sicnu_geo_rs --mcp` with initialize -> notifications/initialized ->
tools/list (newline-delimited JSON-RPC) and asserts the responses name
exp-rs-mcp and carry a tools array. Windows twin: scripts/windows/check_mcp.cmd
(check_mcp_stdio.ps1). Fully local - no network.

usage: scripts/check_mcp_stdio.py [--exe path] [--timeout seconds]
"""
import argparse
import json
import subprocess
import sys
import threading


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="build-dev/sicnu_geo_rs")
    ap.add_argument("--timeout", type=int, default=90)
    args = ap.parse_args()

    env = {"QT_QPA_PLATFORM": "offscreen", "SICNU_OFFLINE": "1", "PATH": __import__("os").environ["PATH"]}
    p = subprocess.Popen([args.exe, "--mcp"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                         text=True, bufsize=1, env=env)
    for msg in (
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
            "protocolVersion": "2024-11-05", "capabilities": {},
            "clientInfo": {"name": "check_mcp", "version": "1.0"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
    ):
        p.stdin.write(json.dumps(msg) + "\n")
    p.stdin.flush()

    responses = {}

    def reader():
        for line in p.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except Exception:
                continue
            if msg.get("id") in (1, 2):
                responses[msg["id"]] = msg
            if 1 in responses and 2 in responses:
                return

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    deadline = threading.Event(); t.join(args.timeout)

    ok = False
    if 1 in responses and 2 in responses:
        try:
            ok = (responses[1]["result"]["serverInfo"]["name"] == "exp-rs-mcp"
                  and isinstance(responses[2]["result"]["tools"], list))
            count = len(responses[2]["result"]["tools"])
        except Exception:
            ok = False
    if not ok:
        print(f"MCP STDIO CHECK FAIL {args.exe}")
        p.terminate()
        return 1
    p.terminate()
    p.wait(10)
    print(f"MCP STDIO CHECK PASS {args.exe} (initialize ok, tools/list ok, "
          f"{count} tools)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
