/**
 * R18-C C05: a per-request timeout must stop SERVER-side work too.
 *
 * The abort path sends notifications/cancelled (#623/#645) so an aborted
 * tools/call stops burning CPU server-side. The timeout path used to only
 * delete the pending entry and reject — the asymmetry meant a timed-out
 * long execution kept running. This test pins: (1) the rejection message,
 * (2) exactly one notifications/cancelled for the timed-out rpc id, and
 * (3) the bridge stays healthy after the timeout (next ping answers).
 *
 * Run: node --test pi/test/
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { McpBridge } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");

function readNotifyLog(path) {
  if (!existsSync(path)) return [];
  return readFileSync(path, "utf8")
    .split("\n")
    .filter(Boolean)
    .map((line) => JSON.parse(line));
}

test("a request timeout fires notifications/cancelled server-side", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r18c-timeout-"));
  const counter = join(dir, "spawns");
  const notifyLog = join(dir, "notify.log");
  const bridge = new McpBridge(fakeServer, ["--hang-tool-calls", "--notify-log", notifyLog, counter], {
    requestTimeoutMs: 250,
  });
  try {
    // The first request lazily runs the handshake (initialize consumes rpc
    // id 1); the hung tools/call below is rpc id 2.
    await assert.rejects(
      bridge.request("tools/call", { name: "rs:slow", arguments: {} }),
      /MCP request timed out: tools\/call/,
    );

    // The server must have received exactly one cancel for rpc id 2.
    const cancels = readNotifyLog(notifyLog).filter(
      (m) => m.method === "notifications/cancelled",
    );
    assert.equal(cancels.length, 1, `expected one cancel, got ${JSON.stringify(readNotifyLog(notifyLog))}`);
    assert.equal(cancels[0].params?.requestId, 2);

    // The bridge is still healthy: a non-hung method answers promptly.
    const pong = await bridge.request("ping", {});
    assert.ok(pong);
    assert.equal(bridge.alive, true);
  } finally {
    bridge.stop();
    rmSync(dir, { recursive: true, force: true });
  }
});
