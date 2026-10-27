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

/// Host canary, resolved ONCE at module load: the fake server is spawned via
/// its shebang, which Windows cannot exec directly (`spawn EFTYPE`,
/// pre-existing host limitation documented in no_drift.test.mjs and
/// scientific_workflow_compiler_11.test.mjs). Tests that need a live child
/// register through testSpawn() and are SKIP-marked — never fail-marked — on
/// such hosts, exactly like the older inline canary below. POSIX/CI hosts
/// spawn normally and the coverage is unchanged.
let spawnWorks = null; // null = unknown, true/false = canary verdict
async function canarySpawns() {
  if (spawnWorks !== null) return spawnWorks;
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    await bridge.request("tools/call", {});
    spawnWorks = true;
  } catch (err) {
    // Spawn-CLASS failures only (shebang/EFTYPE, missing binary, perms)
    // mean "this host cannot exec the fake server". Any other failure —
    // e.g. a real bridge success-path regression on a POSIX CI host — is
    // rethrown so the suite fails loudly instead of skipping green.
    const msg = String(err?.message ?? err);
    if (!/EFTYPE|ENOENT|EACCES|EPIPE|Failed to launch|spawn/i.test(msg))
      throw err;
    spawnWorks = false;
    console.log(`canary: fake MCP server cannot spawn on this host: ${msg}`);
  } finally {
    bridge.stop();
  }
  return spawnWorks;
}

const canSpawnFakeServer = await canarySpawns();
const kSkipReason =
  "host cannot exec the fake MCP server (pre-existing Windows limitation)";
const testSpawn = (name, fn) =>
  test(name, { skip: canSpawnFakeServer ? false : kSkipReason }, fn);

testSpawn("first request initializes against a healthy child", async () => {
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

testSpawn("a child crash respawns exactly once; concurrent requests share the start", async () => {
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

testSpawn("explicit stop() is never resurrected by a request", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  await bridge.request("tools/call", {});
  bridge.stop();
  await assert.rejects(() => bridge.request("tools/call", {}), /stopped/);
  await new Promise((r) => setTimeout(r, 100));
  assert.equal(spawnCount(counter), 1);
});

testSpawn("idempotent start() does not double-spawn a healthy bridge", async () => {
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

testSpawn("a desynced (unterminated >32MiB) stream is killed, not left as a zombie (F-PI-1)", async () => {
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

testSpawn("a healthy initialize does not leave the 30s startup deadline armed (F-PI-2)", async () => {
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

// ---------------------------------------------------------------------------
// Track 9 (mcp-surface-r4) WP-D: cancellation semantics and adversarial
// server frames. Oracles: the #645 cancellation contract (abort -> reject +
// exactly one notifications/cancelled; no result leaks after abort), the
// JSON-RPC transport-fidelity floor (non-JSON lines are dropped, huge
// payloads pass through untruncated — MAX_RESULT_CHARS is the shell's
// budget, not the bridge's), and the #645 structured-error passthrough.
// All triggers are additive modes of fake_mcp_server.mjs.
// ---------------------------------------------------------------------------

function readNotifyLog(path) {
  try {
    return readFileSync(path, "utf8")
      .trim()
      .split("\n")
      .filter(Boolean)
      .map((l) => JSON.parse(l));
  } catch {
    return [];
  }
}

/** Poll the notify log until it has the given number of
 * notifications/cancelled (or the deadline passes) - a fixed sleep races
 * the child's appendFileSync under load (review P2). */
async function waitForCancellations(path, count, deadlineMs = 5000) {
  const start = Date.now();
  for (;;) {
    const cancellations = readNotifyLog(path).filter(
      (n) => n.method === "notifications/cancelled",
    );
    if (cancellations.length >= count || Date.now() - start > deadlineMs)
      return cancellations.length;
    await new Promise((r) => setTimeout(r, 25));
  }
}

testSpawn("abort mid-flight rejects the call and notifies the server exactly once (#645)", async () => {
  const counter = makeCounter();
  const notifyLog = join(dirname(counter), "notify.log");
  const bridge = new McpBridge(fakeServer, ["--notify-log", notifyLog, counter]);
  try {
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");

    const controller = new AbortController();
    const pending = bridge.request(
      "tools/call",
      { name: "slow", arguments: { delay_ms: 4000 } },
      controller.signal,
    );
    // Abort while the fake server is still holding the reply.
    setTimeout(() => controller.abort(), 50);
    await assert.rejects(pending, /Aborted/);

    // Wait for the notification to reach the child, then count exactly.
    const cancellations = await waitForCancellations(notifyLog, 1);
    assert.equal(
      cancellations,
      1,
      `expected exactly one notifications/cancelled, got ${cancellations}`,
    );

    // The bridge stays healthy after a cancellation (state reset): the next
    // call resolves normally on the SAME child.
    assert.equal(bridge.alive, true);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
  } finally {
    bridge.stop();
  }
});

testSpawn("the late reply after an abort is dropped, not delivered (#645 no-leak)", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");

    const controller = new AbortController();
    let lateResolution = null;
    const pending = bridge.request(
      "tools/call",
      { arguments: { delay_ms: 300 } },
      controller.signal,
    );
    // The leak detector: a LATE resolve of the aborted call is the exact
    // "result leaks after cancel" defect - watch for it directly.
    pending.then(
      (v) => {
        lateResolution = v;
      },
      () => {},
    );
    controller.abort();
    await assert.rejects(pending, /Aborted/);
    // Outlive the fake server's delayed reply: it must NEVER deliver a
    // result to the dead call (the pending entry was removed on abort).
    await new Promise((r) => setTimeout(r, 500));
    assert.equal(lateResolution, null);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
  } finally {
    bridge.stop();
  }
});

testSpawn("a pre-aborted signal rejects without a server round-trip for the call", async () => {
  const counter = makeCounter();
  const notifyLog = join(dirname(counter), "notify.log");
  const bridge = new McpBridge(fakeServer, ["--notify-log", notifyLog, counter]);
  try {
    const controller = new AbortController();
    controller.abort();
    await assert.rejects(
      () => bridge.request("tools/call", {}, controller.signal),
      /Aborted/,
    );
    // The pre-aborted path fires onAbort synchronously once the lazy
    // respawn's recursive request() sees signal.aborted: exactly ONE
    // notifications/cancelled for the reserved rpc id (#645 "harmless
    // server-side") - an upper-bound assert here would miss a double-notify
    // regression (review P2).
    const cancellations = await waitForCancellations(notifyLog, 1);
    assert.equal(cancellations, 1);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
  } finally {
    bridge.stop();
  }
});

testSpawn("a non-JSON server line is dropped; framing and pending calls survive", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    // The garbage_line mode emits a malformed stdout line before the valid
    // response: the bridge must drop the line and still resolve.
    const result = await bridge.request("tools/call", { arguments: { garbage_line: true } });
    assert.equal(result?.content?.[0]?.text, "ok");
    assert.equal(bridge.alive, true);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    assert.equal(spawnCount(counter), 1, "a dropped line must not kill the child");
  } finally {
    bridge.stop();
  }
});

testSpawn("structured tool errors pass through with their taxonomy intact (#645)", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    const result = await bridge.request("tools/call", { arguments: { error_shape: true } });
    assert.equal(result?.isError, true, "isError must ride through the bridge");
    assert.equal(result?.errorCode, "E_TEST_SHAPE");
    assert.equal(result?.errorCategory, "test");
    assert.match(result?.content?.[0]?.text ?? "", /invalid parameters/);
  } finally {
    bridge.stop();
  }
});

testSpawn("the bridge does NOT truncate: transport fidelity is the shell's budget boundary", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    const size = 60_000; // > MAX_RESULT_CHARS (50_000), far below the 32 MiB line cap
    const result = await bridge.request("tools/call", { arguments: { huge: size } });
    assert.equal(result?.content?.[0]?.text?.length, size);
  } finally {
    bridge.stop();
  }
});

testSpawn("an oversized outbound payload fails alone; other pending calls survive (R14-C FIX-4)", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");

    // One delayed (in-flight) request plus one request whose payload exceeds
    // the server's 4 MiB stdin line cap. Before the guard, the oversized
    // write reached the server, came back as an id:null -32700 error, and the
    // bridge rejected EVERY pending request with it.
    const delayed = bridge.request("tools/call", { arguments: { delay_ms: 300 } });
    const oversized = bridge.request("tools/call", {
      arguments: { big: "x".repeat(5 * 1024 * 1024) },
    });
    await assert.rejects(() => oversized, /line cap/);
    assert.equal((await delayed)?.content?.[0]?.text, "late", "the in-flight call must survive");
    // The child was never written to and stays healthy.
    assert.equal(bridge.alive, true);
    assert.equal(spawnCount(counter), 1);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
  } finally {
    bridge.stop();
  }
});

testSpawn("a healthy initialize re-arms the fast-crash circuit (R14-C FIX-4)", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    // Two fast crashes (count 2), then a HEALTHY respawn+initialize (which
    // must reset the circuit), then three more fast crashes. Without the
    // reset the accumulated count reaches 5 and the final request is
    // permanently refused ("keeps crashing"); with it, the count is 3 and
    // lazy respawn still works.
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), /exited/);
    const result = await bridge.request("tools/call", {});
    assert.equal(result?.content?.[0]?.text, "ok", "respawn must still be enabled");
  } finally {
    bridge.stop();
  }
});

testSpawn("the outbound cap counts UTF-8 bytes, not UTF-16 code units (R14-C FIX-4 rework)", async () => {
  const counter = makeCounter();
  const bridge = makeBridge(counter);
  try {
    assert.equal((await bridge.request("tools/call", {}))?.content?.[0]?.text, "ok");

    // 3,000,000 CJK characters: 3,000,000 UTF-16 code units (UNDER the 4 MiB
    // cap by string length) but 9,000,000 UTF-8 bytes (OVER it). A
    // .length-based guard lets this through; the byte-based guard must
    // refuse it before the write.
    const multibyte = "地".repeat(3_000_000);
    const delayed = bridge.request("tools/call", { arguments: { delay_ms: 300 } });
    const oversized = bridge.request("tools/call", { arguments: { big: multibyte } });
    await assert.rejects(() => oversized, /byte/);
    assert.equal((await delayed)?.content?.[0]?.text, "late");
    assert.equal(bridge.alive, true);
    assert.equal(spawnCount(counter), 1);
  } finally {
    bridge.stop();
  }
});

// ---------------------------------------------------------------------------
// R15-D: a generation whose START failed (spawn ENOENT/EACCES, initialize
// timeout) must be discarded, not left as this.child with exited=false.
// Before the fix such a bridge reported alive===true forever, every later
// request wrote into a dead stream ("MCP write failed"), and the fast-crash
// circuit never saw the failure (the exit handler never fires for a child
// that never spawned).
// ---------------------------------------------------------------------------

/** Resolve to the rejection Error, or a sentinel when the promise does not
 * reject within `ms` — a regression fails fast instead of hanging the run. */
function rejectionWithin(promise, ms) {
  return Promise.race([
    promise.then(
      () => new Error("request resolved instead of rejecting"),
      (e) => e,
    ),
    new Promise((r) => setTimeout(() => r(new Error(`request still pending after ${ms}ms`)), ms)),
  ]);
}

test("a spawn failure leaves the bridge dead, not CONNECTED", async () => {
  const missing = join(tmpdir(), "no-such-binary-r15d");
  const bridge = new McpBridge(missing, []);
  try {
    await assert.rejects(
      () => bridge.request("tools/call", {}),
      /Failed to launch|restart failed/,
    );
    // The failed child must be discarded: dead bridge, launch-failure error.
    // Before the fix this.child stayed set with exited=false, so alive
    // reported true and exprs_status showed CONNECTED.
    assert.equal(bridge.alive, false);
    assert.match(bridge.error ?? "", /Failed to launch/);

    // Every later request must take the respawn path and fail with a START
    // failure — never the "write into the zombie stream" defect.
    const second = await rejectionWithin(bridge.request("tools/call", {}), 2000);
    assert.match(second.message, /restart failed|Failed to launch|failed to start/);
    assert.doesNotMatch(second.message, /MCP write failed/);

    // Each failed start is booked as a fast crash; after enough attempts the
    // circuit opens and refuses further lazy respawns promptly.
    let last = second;
    for (let i = 0; i < 8 && !/keeps crashing/.test(last.message); i++)
      last = await rejectionWithin(bridge.request("tools/call", {}), 2000);
    assert.match(last.message, /keeps crashing/);
  } finally {
    bridge.stop();
  }
});

test("an initialize timeout tears the hung child down and the next request respawns", async (t) => {
  if (!(await canarySpawns())) {
    t.skip("host cannot exec the fake MCP server (pre-existing Windows limitation)");
    return;
  }
  const counter = makeCounter();
  // --hang-initialize: the child spawns (counter ticks) but never answers
  // initialize, so only the bridge-side deadline settles the handshake.
  const bridge = new McpBridge(fakeServer, ["--hang-initialize", counter], {
    startupTimeoutMs: 300,
  });
  try {
    await assert.rejects(() => bridge.request("tools/call", {}), /did not initialize/);
    // The hung child is killed and the generation discarded: the bridge must
    // read as dead, not CONNECTED. Poll instead of a fixed sleep — SIGTERM
    // teardown can exceed a small constant on a loaded machine.
    for (let waited = 0; waited < 5000 && bridge.alive; waited += 25)
      await new Promise((r) => setTimeout(r, 25));
    assert.equal(bridge.alive, false);

    // The next request respawns a fresh child (which also hangs) instead of
    // writing into the zombie — a regression makes this hang the full
    // request timeout, so race it against 2s.
    const second = await rejectionWithin(bridge.request("tools/call", {}), 2000);
    assert.match(second.message, /did not initialize/);
    assert.equal(spawnCount(counter), 2);
  } finally {
    bridge.stop();
  }
});
