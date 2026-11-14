/**
 * R30-T05 WP03: exprs_wait_for_execution error/deadline/abort semantics.
 *
 * The wait loop used to (a) not check its signal, so a status request in
 * flight during an abort resolved into a SUCCESS return for a terminal
 * state that arrived after the waiter had already ended; (b) ignore
 * isError/errorCode on get_execution_status, converting typed errors into
 * an "unknown status" poll-until-timeout; (c) accept any timeout_seconds
 * (NaN/Infinity → unbounded loop, ≤0 → immediate confusing timeout); (d)
 * let one slow status response blow through the total wait budget because
 * only the sleep was deadline-checked. These tests drive the REAL shell's
 * registered wait tool against scripted get_execution_status responses.
 *
 * Run: node --test pi/test/r30_t05_wait_semantics.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { activeBridges } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");
const shellUrl = pathToFileURL(join(here, "..", "exp-rs-spatial.ts")).href;

function fakePi() {
  const tools = [];
  return { tools, registerTool(def) { tools.push(def); } };
}
const toolByName = (pi, name) => pi.tools.find((t) => t.name === name);

const kEnvKeys = ["EXP_RS_MCP_BIN", "EXP_RS_MCP_ARGS", "EXP_RS_TOOL_CATEGORIES"];

async function loadShell(pi, statusScript, callLog) {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-wait-"));
  const toolsFile = join(dir, "tools.json");
  const scriptFile = join(dir, "status-script.json");
  writeFileSync(toolsFile, JSON.stringify([{ name: "meta:probe" }]));
  writeFileSync(scriptFile, JSON.stringify(statusScript));
  const saved = Object.fromEntries(kEnvKeys.map((k) => [k, process.env[k]]));
  try {
    process.env.EXP_RS_MCP_BIN = fakeServer;
    const argPairs = ["--tools-file", toolsFile, "--status-script", scriptFile];
    if (callLog) argPairs.push("--call-log", callLog);
    argPairs.push(join(dir, "counter"));
    process.env.EXP_RS_MCP_ARGS = argPairs.join(" ");
    process.env.EXP_RS_TOOL_CATEGORIES = "meta";
    const mod = await import(`${shellUrl}?r30w=${Math.random()}`);
    await mod.default(pi);
    return dir;
  } finally {
    for (const [k, v] of Object.entries(saved)) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  }
}

function cleanup(dir) {
  for (const b of [...activeBridges]) b.stop();
  rmSync(dir, { recursive: true, force: true });
}

const waitCall = (pi, args, signal) =>
  toolByName(pi, "exprs_wait_for_execution").execute("c", {
    execution_id: "task-1",
    ...args,
  }, signal);

function readCallLog(path) {
  if (!existsSync(path)) return [];
  return readFileSync(path, "utf8").split("\n").filter(Boolean).map((l) => JSON.parse(l));
}

test("terminal poll returns the final envelope with details", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi, {
    get_execution_status: [
      { json: { status: "running", progress: 10 } },
      { json: { status: "completed", result: { asset_id: "a-7" } } },
    ],
  });
  try {
    const out = await waitCall(pi, { timeout_seconds: 10, poll_interval_ms: 100 });
    assert.match(out.content[0].text, /completed/);
    assert.equal(out.details.status, "completed");
    assert.equal(out.details.result.asset_id, "a-7");
  } finally {
    cleanup(dir);
  }
});

test("typed get_execution_status errors surface instead of polling to timeout", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi, {
    get_execution_status: [
      { isError: true, message: "no such execution", errorCode: "UNKNOWN_EXECUTION",
        errorCategory: "validation", retryable: false },
    ],
  });
  try {
    await assert.rejects(
      waitCall(pi, { timeout_seconds: 8, poll_interval_ms: 100 }),
      (err) => {
        assert.match(err.message, /no such execution/);
        assert.equal(err.errorCode, "UNKNOWN_EXECUTION");
        assert.equal(err.errorCategory, "validation");
        assert.equal(err.retryable, false);
        return true;
      },
    );
  } finally {
    cleanup(dir);
  }
});

test("timeout_seconds is validated: NaN/Infinity/zero/negative refuse immediately", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi, { get_execution_status: [] });
  try {
    for (const bad of [Number.NaN, Number.POSITIVE_INFINITY, 0, -5]) {
      await assert.rejects(
        waitCall(pi, { timeout_seconds: bad }),
        (err) => {
          assert.match(err.message, /timeout_seconds/);
          assert.equal(readCallLog(join(dir, "none")).length, 0); // never polled
          return true;
        },
      );
    }
  } finally {
    cleanup(dir);
  }
});

test("the total wait budget bounds in-flight status requests, not just sleeps", async () => {
  const pi = fakePi();
  // Poll 1 answers "running" fast; poll 2 holds "completed" for 300ms. The
  // 250ms total budget must CUT poll 2 at the deadline (a completed that
  // arrives after the budget is a timeout, not a green result).
  const dir = await loadShell(pi, {
    get_execution_status: [
      { json: { status: "running" } },
      { delay_ms: 300, json: { status: "completed" } },
    ],
  });
  try {
    const started = Date.now();
    await assert.rejects(
      waitCall(pi, { timeout_seconds: 0.25, poll_interval_ms: 100 }),
      (err) => {
        assert.match(err.message, /timed out/i);
        assert.match(err.message, /running/);
        return true;
      },
    );
    const elapsed = Date.now() - started;
    assert.ok(elapsed < 700, `timeout should fire near the 250ms budget, took ${elapsed}ms`);
  } finally {
    cleanup(dir);
  }
});

test("abort during an in-flight terminal poll: the late completed never becomes a success", async () => {
  const callLog = mkdtempSync(join(tmpdir(), "r30t05-log-")) + "/calls.log";
  const pi = fakePi();
  // Poll 1 "running" (fast), poll 2 "completed" delayed 160ms — the abort
  // lands while poll 2 is in flight. The waiter already ended at abort: a
  // terminal arriving afterwards must not resurrect it as a success.
  const dir = await loadShell(pi, {
    get_execution_status: [
      { json: { status: "running" } },
      { delay_ms: 160, json: { status: "completed" } },
    ],
  }, callLog);
  const controller = new AbortController();
  try {
    const waiting = waitCall(pi, { timeout_seconds: 10, poll_interval_ms: 100 }, controller.signal);
    // Let poll 2 (the delayed completed) be sent, then abort mid-flight.
    setTimeout(() => controller.abort(new Error("user cancelled")), 130);
    await assert.rejects(waiting, (err) => {
      assert.match(err.message, /abort/i);
      return true;
    });
    // Exactly ONE server-side cancel for the execution (no duplicates).
    let cancels = [];
    for (let waited = 0; waited < 2000; waited += 25) {
      cancels = readCallLog(callLog).filter((c) => c.name === "cancel_execution");
      if (cancels.length >= 1) break;
      await new Promise((r) => setTimeout(r, 25));
    }
    assert.equal(cancels.length, 1, `expected one cancel, got ${JSON.stringify(readCallLog(callLog))}`);
  } finally {
    cleanup(dir);
    rmSync(dirname(callLog), { recursive: true, force: true });
  }
});

test("abort during the sleep window rejects promptly and cancels once", async () => {
  const callLog = mkdtempSync(join(tmpdir(), "r30t05-log-")) + "/calls.log";
  const pi = fakePi();
  const dir = await loadShell(pi, {
    get_execution_status: [{ json: { status: "running" } }],
  }, callLog);
  const controller = new AbortController();
  try {
    const waiting = waitCall(pi, { timeout_seconds: 30, poll_interval_ms: 5000 }, controller.signal);
    setTimeout(() => controller.abort(new Error("user cancelled")), 150);
    const started = Date.now();
    await assert.rejects(waiting, /abort/i);
    const elapsed = Date.now() - started;
    assert.ok(elapsed < 2500, `abort should interrupt the sleep promptly, took ${elapsed}ms`);
    let cancels = [];
    for (let waited = 0; waited < 2000; waited += 25) {
      cancels = readCallLog(callLog).filter((c) => c.name === "cancel_execution");
      if (cancels.length >= 1) break;
      await new Promise((r) => setTimeout(r, 25));
    }
    assert.equal(cancels.length, 1);
  } finally {
    cleanup(dir);
    rmSync(dirname(callLog), { recursive: true, force: true });
  }
});

test("malformed or unknown status responses fail fast instead of polling blind", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi, {
    get_execution_status: [{ text: "not json at all" }],
  });
  try {
    const started = Date.now();
    await assert.rejects(
      waitCall(pi, { timeout_seconds: 20, poll_interval_ms: 100 }),
      (err) => {
        assert.match(err.message, /unparseable|status/i);
        return true;
      },
    );
    const elapsed = Date.now() - started;
    assert.ok(elapsed < 5000, `malformed statuses should fail fast, took ${elapsed}ms`);
  } finally {
    cleanup(dir);
  }
});

test("cancelling is a non-terminal status: the wait keeps polling to canceled", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi, {
    get_execution_status: [
      { json: { status: "running" } },
      { json: { status: "cancelling" } },
      { json: { status: "canceled" } },
    ],
  });
  try {
    const out = await waitCall(pi, { timeout_seconds: 10, poll_interval_ms: 100 });
    assert.match(out.content[0].text, /canceled/);
    assert.equal(out.details.status, "canceled");
  } finally {
    cleanup(dir);
  }
});
