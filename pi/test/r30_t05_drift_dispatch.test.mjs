/**
 * R30-T05 WP02: schema drift must CONSTRAIN dispatch, not just report.
 *
 * R29-T03 detected catalog drift (added/changed/removed after a respawn)
 * and surfaced it through exprs_status — but the bridged execute closures
 * still dispatched with the stale Pi-side registration schema. These tests
 * pin the dispatch contract:
 *
 *  - a CHANGED or REMOVED tool is refused with a typed SCHEMA_DRIFT error
 *    (non-retryable; restart is the recovery), never silently dispatched;
 *  - an UNCHANGED tool keeps working while another tool drifted (no
 *    session-wide freeze);
 *  - the first call after a respawn waits for the catalog comparison of
 *    the generation that will actually serve it (bounded);
 *  - a comparison that cannot settle refuses with a retryable
 *    SCHEMA_UNVERIFIED error instead of dispatching blind;
 *  - a FAILED comparison is retried once by the gate and recovers without
 *    a session restart;
 *  - a schema restored to the registered version re-enables dispatch.
 *
 * Run: node --test pi/test/r30_t05_drift_dispatch.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
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

const kSchemaV1 = { type: "object", properties: { path: { type: "string" } } };
const kSchemaV2 = {
  type: "object",
  properties: { path: { type: "string" }, band: { type: "integer" } },
  required: ["band"],
};

async function loadShell(pi, toolsFile, extraArgs = []) {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const counter = join(dir, "counter");
  const saved = Object.fromEntries(kEnvKeys.map((k) => [k, process.env[k]]));
  try {
    process.env.EXP_RS_MCP_BIN = fakeServer;
    process.env.EXP_RS_MCP_ARGS = [
      "--tools-file", toolsFile, ...extraArgs, counter,
    ].join(" ");
    process.env.EXP_RS_TOOL_CATEGORIES = "meta";
    const mod = await import(`${shellUrl}?r30d=${Math.random()}`);
    await mod.default(pi);
    return { dir, counter };
  } finally {
    for (const [k, v] of Object.entries(saved)) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  }
}

function writeTools(path, tools) {
  writeFileSync(path, JSON.stringify(tools));
}

function cleanup(dir) {
  for (const b of [...activeBridges]) b.stop();
  rmSync(dir, { recursive: true, force: true });
}

async function waitFor(predicate, timeoutMs = 8000, what = "condition") {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    last = await predicate();
    if (last) return last;
    await new Promise((r) => setTimeout(r, 25));
  }
  throw new Error(`timed out waiting for ${what}; last=${JSON.stringify(last)}`);
}

const statusDetails = async (pi) =>
  (await toolByName(pi, "exprs_status").execute("c", {}, undefined)).details;

test("changed tool is refused (typed, non-retryable); unchanged tool still dispatches", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    // Sanity: gen-1 dispatch works.
    assert.equal((await toolByName(pi, "exprs_meta_probe").execute("c", {}, undefined)).content[0].text, "ok");
    // Crash gen 1, change meta:probe's schema, respawn.
    await assert.rejects(toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined));
    writeTools(toolsFile, [
      { name: "meta:probe", inputSchema: kSchemaV2 },
      { name: "meta:helper" },
    ]);
    // Respawn via the UNCHANGED helper; wait for the comparison to land.
    assert.equal((await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined)).content[0].text, "ok");
    await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 2 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "generation 2 comparison settled");

    // THE dispatch gate: calling the changed tool must REFUSE, not dispatch.
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", { path: "x" }, undefined),
      (err) => {
        assert.equal(err.bridgeError, "SCHEMA_DRIFT");
        assert.equal(err.retryable, false);
        assert.match(err.message, /meta:probe/);
        assert.match(err.message, /restart/i);
        return true;
      },
    );
    // The refusal must not have dispatched: the fake server answers "ok"
    // for every tools/call — a dispatch would have returned "ok" content.

    // Unchanged tool keeps working while probe drifted (no session freeze).
    assert.equal((await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined)).content[0].text, "ok");
    // exprs_status still answers and reports the drift.
    const d = await statusDetails(pi);
    assert.deepEqual(d.drift.changed, ["meta:probe"]);
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("removed tool is refused with SCHEMA_DRIFT", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    await assert.rejects(toolByName(pi, "exprs_meta_helper").execute("c", { crash: true }, undefined));
    writeTools(toolsFile, [{ name: "meta:helper" }]);
    // Trigger the lazy respawn (unchanged helper), then wait for the settle.
    assert.equal((await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined)).content[0].text, "ok");
    await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 2 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "generation 2 comparison settled");
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", {}, undefined),
      (err) => {
        assert.equal(err.bridgeError, "SCHEMA_DRIFT");
        return true;
      },
    );
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("first call after respawn waits for the live generation's comparison", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  // Slow the re-list so the comparison is observably in flight when the
  // first post-respawn call arrives.
  const loaded = await loadShell(pi, toolsFile, ["--delay-tools-list", "600"]);
  try {
    await assert.rejects(toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined));
    // UNCHANGED catalog on generation 2: the gate must WAIT for the
    // comparison (not refuse, not dispatch blind), then dispatch.
    const started = Date.now();
    const out = await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined);
    const elapsed = Date.now() - started;
    assert.equal(out.content[0].text, "ok");
    // 600ms delayed comparison: a gate that skipped the wait would return
    // in well under 400ms (local handshake + dispatch).
    assert.ok(elapsed >= 400, `gate should have waited for the comparison, returned in ${elapsed}ms`);
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("comparison that cannot settle refuses with retryable SCHEMA_UNVERIFIED", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  // 8s re-list delay vs the gate's bounded wait: dispatch must refuse
  // (retryable) rather than hang or dispatch blind.
  const loaded = await loadShell(pi, toolsFile, ["--delay-tools-list", "8000"]);
  try {
    await assert.rejects(toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined));
    await assert.rejects(
      toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined),
      (err) => {
        assert.equal(err.bridgeError, "SCHEMA_UNVERIFIED");
        assert.equal(err.retryable, true);
        assert.match(err.message, /comparison/i);
        return true;
      },
    );
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("failed comparison is retried by the gate and recovers without restart", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  const failOnce = join(dir, "fail-once");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile, ["--fail-tools-list-file", failOnce]);
  // Arm the one-shot failure AFTER registration (the initial tools/list must
  // succeed; only the post-crash comparison fails once).
  writeFileSync(failOnce, "1");
  try {
    await assert.rejects(toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined));
    // The post-crash comparison fails (JSON-RPC error); the gate re-runs it
    // once, the retry succeeds (fail marker consumed), dispatch proceeds.
    const out = await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined);
    assert.equal(out.content[0].text, "ok");
    const d = await statusDetails(pi);
    assert.equal(d.generation, 2);
    assert.equal(d.drift.lastError, null);
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("schema restored to the registered version re-enables dispatch (two restarts)", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    // gen1 → gen2: probe schema changes → dispatch refused.
    await assert.rejects(toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined));
    writeTools(toolsFile, [
      { name: "meta:probe", inputSchema: kSchemaV2 },
      { name: "meta:helper" },
    ]);
    // Trigger the lazy respawn through the unchanged helper, then settle.
    assert.equal((await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined)).content[0].text, "ok");
    await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 2 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "generation 2 comparison settled");
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", {}, undefined),
      /SCHEMA_DRIFT|schema/i,
    );
    // gen2 → gen3: catalog restored to the REGISTERED schema (+ an added
    // tool, which must not freeze the session).
    await assert.rejects(toolByName(pi, "exprs_meta_helper").execute("c", { crash: true }, undefined));
    writeTools(toolsFile, [
      { name: "meta:probe", inputSchema: kSchemaV1 },
      { name: "meta:helper" },
      { name: "meta:extra" },
    ]);
    // Trigger the gen-3 respawn through the unchanged helper.
    assert.equal((await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined)).content[0].text, "ok");
    await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 3 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "generation 3 comparison settled");
    // Restored schema: dispatch allowed again — a drift refusal is not a
    // permanent freeze.
    const out = await toolByName(pi, "exprs_meta_probe").execute("c", {}, undefined);
    assert.equal(out.content[0].text, "ok");
    const d = await statusDetails(pi);
    assert.deepEqual(d.drift.changed, []);
    assert.deepEqual(d.drift.removed, []);
    assert.deepEqual(d.drift.added, ["meta:extra"]);
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("a success answered by a DRIFTED generation is refused post-hoc with an execution warning (R1-1/R1-2)", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    // Kill generation 1 WITHOUT triggering the respawn (the crash call
    // itself rejects first), so the NEXT call rides the lazy respawn.
    await assert.rejects(
      toolByName(pi, "exprs_meta_helper").execute("c", { crash: true }, undefined),
    );
    writeTools(toolsFile, [
      { name: "meta:probe", inputSchema: kSchemaV2 },
      { name: "meta:helper" },
    ]);
    // The gate sees generation 1 (no comparison for a dead child yet) and
    // passes; the request itself respawns onto generation 2, WHICH ANSWERS.
    // The post-hoc re-verification must refuse — and must WARN that the
    // call may already have executed server-side (R1-2).
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", { path: "x" }, undefined),
      (err) => {
        assert.equal(err.bridgeError, "SCHEMA_DRIFT");
        assert.equal(err.retryable, false);
        assert.match(err.message, /generation 2/);
        assert.match(err.message, /MAY ALREADY HAVE EXECUTED/i);
        assert.match(err.message, /orphaned/i);
        return true;
      },
    );
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("crash-crossed dispatches retry at most twice, then surface the crash error (R1-1)", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r30t05-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeTools(toolsFile, [
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]);
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    await assert.rejects(
      toolByName(pi, "exprs_meta_helper").execute("c", { crash: true }, undefined),
    );
    // Every attempt of this call crashes the generation that serves it:
    // initial dispatch (respawn g2) + retry 1 (g3) + retry 2 (g4), then the
    // crash error surfaces instead of looping forever.
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined),
      (err) => {
        assert.equal(err.bridgeError, "CHILD_EXITED");
        return true;
      },
    );
    const d = await statusDetails(pi);
    // R2-N4: normally 1 initial + 3 crash-respawned generations (4); if the
    // bridge processes a crash's exit event before the in-flight tools/list
    // response (no ordering guarantee), one hop can merge — 3..4 both prove
    // the bounded ladder (>= 2 retries happened and it terminated).
    assert.ok(
      d.generation >= 3 && d.generation <= 4,
      `expected the bounded retry ladder to burn 2-3 generations, got ${d.generation}`,
    );
    // The bridge is still usable after the bounded retry ladder.
    assert.equal(
      (await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined)).content[0].text,
      "ok",
    );
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});
