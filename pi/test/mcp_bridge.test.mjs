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

test("a desynced (unterminated >32MiB) stream is killed, not left as a zombie (F-PI-1)", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    assert.equal(spawnCount(counter), 1);

    // Flood: the in-flight call rejects with the desync error instead of
    // hanging for the full 10-minute timeout.
    await assert.rejects(() => bridge.request("tools/call", { flood: true }), /exceeded/);

    // The zombie-fix assertion: the child was killed, `exited` flipped, and
    // the NEXT request lazily respawns a healthy child. Before the fix the
    // overflow branch only cleared the buffer — alive stayed true, and every
    // subsequent call glued onto the unterminated line until reload.
    // Poll for the exit event instead of a fixed sleep: SIGTERM teardown of
    // a child mid-write can exceed any small constant on a loaded machine.
    for (let waited = 0; waited < 5000 && bridge.alive; waited += 25)
      await new Promise((r) => setTimeout(r, 25));
    assert.equal(bridge.alive, false);

    const result = await bridge.request("tools/call", { n: 2 });
    assert.equal(result?.content?.[0]?.text, "ok");
    assert.equal(spawnCount(counter), 2);
  } finally {
    bridge.stop();
  }
});

test("a healthy initialize does not leave the 30s startup deadline armed (F-PI-2)", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    await bridge.request("tools/call", {});
    // process.getActiveResourcesInfo reports what keeps the event loop
    // busy; a leaked setTimeout shows up as "Timeout". The request timeout
    // for the settled call must also be gone — only idle handles remain.
    const leak = process
      .getActiveResourcesInfo()
      .filter((r) => r === "Timeout" || r === "Immediate");
    assert.equal(leak.length, 0, `timer handles leaked after init: ${leak.length}`);
  } finally {
    bridge.stop();
  }
});
