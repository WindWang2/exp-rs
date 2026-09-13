/**
 * Compiler 10.0 drift guard (review F-PI-1 / F-PI-2) — the TS bridge must
 * stay ONE implementation.
 *
 * Static floor: the extension shell (exp-rs-spatial.ts) imports the shared
 * McpBridge and never re-declares the transport (class, constants, teardown
 * hook). A second copy is exactly what let the startup-deadline fix drift
 * one way (F-PI-2) and the zombie-overflow behavior persist (F-PI-1).
 *
 * Behavioral floor: a runaway >32 MiB line kills the child — the next
 * request lazy-respawns onto a fresh process and SUCCEEDS (before the fix
 * the bridge zombied and every later call timed out).
 *
 * Run: node --test pi/test/
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { McpBridge } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");
const bridgeSource = readFileSync(join(here, "..", "mcp_bridge.ts"), "utf8");
const extensionSource = readFileSync(join(here, "..", "exp-rs-spatial.ts"), "utf8");

test("the extension shell imports the shared bridge (no second implementation)", () => {
  assert.match(extensionSource, /from "\.\/mcp_bridge\.ts"/);
  // The transport must live in exactly one file: none of these may be
  // re-declared in the extension shell.
  assert.doesNotMatch(extensionSource, /class\s+McpBridge/);
  assert.doesNotMatch(extensionSource, /MAX_LINE_BUFFER_CHARS\s*=/);
  assert.doesNotMatch(extensionSource, /STARTUP_TIMEOUT_MS\s*=/);
  assert.doesNotMatch(extensionSource, /REQUEST_TIMEOUT_MS\s*=/);
  assert.doesNotMatch(extensionSource, /new Set<McpBridge>/);
  assert.doesNotMatch(extensionSource, /function installExitHook/);
});

test("the shared bridge kills the child on a runaway line (F-PI-1 behavior anchor)", () => {
  // The overflow branch must tear the child down instead of zombie-streaming.
  const overflowBranch = bridgeSource.slice(
    bridgeSource.indexOf("MAX_LINE_BUFFER_CHARS) {"),
    bridgeSource.indexOf("private onLine"),
  );
  assert.ok(overflowBranch.length > 0, "overflow branch not found");
  assert.match(overflowBranch, /child\.kill\(\)/);
  assert.match(overflowBranch, /this\.exited = true/);
});

test("the shared bridge clears the startup deadline on every settle path (F-PI-2)", () => {
  assert.match(bridgeSource, /cancelStartupDeadline/);
  // The fix shape is positional: `await Promise.race` inside a try whose
  // finally calls cancelStartupDeadline(). A loose regex would match any
  // earlier finally in the file.
  const raceAt = bridgeSource.indexOf("await Promise.race");
  assert.ok(raceAt >= 0, "initialize race not found");
  const tail = bridgeSource.slice(raceAt);
  const finallyAt = tail.indexOf("} finally {");
  const cancelAt = tail.indexOf("cancelStartupDeadline();");
  assert.ok(finallyAt >= 0, "startup try/finally missing");
  assert.ok(cancelAt > finallyAt, "cancelStartupDeadline not in the startup finally");
});

test("a >32 MiB unbounded line kills the child; the next request recovers", async () => {
  const { mkdtempSync } = await import("node:fs");
  const { tmpdir } = await import("node:os");
  const counter = join(mkdtempSync(join(tmpdir(), "pi-drift-")), "spawns");
  const bridge = new McpBridge(fakeServer, [counter]);
  try {
    // Healthy call first: spawn 1.
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    // Flood: the runaway line rejects the pending request…
    await assert.rejects(() => bridge.request("tools/call", { flood: true }), /response line exceeded/);
    // …and the child is dead (killed by the overflow branch).
    assert.equal(bridge.alive, false);
    // The next request lazy-respawns and SUCCEEDS — before the fix the
    // bridge zombied and this call timed out against the desynced stream.
    const recovered = await bridge.request("tools/call", { after: "flood" });
    assert.equal(recovered?.content?.[0]?.text, "ok");
    assert.equal(bridge.alive, true);
  } finally {
    bridge.stop();
  }
});
