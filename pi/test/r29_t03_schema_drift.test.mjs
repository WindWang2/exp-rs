/**
 * R29-T03 WP04: schema-level catalog drift detection after a respawn.
 *
 * R25-C compared tool NAMES only: a respawn onto a binary whose tool kept
 * the name but changed its inputSchema (new required parameter, new
 * capability version) was invisible — the agent then calls the new tool
 * through the OLD Pi-side schema. These tests drive the REAL shell through
 * a real crash (tools/call {crash:true} kills the child), a catalog file
 * rewrite, and the lazy respawn, then assert exprs_status reports
 * added/changed/removed — and that a stable catalog RESETS the drift state
 * instead of sticking.
 *
 * Run: node --test pi/test/r29_t03_schema_drift.test.mjs
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

async function loadShell(pi, toolsFile) {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-drift-"));
  const counter = join(dir, "counter");
  const saved = Object.fromEntries(kEnvKeys.map((k) => [k, process.env[k]]));
  try {
    process.env.EXP_RS_MCP_BIN = fakeServer;
    process.env.EXP_RS_MCP_ARGS = `--tools-file ${toolsFile} ${counter}`;
    process.env.EXP_RS_TOOL_CATEGORIES = "meta";
    const mod = await import(`${shellUrl}?r29s=${Math.random()}`);
    await mod.default(pi);
    return { dir, counter };
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

test("schema-only change is detected as `changed` after a crash respawn", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeFileSync(toolsFile, JSON.stringify([
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]));
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    assert.equal((await statusDetails(pi)).generation, 1);
    // Crash the child (generation 1 dies mid-call), mutate the catalog
    // (same names, NEW schema on meta:probe), then trigger the lazy
    // respawn through the next tool call.
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined),
    );
    writeFileSync(toolsFile, JSON.stringify([
      { name: "meta:probe", inputSchema: kSchemaV2 },
      { name: "meta:helper" },
    ]));
    const out = await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined);
    assert.equal(out.content[0].text, "ok"); // respawn (generation 2) answered

    const details = await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 2 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "generation 2 + settled drift comparison");
    // Same tool names everywhere: only the SCHEMA moved.
    assert.deepEqual(details.drift.added, []);
    assert.deepEqual(details.drift.removed, []);
    assert.deepEqual(details.drift.changed, ["meta:probe"]);
    // The text projection announces the schema drift, not just names.
    const text = (await toolByName(pi, "exprs_status").execute("c", {}, undefined)).content[0].text;
    assert.match(text, /schema changed: meta:probe/);
    assert.match(text, /restart the Pi session/i);
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("name drift (added/removed) is still detected alongside schema drift", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-drift-"));
  const toolsFile = join(dir, "tools.json");
  writeFileSync(toolsFile, JSON.stringify([
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:old" },
  ]));
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined),
    );
    writeFileSync(toolsFile, JSON.stringify([
      { name: "meta:probe", inputSchema: kSchemaV1 }, // unchanged name AND schema
      { name: "meta:new" },                            // added
      // meta:old removed
    ]));
    await toolByName(pi, "exprs_meta_probe").execute("c", {}, undefined);
    const details = await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 2 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "settled drift");
    assert.deepEqual(details.drift.added, ["meta:new"]);
    assert.deepEqual(details.drift.removed, ["meta:old"]);
    assert.deepEqual(details.drift.changed, []); // probe unchanged: no false `changed`
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("drift is measured against REGISTRATION: a drifted-but-stable catalog stays reported, returning to the registered catalog clears it", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-drift-"));
  const toolsFile = join(dir, "tools.json");
  // meta:helper stays UNCHANGED in every generation so it can trigger the
  // lazy respawns: since R30-T05 the drifted meta:probe itself is refused
  // at dispatch (the R29 trigger-by-probe shape would now reject).
  writeFileSync(toolsFile, JSON.stringify([
    { name: "meta:probe", inputSchema: kSchemaV1 },
    { name: "meta:helper" },
  ]));
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    // Generation 2 with a drifted schema — drift state populates...
    await assert.rejects(
      toolByName(pi, "exprs_meta_helper").execute("c", { crash: true }, undefined),
    );
    writeFileSync(toolsFile, JSON.stringify([
      { name: "meta:probe", inputSchema: kSchemaV2 },
      { name: "meta:helper" },
    ]));
    await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined);
    await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.drift.changed.length === 1 ? d : null;
    }, 8000, "drift populated");
    // ...crash AGAIN with the SAME drifted catalog: the drift is vs the
    // GENERATION-1 registration, so it must STAY reported (Pi's parameter
    // schemas are still the stale ones — honest, not sticky-state noise).
    await assert.rejects(
      toolByName(pi, "exprs_meta_helper").execute("c", { crash: true }, undefined),
    );
    await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined);
    const stillDrifted = await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 3 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "generation 3 settled");
    assert.deepEqual(stillDrifted.drift.changed, ["meta:probe"]);
    // ...and returning the catalog to the REGISTERED schema clears it
    // (and re-enables dispatch — the refusal is not a permanent freeze).
    await assert.rejects(
      toolByName(pi, "exprs_meta_helper").execute("c", { crash: true }, undefined),
    );
    writeFileSync(toolsFile, JSON.stringify([
      { name: "meta:probe", inputSchema: kSchemaV1 },
      { name: "meta:helper" },
    ]));
    await toolByName(pi, "exprs_meta_helper").execute("c", {}, undefined);
    const cleared = await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 4 && d.drift.comparisonInFlight === false && d.drift.changed.length === 0 ? d : null;
    }, 8000, "drift cleared at generation 4");
    assert.deepEqual(cleared.drift.added, []);
    assert.deepEqual(cleared.drift.removed, []);
    assert.equal(
      (await toolByName(pi, "exprs_meta_probe").execute("c", {}, undefined)).content[0].text,
      "ok",
    );
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});

test("reordered-but-equal schemas are NOT drift (canonical comparison)", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-drift-"));
  const toolsFile = join(dir, "tools.json");
  // Key order differs between the two documents — semantically equal.
  writeFileSync(toolsFile, JSON.stringify([
    { name: "meta:probe", inputSchema: { type: "object", properties: { a: { type: "string" }, b: { type: "integer" } } } },
  ]));
  const pi = fakePi();
  const loaded = await loadShell(pi, toolsFile);
  try {
    await assert.rejects(
      toolByName(pi, "exprs_meta_probe").execute("c", { crash: true }, undefined),
    );
    writeFileSync(toolsFile, JSON.stringify([
      { name: "meta:probe", inputSchema: { properties: { b: { type: "integer" }, a: { type: "string" } }, type: "object" } },
    ]));
    await toolByName(pi, "exprs_meta_probe").execute("c", {}, undefined);
    const details = await waitFor(async () => {
      const d = await statusDetails(pi);
      return d.generation === 2 && d.drift.comparisonInFlight === false ? d : null;
    }, 8000, "settled");
    assert.deepEqual(details.drift.changed, [], "key reordering must not read as drift");
    assert.deepEqual(details.drift.added, []);
    assert.deepEqual(details.drift.removed, []);
  } finally {
    cleanup(loaded.dir);
    rmSync(dir, { recursive: true, force: true });
  }
});
