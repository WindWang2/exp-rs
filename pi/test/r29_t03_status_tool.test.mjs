/**
 * R29-T03 WP02: the exprs_status entry point on EVERY path, plus the
 * scientific opt-in discovery path and explicit name-collision rejection.
 *
 * These tests execute the REAL extension shell (exp-rs-spatial.ts default
 * export) against the REAL transport (mcp_bridge.ts) and the fake MCP
 * server — no regex-over-source: a fake Pi host captures registerTool
 * calls, and the status tool's execute() is invoked for its live verdict.
 *
 * Run: node --test pi/test/r29_t03_status_tool.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { activeBridges } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");
const shellUrl = pathToFileURL(join(here, "..", "exp-rs-spatial.ts")).href;

/** Minimal Pi ExtensionAPI double: the shell only calls pi.registerTool. */
function fakePi() {
  const tools = [];
  return { tools, registerTool(def) { tools.push(def); } };
}
const toolByName = (pi, name) => pi.tools.find((t) => t.name === name);

const kEnvKeys = ["EXP_RS_MCP_BIN", "EXP_RS_MCP_ARGS", "EXP_RS_TOOL_CATEGORIES"];

/** Loads a FRESH shell module instance (query string busts the module
 * cache, so catalogDrift/registration state never leaks between tests)
 * with the given env, invokes its default export, restores env. */
async function loadShell(pi, { toolsFile, tools, categories, bin } = {}) {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-status-"));
  const counter = join(dir, "counter");
  const file = toolsFile ?? join(dir, "tools.json");
  if (tools !== undefined) writeFileSync(file, JSON.stringify(tools));
  const saved = Object.fromEntries(kEnvKeys.map((k) => [k, process.env[k]]));
  try {
    // The bridge spawns [bin, "--mcp", ...extraArgs]: with the shebang'd
    // fake server as bin, the server script sees its own argv exactly like
    // the direct-McpBridge tests spawn it (same launch class).
    process.env.EXP_RS_MCP_BIN = bin ?? fakeServer;
    process.env.EXP_RS_MCP_ARGS =
      bin !== undefined ? "" : `--tools-file ${file} ${counter}`;
    if (categories === undefined) delete process.env.EXP_RS_TOOL_CATEGORIES;
    else process.env.EXP_RS_TOOL_CATEGORIES = categories;
    const mod = await import(`${shellUrl}?r29=${Math.random()}`);
    await mod.default(pi);
    return { dir, counter, mod };
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

const kMetaTools = [
  { name: "meta:list_widgets", description: "w",
    inputSchema: { type: "object", properties: { a: { type: "string" } }, required: ["a"] } },
  { name: "spatial:probe", description: "p" },
];

test("healthy startup registers exprs_status alongside bridged + wait tools", async () => {
  const pi = fakePi();
  const { dir } = await loadShell(pi, { tools: kMetaTools });
  try {
    const names = pi.tools.map((t) => t.name);
    assert.ok(names.includes("exprs_status"), `exprs_status missing: ${names}`);
    assert.ok(names.includes("exprs_meta_list_widgets"));
    assert.ok(names.includes("exprs_spatial_probe"));
    assert.ok(names.includes("exprs_wait_for_execution"));
    // Single registration: exactly one status/wait tool, no duplicates.
    assert.equal(names.filter((n) => n === "exprs_status").length, 1);
    assert.equal(names.filter((n) => n === "exprs_wait_for_execution").length, 1);

    const status = toolByName(pi, "exprs_status");
    const out = await status.execute("call-1", {}, undefined);
    const text = out.content[0].text;
    assert.match(text, /CONNECTED/);
    assert.match(text, /generation 1/);
    assert.match(text, /2 tools in \[meta,spatial/);
    // Machine-readable verdict mirrors the text.
    assert.equal(out.details.connected, true);
    assert.equal(out.details.generation, 1);
    assert.equal(out.details.protocolVersion, "2024-11-05");
    assert.equal(out.details.registeredTools, 2);
    // Default categories (all 11) are reported; meta+spatial from default.
    assert.ok(out.details.categories.includes("meta"));
    assert.ok(out.details.categories.includes("spatial"));
    assert.ok(!out.details.categories.includes("scientific"));
    assert.deepEqual(out.details.unmetDependencies, []);
    // Healthy session: no crash/drift complaint in the drift block.
    assert.equal(out.details.drift.comparisonInFlight, false);
  } finally {
    cleanup(dir);
  }
});

test("healthy status surfaces the scientific opt-in as a next action", async () => {
  const pi = fakePi();
  const { dir } = await loadShell(pi, { tools: kMetaTools });
  try {
    const out = await toolByName(pi, "exprs_status").execute("c", {}, undefined);
    assert.ok(
      out.details.nextActions.some((a) => a.includes("scientific") && a.includes("EXP_RS_TOOL_CATEGORIES")),
      `scientific opt-in hint missing: ${JSON.stringify(out.details.nextActions)}`,
    );
    assert.match(out.content[0].text, /scientific sessions are opt-in/);
  } finally {
    cleanup(dir);
  }
});

test("EXP_RS_TOOL_CATEGORIES=…,scientific bridges the scientific session surface", async () => {
  const pi = fakePi();
  const tools = [
    ...kMetaTools,
    { name: "scientific:agent_session", description: "session surface",
      inputSchema: { type: "object", properties: { action: { type: "string" } }, required: ["action"] } },
    { name: "scientific:context", description: "ctx" },
    { name: "scientific:capabilities", description: "caps" },
  ];
  const { dir } = await loadShell(pi, { tools, categories: "meta,scientific" });
  try {
    const names = pi.tools.map((t) => t.name);
    assert.ok(names.includes("exprs_scientific_agent_session"), names.join(","));
    assert.ok(names.includes("exprs_scientific_context"));
    assert.ok(names.includes("exprs_scientific_capabilities"));
    // spatial was NOT enabled this time: its tool must not register.
    assert.ok(!names.includes("exprs_spatial_probe"));
    const out = await toolByName(pi, "exprs_status").execute("c", {}, undefined);
    assert.ok(out.details.categories.includes("scientific"));
    assert.equal(out.details.registeredTools, 4);
    // Enabled family: no opt-in hint for it anymore.
    assert.ok(!out.details.nextActions.some((a) => a.includes("opt-in")),
      `unexpected opt-in nag: ${JSON.stringify(out.details.nextActions)}`);
  } finally {
    cleanup(dir);
  }
});

test("no binary: the status tool still registers and reports the unmet dependency", async () => {
  const pi = fakePi();
  // Empty EXP_RS_MCP_BIN + a cwd with no build tree → detectBinary() misses.
  const cwd = process.cwd();
  const dir = mkdtempSync(join(tmpdir(), "r29t03-nobin-"));
  process.chdir(dir);
  let loaded;
  try {
    loaded = await loadShell(pi, { bin: "", tools: kMetaTools });
    const names = pi.tools.map((t) => t.name);
    assert.deepEqual(names, ["exprs_status"], `unexpected registrations: ${names}`);
    const out = await toolByName(pi, "exprs_status").execute("c", {}, undefined);
    assert.equal(out.details.connected, false);
    assert.equal(out.details.binary, null);
    assert.equal(out.details.registeredTools, 0);
    assert.ok(out.details.unmetDependencies.some((u) => u.includes("binary not found")),
      JSON.stringify(out.details.unmetDependencies));
    assert.ok(out.details.nextActions.some((n) => n.includes("EXP_RS_MCP_BIN")));
    assert.match(out.content[0].text, /DISCONNECTED/);
  } finally {
    process.chdir(cwd);
    cleanup(loaded?.dir ?? dir);
  }
});

test("failed start (launch failure): typed unmet dependency, no leak, status alive", async () => {
  const pi = fakePi();
  const { dir } = await loadShell(pi, {
    bin: "/nonexistent/r29-t03/no-such-binary",
  });
  try {
    // start() fails (ENOENT) → catch path → only the status tool exists.
    const names = pi.tools.map((t) => t.name);
    assert.deepEqual(names, ["exprs_status"], names.join(","));
    const out = await toolByName(pi, "exprs_status").execute("c", {}, undefined);
    assert.equal(out.details.connected, false);
    assert.ok(out.details.unmetDependencies.some((u) => u.includes("failed to start")),
      JSON.stringify(out.details.unmetDependencies));
    assert.match(out.content[0].text, /failed to start/);
  } finally {
    cleanup(dir);
  }
});

test("piToolName collisions are rejected explicitly, never shadowed", async () => {
  const pi = fakePi();
  // "meta:a b" and "meta:a_b" both sanitize to exprs_meta_a_b.
  const tools = [
    { name: "meta:a b", description: "first" },
    { name: "meta:a_b", description: "second (collides)" },
    { name: "spatial:ok", description: "fine" },
  ];
  const { dir } = await loadShell(pi, { tools });
  try {
    const names = pi.tools.map((t) => t.name);
    assert.equal(names.filter((n) => n === "exprs_meta_a_b").length, 1,
      `collision was not rejected: ${names}`);
    assert.equal(names.filter((n) => n === "exprs_spatial_ok").length, 1);
    const out = await toolByName(pi, "exprs_status").execute("c", {}, undefined);
    assert.equal(out.details.registeredTools, 2);
    assert.equal(out.details.nameCollisions.length, 1);
    assert.match(out.details.nameCollisions[0], /meta:a_b/);
    assert.match(out.content[0].text, /name collision/);
  } finally {
    cleanup(dir);
  }
});
