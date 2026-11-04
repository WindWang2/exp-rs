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

test("a request timeout fires notifications/cancelled server-side", async (t) => {
  // The fake server is spawned via its shebang, which Windows cannot exec
  // (`spawn EFTYPE`, pre-existing host limitation — same canary contract as
  // mcp_bridge.test.mjs). Skip honestly instead of failing on such hosts;
  // non-spawn failures are rethrown (a real regression must fail, not skip).
  const dir0 = mkdtempSync(join(tmpdir(), "r18c-canary-"));
  const canary = new McpBridge(fakeServer, [join(dir0, "spawns")]);
  let canSpawn = true;
  try {
    await canary.request("tools/call", {});
  } catch (err) {
    const msg = String(err?.message ?? err);
    // Launch-CLASS failures only: the bridge's own "Failed to launch"
      // prefix, or the Windows shebang EFTYPE. Everything else — a child
      // that spawned then died, a missing/renamed fixture (ENOENT), an
      // EPIPE from a real regression — is rethrown: the suite must fail
      // loudly, never skip green on a broken test fixture or bridge.
      if (!/^Failed to launch|EFTYPE/i.test(msg))
      throw err;
    canSpawn = false;
    console.log(`canary: fake MCP server cannot spawn on this host: ${msg}`);
  } finally {
    canary.stop();
    rmSync(dir0, { recursive: true, force: true });
  }
  if (!canSpawn) {
    t.skip("host cannot exec the fake MCP server (pre-existing Windows limitation)");
    return;
  }

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
    // The cancel is written to stdin the moment the timeout fires, but the
    // fake server's notify-log append is a separate PROCESS doing async I/O
    // — reading immediately races it (pre-existing load-dependent flake,
    // observed on pristine master under a full parallel suite run). Poll
    // bounded for the append instead of assuming it landed.
    let cancels = [];
    for (let waited = 0; waited < 2000; waited += 25) {
      cancels = readNotifyLog(notifyLog).filter(
        (m) => m.method === "notifications/cancelled",
      );
      if (cancels.length >= 1) break;
      await new Promise((r) => setTimeout(r, 25));
    }
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
