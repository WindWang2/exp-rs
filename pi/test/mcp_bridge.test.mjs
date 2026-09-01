/**
 * Bridge lifecycle regression tests (#669): a child crash must trigger
 * exactly ONE respawn — never the unbounded fork loop — and explicit
 * stop() must keep hard-rejecting requests.
 *
 * Run: node --test pi/test/
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { McpBridge } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");

function makeCounter() {
  return join(mkdtempSync(join(tmpdir(), "mcp-bridge-test-")), "spawns");
}

function spawnCount(counterPath) {
  return readFileSync(counterPath, "utf8").trim().split("\n").filter(Boolean).length;
}

function makeBridge(counterPath) {
  return new McpBridge(fakeServer, [counterPath]);
}

test("first request initializes against a healthy child", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    const result = await bridge.request("tools/call", { name: "x" });
    assert.equal(result?.content?.[0]?.text, "ok");
    assert.equal(spawnCount(counter), 1);
    assert.equal(bridge.alive, true);
  } finally {
    bridge.stop();
  }
});

test("a child crash respawns exactly once; concurrent requests share the start", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    // Normal call works against spawn 1.
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    assert.equal(spawnCount(counter), 1);

    // Crash the child mid-run (no reply): the request rejects via the
    // exit handler.
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);

    // Two concurrent requests after the crash: exactly ONE respawn,
    // both succeed. Before the #669 fix this forked children in an
    // unbounded microtask loop.
    const [r1, r2] = await Promise.all([
      bridge.request("tools/call", { n: 1 }),
      bridge.request("tools/call", { n: 2 }),
    ]);
    assert.equal(r1?.content?.[0]?.text, "ok");
    assert.equal(r2?.content?.[0]?.text, "ok");
    assert.equal(spawnCount(counter), 2);

    // Steady state: no further spawns.
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    assert.equal(spawnCount(counter), 2);
  } finally {
    bridge.stop();
  }
});

test("explicit stop() is never resurrected by a request", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  await bridge.request("tools/call", {});
  bridge.stop();
  await assert.rejects(() => bridge.request("tools/call", {}), /stopped/);
  await new Promise((r) => setTimeout(r, 100));
  assert.equal(spawnCount(counter), 1);
});

test("idempotent start() does not double-spawn a healthy bridge", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    await Promise.all([bridge.start(), bridge.start(), bridge.start()]);
    assert.equal(spawnCount(counter), 1);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
  } finally {
    bridge.stop();
  }
});

test("start() after stop() is rejected", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  bridge.stop();
  await assert.rejects(() => bridge.start(), /stopped/);
  rmSync(dirname(counter), { recursive: true, force: true });
});
