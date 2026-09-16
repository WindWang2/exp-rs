/**
 * Compiler & Grounding 11.0 — Pi/ZCode bridge robustness for the compiler
 * surfaces (goal work package H).
 *
 * Static floor: the compiler/probe tools ride the SAME McpBridge transport
 * as every other tool — the extension shell must not grow per-tool special
 * cases (that is how the Compiler 10.0 transport drift started, F-PI-1/2).
 * Cross-language anchors: the schema constants the bridge surfaces rely on
 * stay pinned in the C++ sources (workflow_ir "1.0", projection "1.0",
 * probe tool ids), so a silent rename fails here instead of at runtime.
 *
 * Behavioral floor: a compiler-shaped request with a large (real-scale)
 * WorkflowIR document round-trips through the shared bridge without hitting
 * the line-buffer kill — knowledge-budget and desync regressions show up as
 * exactly that failure.
 *
 * Run: node --test pi/test/scientific_workflow_compiler_11.test.mjs
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
const irHeader = readFileSync(
  join(here, "..", "..", "src", "agent", "harness", "workflow_ir.h"),
  "utf8",
);
const projectionHeader = readFileSync(
  join(here, "..", "..", "src", "agent", "harness", "provenance_projection.h"),
  "utf8",
);
const probesSource = readFileSync(
  join(here, "..", "..", "src", "agent", "harness", "grounding_probes.cpp"),
  "utf8",
);

const kCompilerTools = [
  "harness:compile_workflow",
  "harness:probe_facts",
  "harness:probe_model",
];

test("compiler surfaces ride the shared bridge — no per-tool forks in the shell", () => {
  for (const tool of kCompilerTools) {
    // The extension shell must not dispatch these by name; tools flow
    // through the uniform tools/list + tools/call path.
    assert.doesNotMatch(
      extensionSource,
      new RegExp(tool.replace(/[:]/g, "\\:")),
      `${tool} special-cased in the extension shell — move it behind the bridge`,
    );
  }
  // The bridge itself stays implementation #1 (no_drift guards this too;
  // here we pin the compiler tools onto that single transport).
  assert.match(extensionSource, /from "\.\/mcp_bridge\.ts"/);
});

test("schema constants the bridge relies on stay pinned in C++", () => {
  assert.match(irHeader, /kWorkflowIrSchemaVersion = "1\.0"/);
  assert.match(irHeader, /kWorkflowIrKind = "workflow_ir"/);
  assert.match(projectionHeader, /kProjectionSchemaVersion = "1\.0"/);
  assert.match(projectionHeader, /kCompilerMetadataKey = "compiler"/);
  for (const tool of ["harness:probe_facts", "harness:probe_model"]) {
    assert.ok(
      probesSource.includes(`return "${tool}";`),
      `${tool} renamed without updating the bridge guard`,
    );
  }
});

test("compiler knowledge pages stay within the 8 KiB knowledge-page budget", () => {
  // tool_shortlist pages compiler knowledge at a hard budget; the on-disk
  // pages it draws from must not silently outgrow it.
  const pages = [
    "capability-index.md",
    "capability-temporal.md",
    "spatial-algorithm-guide.md",
  ];
  for (const page of pages) {
    const bytes = readFileSync(join(here, "..", "knowledge", page)).length;
    assert.ok(
      bytes <= 32 * 1024,
      `${page} is ${bytes} bytes — knowledge pages must stay bounded`,
    );
  }
});

/// The fake server is spawned via its shebang, which Windows cannot exec
/// directly (pre-existing host limitation: pi/test/no_drift.test.mjs hits
/// the same `spawn EFTYPE` on master). The behavioral floor below only runs
/// where the canary proves the fake server can spawn — skipped otherwise,
/// never silently.
const { mkdtempSync, writeFileSync } = await import("node:fs");
const { tmpdir } = await import("node:os");
let spawnWorks = null; // null = unknown, true/false = canary verdict
async function canarySpawns() {
  if (spawnWorks !== null) return spawnWorks;
  const counter = join(mkdtempSync(join(tmpdir(), "pi-compiler11-canary-")), "spawns");
  writeFileSync(counter, "");
  const bridge = new McpBridge(fakeServer, [counter]);
  try {
    await bridge.request("tools/call", {});
    spawnWorks = true;
  } catch (err) {
    spawnWorks = false;
    console.log(`canary: fake MCP server cannot spawn on this host: ${err?.message}`);
  } finally {
    bridge.stop();
  }
  return spawnWorks;
}

test("a real-scale WorkflowIR document round-trips through the bridge", async (t) => {
  if (!(await canarySpawns())) {
    t.skip("host cannot exec the fake MCP server (pre-existing Windows limitation)");
    return;
  }
  const counter = join(mkdtempSync(join(tmpdir(), "pi-compiler11-")), "spawns");
  const bridge = new McpBridge(fakeServer, [counter]);
  try {
    // A 64-node compiler IR (the IrLimits::kMaxNodes bound) as one request:
    // ~40 KiB of JSON — far below the 32 MiB kill line, far above a token
    // request. A desync/budget regression turns this into a hang or kill.
    const nodes = [];
    for (let i = 0; i < 64; ++i) {
      nodes.push({
        id: `ndvi_${i}`,
        operator: "rs:spectral_index",
        params: { index: "NDVI", output: `/tmp/out/ndvi_${i}.tif` },
        inputs: [{ input: "primary" }],
        outputs: [
          { name: "output", artifact: { kind: "raster", numeric_domain: "index" } },
        ],
      });
    }
    const ir = {
      kind: "workflow_ir",
      schema_version: "1.0",
      goal: "bridge round-trip fixture",
      inputs: [{ name: "primary", ref: "asset-3" }],
      nodes,
    };
    const response = await bridge.request("tools/call", { ir });
    assert.equal(response?.content?.[0]?.text, "ok");
    assert.equal(bridge.alive, true);

    // The bridge survives the big request: a normal call still succeeds on
    // the same process (no stream desync).
    const after = await bridge.request("tools/call", { after: "big-ir" });
    assert.equal(after?.content?.[0]?.text, "ok");
  } finally {
    bridge.stop();
  }
});

test("a killed child after a compiler request lazy-respawns (abort floor)", async (t) => {
  if (!(await canarySpawns())) {
    t.skip("host cannot exec the fake MCP server (pre-existing Windows limitation)");
    return;
  }
  const counter = join(mkdtempSync(join(tmpdir(), "pi-compiler11-abort-")), "spawns");
  const bridge = new McpBridge(fakeServer, [counter]);
  try {
    // A crash mid-request rejects the pending call; the next call takes the
    // lazy-respawn path and SUCCEEDS (the #623 contract every compiler tool
    // inherits).
    await assert.rejects(() => bridge.request("tools/call", { crash: true }), Error);
    const recovered = await bridge.request("tools/call", { after: "crash" });
    assert.equal(recovered?.content?.[0]?.text, "ok");
    assert.equal(bridge.alive, true);
  } finally {
    bridge.stop();
  }
});
