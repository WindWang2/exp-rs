/**
 * R18-C C09: an id:null JSON-RPC error frame (the server's unattributable
 * parse-fault shape, e.g. a stdin line the server rejects wholesale) cannot
 * be matched to any request. The bridge must reject ALL pending requests
 * with that error — and must stay usable afterwards (lazy respawn on the
 * next request) rather than wedging.
 *
 * Run: node --test pi/test/
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { McpBridge } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");

test("an id:null error frame rejects all pending and the bridge recovers", async (t) => {
  // Host canary: the fake server runs via its shebang, which Windows cannot
  // exec (`spawn EFTYPE`, pre-existing limitation). Skip honestly there;
  // non-spawn failures are rethrown (a real regression must fail, not skip).
  {
    const canaryDir = mkdtempSync(join(tmpdir(), "r18c-idnull-canary-"));
    const probe = new McpBridge(fakeServer, [join(canaryDir, "s")]);
    let canSpawn = true;
    try {
      await probe.request("tools/call", {});
    } catch (err) {
      const msg = String(err?.message ?? err);
      if (!/EFTYPE|ENOENT|EACCES|EPIPE|Failed to launch|spawn/i.test(msg))
        throw err;
      canSpawn = false;
      console.log(`canary: fake MCP server cannot spawn on this host: ${msg}`);
    } finally {
      probe.stop();
      rmSync(canaryDir, { recursive: true, force: true });
    }
    if (!canSpawn) {
      t.skip("host cannot exec the fake MCP server (pre-existing Windows limitation)");
      return;
    }
  }
  const counter = join(mkdtempSync(join(tmpdir(), "r18c-idnull-")), "spawns");
  const bridge = new McpBridge(fakeServer, [counter]);
  try {
    // Two in-flight calls; the first triggers the id:null error frame.
    const slow = bridge.request("tools/call", {
      name: "x",
      arguments: { delay_ms: 5000 },
    });
    const doomed = bridge.request("tools/call", {
      name: "x",
      arguments: { id_null_error: true },
    });

    // Both must reject (the frame cannot be attributed to just one call).
    const [slowErr, doomedErr] = await Promise.allSettled([slow, doomed]);
    assert.equal(slowErr.status, "rejected");
    assert.equal(doomedErr.status, "rejected");
    assert.match(doomedErr.reason.message, /Parse error/);
    assert.match(slowErr.reason.message, /Parse error/);

    // The child is still alive (the fake never exited) and usable: the next
    // request resolves normally against the same generation.
    const ok = await bridge.request("tools/call", { name: "x", arguments: {} });
    assert.equal(ok?.content?.[0]?.text, "ok");
    assert.equal(bridge.alive, true);
  } finally {
    bridge.stop();
    rmSync(counter, { recursive: true, force: true });
  }
});
