/**
 * R25-C generation-drift & typed-error contract for the Pi MCP bridge.
 *
 * W1 of the R25-C track: a bridge that survives child crashes must also be
 * HONEST about what changed across generations. Three contract legs:
 *
 * 1. The initialize handshake's serverInfo/protocolVersion are captured and
 *    exposed (a respawn onto a different binary version is observable).
 * 2. Every successful (re)spawn fires an onGeneration callback — generation
 *    number, current AND previous serverInfo — so the extension shell can
 *    re-list the tool catalog and report drift (stale Pi registrations are
 *    otherwise undetectable: the bridge lazily respawns but the shell's
 *    registerTool set is from generation 1 forever).
 * 3. Bridge-level failures carry a typed `bridgeError` code + `retryable`
 *    flag (tool-level errors got errorCode/errorCategory in #645; the
 *    bridge's own child-exit/timeout/desync/restart failures stayed
 *    prose-only, forcing Pi to parse English to classify retryability).
 *
 * Run: node --test pi/test/r25_c_generation_drift.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { McpBridge } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");

function makeDir() {
  return mkdtempSync(join(tmpdir(), "r25c-drift-"));
}
function spawnCount(counterPath) {
  return readFileSync(counterPath, "utf8").trim().split("\n").filter(Boolean).length;
}

/// Host canary — same launch-class rule as mcp_bridge.test.mjs: only
/// "Failed to launch"/EFTYPE failures mean "cannot exec here"; anything
/// else is a real regression and must fail loudly, never skip green.
let spawnWorks = null;
async function canarySpawns() {
  if (spawnWorks !== null) return spawnWorks;
  const dir = makeDir();
  const probe = new McpBridge(fakeServer, [join(dir, "s")]);
  try {
    await probe.request("tools/call", {});
    spawnWorks = true;
  } catch (err) {
    const msg = String(err?.message ?? err);
    if (!/^Failed to launch|EFTYPE/i.test(msg)) throw err;
    spawnWorks = false;
    console.log(`canary: fake MCP server cannot spawn on this host: ${msg}`);
  } finally {
    probe.stop();
    rmSync(dir, { recursive: true, force: true });
  }
  return spawnWorks;
}
const canSpawn = await canarySpawns();
const kSkipReason = "host cannot exec the fake MCP server (pre-existing Windows limitation)";
const testSpawn = (name, fn) =>
  test(name, { skip: canSpawn ? false : kSkipReason }, fn);

testSpawn("handshake serverInfo/protocolVersion are captured and exposed", async () => {
  const dir = makeDir();
  const versionFile = join(dir, "version");
  writeFileSync(versionFile, "1.0");
  const bridge = new McpBridge(fakeServer, ["--version-file", versionFile, join(dir, "spawns")]);
  try {
    await bridge.request("tools/call", {});
    assert.deepEqual(bridge.serverInfo, { name: "fake", version: "1.0" });
    assert.equal(bridge.protocolVersion, "2024-11-05");
    assert.equal(bridge.generations, 1);
  } finally {
    bridge.stop();
    rmSync(dir, { recursive: true, force: true });
  }
});

testSpawn("onGeneration fires once per successful handshake with prev/current serverInfo", async () => {
  const dir = makeDir();
  const versionFile = join(dir, "version");
  writeFileSync(versionFile, "1.0");
  const events = [];
  const bridge = new McpBridge(fakeServer, ["--version-file", versionFile, join(dir, "spawns")], {
    onGeneration: (info) => events.push(structuredClone(info)),
  });
  try {
    await bridge.request("tools/call", {});
    assert.equal(events.length, 1);
    assert.equal(events[0].generation, 1);
    assert.equal(events[0].serverInfo?.version, "1.0");
    assert.equal(events[0].previousServerInfo, null);

    // Crash → lazy respawn → generation 2 with a CHANGED version: the drift
    // must be observable through both the callback and the getter.
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    writeFileSync(versionFile, "2.0");
    await bridge.request("tools/call", {});
    assert.equal(bridge.generations, 2);
    assert.equal(bridge.serverInfo?.version, "2.0");
    assert.equal(events.length, 2);
    assert.equal(events[1].generation, 2);
    assert.equal(events[1].serverInfo?.version, "2.0");
    assert.equal(events[1].previousServerInfo?.version, "1.0");
  } finally {
    bridge.stop();
    rmSync(dir, { recursive: true, force: true });
  }
});

testSpawn("tool-catalog drift across a respawn is detectable via a re-list", async () => {
  const dir = makeDir();
  const toolsFile = join(dir, "tools.json");
  const catalog = (names) =>
    names.map((n) => ({ name: n, description: `fake ${n}` }));
  writeFileSync(toolsFile, JSON.stringify(catalog(["meta:one", "meta:two"])));
  const bridge = new McpBridge(fakeServer, ["--tools-file", toolsFile, join(dir, "spawns")]);
  const names = async () =>
    (await bridge.request("tools/list", {})).tools.map((t) => t.name).sort();
  try {
    assert.deepEqual(await names(), ["meta:one", "meta:two"]);

    // Crash; the NEXT generation serves a drifted catalog. A shell that
    // re-lists on onGeneration sees the change — that is the contract:
    // drift must be DETECTABLE, not silently served stale.
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    writeFileSync(toolsFile, JSON.stringify(catalog(["meta:two", "meta:three"])));
    await bridge.request("tools/call", {});
    assert.deepEqual(await names(), ["meta:three", "meta:two"]);
  } finally {
    bridge.stop();
    rmSync(dir, { recursive: true, force: true });
  }
});

testSpawn("bridge-level failures carry typed bridgeError codes", async () => {
  const dir = makeDir();
  const counter = join(dir, "spawns");
  const poison = join(dir, "poison");

  // CHILD_EXITED: crash mid-run → typed + retryable (lazy respawn exists).
  {
    const bridge = new McpBridge(fakeServer, [counter]);
    try {
      await bridge.request("tools/call", {});
      const err = await bridge.request("tools/call", { crash: true }).then(
        () => null,
        (e) => e,
      );
      assert.ok(err, "crash must reject");
      assert.equal(err.bridgeError, "CHILD_EXITED");
      assert.equal(err.retryable, true);
      // Prose contract unchanged (#669-era tests match on it).
      assert.match(err.message, /exited/);
    } finally {
      bridge.stop();
    }
  }

  // REQUEST_TIMEOUT: hang tool calls, short client timeout → typed +
  // retryable (server-side cancel already fires, R18-C C05). The warm-up
  // uses ping — with --hang-tool-calls EVERY tools/call hangs, including a
  // warm-up one.
  {
    const bridge = new McpBridge(fakeServer, ["--hang-tool-calls", counter], {
      requestTimeoutMs: 400,
    });
    try {
      await bridge.request("ping", {});
      const err = await bridge.request("tools/call", { name: "hang" }).then(
        () => null,
        (e) => e,
      );
      assert.ok(err, "hang must time out");
      assert.equal(err.bridgeError, "REQUEST_TIMEOUT");
      assert.equal(err.retryable, true);
    } finally {
      bridge.stop();
    }
  }

  // STREAM_DESYNC: >32 MiB runaway line → typed (bridge self-heals).
  {
    const bridge = new McpBridge(fakeServer, [counter]);
    try {
      await bridge.request("tools/call", {});
      const err = await bridge.request("tools/call", { flood: true }).then(
        () => null,
        (e) => e,
      );
      assert.ok(err, "flood must reject");
      assert.equal(err.bridgeError, "STREAM_DESYNC");
      for (let w = 0; w < 5000 && bridge.alive; w += 25)
        await new Promise((r) => setTimeout(r, 25));
      // Post-desync recovery still works (F-PI-1 behavior unchanged).
      assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    } finally {
      bridge.stop();
    }
  }

  // PAYLOAD_TOO_LARGE: >4 MiB outbound line refused BEFORE the write →
  // typed, NOT retryable as-is (the caller must split or use a file path).
  {
    const bridge = new McpBridge(fakeServer, [counter]);
    try {
      await bridge.request("tools/call", {});
      const err = await bridge
        .request("tools/call", { pad: "x".repeat(5 * 1024 * 1024) })
        .then(() => null, (e) => e);
      assert.ok(err, "oversized payload must reject");
      assert.equal(err.bridgeError, "PAYLOAD_TOO_LARGE");
      assert.equal(err.retryable, false);
      // Only the oversized call failed; the bridge stays healthy.
      assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    } finally {
      bridge.stop();
    }
  }

  // RESTART_FAILED: crash, then poison every later generation → the lazy
  // respawn's start() rejects and the wrapper is typed.
  {
    const bridge = new McpBridge(fakeServer, ["--die-if-exists", poison, counter]);
    try {
      await bridge.request("tools/call", {});
      await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
      writeFileSync(poison, "1");
      const err = await bridge.request("tools/call", {}).then(() => null, (e) => e);
      assert.ok(err, "restart against a poisoned binary must reject");
      // The wrapper PRESERVES the start() failure's own class: a
      // spawn-class failure (the poisoned child dies before the
      // handshake → discardFailedStart's RESTART_FAILED, retryable) stays
      // typed; a missing/not-executable binary would surface as
      // LAUNCH_FAILED with retryable=false instead (R1-P3 fix: never
      // retype everything retryable).
      // A child that SPAWNS then dies (exit before the handshake) is
      // reported by the exit handler as CHILD_EXITED; a child that never
      // spawns (ENOENT/EACCES) comes back LAUNCH_FAILED; a handshake that
      // fails otherwise is discardFailedStart's RESTART_FAILED. All three
      // are typed and retryable — the prose contract is unchanged.
      assert.ok(
        ["RESTART_FAILED", "LAUNCH_FAILED", "CHILD_EXITED"].includes(err.bridgeError),
        `unexpected class ${err.bridgeError}`,
      );
      assert.match(err.message, /restart failed|crashed|exited/i);
    } finally {
      bridge.stop();
      rmSync(poison, { force: true });
    }
  }

  // BRIDGE_STOPPED: an explicitly stopped bridge is never resurrected
  // (#645-era contract) and the rejection is typed + non-retryable.
  {
    const bridge = new McpBridge(fakeServer, [counter]);
    await bridge.request("tools/call", {});
    bridge.stop();
    const err = await bridge.request("tools/call", {}).then(() => null, (e) => e);
    assert.ok(err, "stopped bridge must reject");
    assert.equal(err.bridgeError, "BRIDGE_STOPPED");
    assert.equal(err.retryable, false);
  }
  rmSync(dir, { recursive: true, force: true });
});

testSpawn("onGeneration contains BOTH a sync throw and a rejected promise (R2 P2-1/P2-3)", async () => {
  const dir = makeDir();
  const bridge = new McpBridge(fakeServer, [join(dir, "spawns")], {
    // A callback that throws synchronously on generation 1 and returns a
    // rejected promise on generation 2: neither may escape into the
    // process as an unhandledRejection (Node's default terminates it).
    onGeneration: (info) => {
      if (info.generation === 1) throw new Error("sync boom");
      return Promise.reject(new Error("async boom"));
    },
  });
  // An unhandled rejection anywhere in this process must fail the test.
  const unhandled = [];
  const onUnhandled = (reason) => unhandled.push(reason);
  process.on("unhandledRejection", onUnhandled);
  try {
    // Generation 1: the sync throw must not break start()/request().
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    // Generation 2: crash → respawn; the async rejection must be contained.
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    assert.equal(bridge.generations, 2);
    // Give any would-be unhandled rejection a macrotask to surface.
    await new Promise((r) => setTimeout(r, 50));
    assert.equal(unhandled.length, 0, "callback failures must be contained");
  } finally {
    process.removeListener("unhandledRejection", onUnhandled);
    bridge.stop();
    rmSync(dir, { recursive: true, force: true });
  }
});

testSpawn("a lazy-respawn preserves the start() failure's error class (R2 P2-3)", async () => {
  const dir = makeDir();
  const counter = join(dir, "spawns");
  // --die-if-exists poisons every generation from now on: the FIRST request
  // spawns a healthy child; after the crash the respawn spawns a poisoned
  // one that dies BEFORE the handshake — an exit-class failure, which the
  // wrapper must report with its own typed code (not blanket-retryable).
  const poison = join(dir, "poison");
  const bridge = new McpBridge(fakeServer, ["--die-if-exists", poison, counter]);
  try {
    await bridge.request("tools/call", {});
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    writeFileSync(poison, "1");
    const err = await bridge.request("tools/call", {}).then(() => null, (e) => e);
    assert.ok(err, "poisoned respawn must reject");
    // The class must come from the start() failure itself (LAUNCH_FAILED for
    // spawn-class, CHILD_EXITED for a spawn-then-die, RESTART_FAILED
    // otherwise) and must never fabricate retryable=true for LAUNCH_FAILED.
    assert.ok(
      ["LAUNCH_FAILED", "CHILD_EXITED", "RESTART_FAILED"].includes(err.bridgeError),
      `unexpected class ${err.bridgeError}`,
    );
    if (err.bridgeError === "LAUNCH_FAILED") assert.equal(err.retryable, false);
  } finally {
    bridge.stop();
    rmSync(dir, { recursive: true, force: true });
  }
});
