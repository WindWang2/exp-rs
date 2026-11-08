/**
 * R29-T03 WP03: machine-readable details, retryable passthrough, truncation
 * transparency, resource projection, and tools/list pagination consumption.
 *
 * Executes the REAL shell (default export) against the REAL transport and
 * the fake MCP server — the registered tool's execute() is invoked and its
 * thrown/returned shapes asserted (no source regexes).
 *
 * Run: node --test pi/test/r29_t03_details_passthrough.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { activeBridges, listAllTools, MAX_RESULT_CHARS, McpBridge } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const fakeServer = join(here, "fake_mcp_server.mjs");
const shellUrl = pathToFileURL(join(here, "..", "exp-rs-spatial.ts")).href;

function fakePi() {
  const tools = [];
  return { tools, registerTool(def) { tools.push(def); } };
}
const toolByName = (pi, name) => pi.tools.find((t) => t.name === name);

const kEnvKeys = ["EXP_RS_MCP_BIN", "EXP_RS_MCP_ARGS", "EXP_RS_TOOL_CATEGORIES"];

async function loadShell(pi) {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-details-"));
  const counter = join(dir, "counter");
  const toolsFile = join(dir, "tools.json");
  writeFileSync(toolsFile, JSON.stringify([
    { name: "meta:probe", description: "p",
      inputSchema: { type: "object", properties: {} } },
  ]));
  const saved = Object.fromEntries(kEnvKeys.map((k) => [k, process.env[k]]));
  try {
    process.env.EXP_RS_MCP_BIN = fakeServer;
    process.env.EXP_RS_MCP_ARGS = `--tools-file ${toolsFile} ${counter}`;
    process.env.EXP_RS_TOOL_CATEGORIES = "meta";
    const mod = await import(`${shellUrl}?r29d=${Math.random()}`);
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

const callProbe = (pi, args) =>
  toolByName(pi, "exprs_meta_probe").execute("call-1", args, undefined);

test("text-JSON success results are mirrored into machine-readable details", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    const out = await callProbe(pi, {
      json_result: { status: "delivered", artifacts: [{ id: "a1" }], steps: 7 },
    });
    assert.equal(out.details.status, "delivered");
    assert.deepEqual(out.details.artifacts, [{ id: "a1" }]);
    assert.equal(out.details.steps, 7);
    // The text projection stays the whole JSON document.
    assert.deepEqual(JSON.parse(out.content[0].text), {
      status: "delivered", artifacts: [{ id: "a1" }], steps: 7,
    });
    // No truncation keys when nothing was cut.
    assert.equal(out.details.resultTruncated, undefined);
  } finally {
    cleanup(dir);
  }
});

test("native structuredContent is preserved verbatim over the text projection", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    const out = await callProbe(pi, { structured: { verdict: "PASS", checks: [1, 2] } });
    assert.equal(out.details.verdict, "PASS");
    assert.deepEqual(out.details.checks, [1, 2]);
    assert.equal(out.content[0].text, "short projection");
  } finally {
    cleanup(dir);
  }
});

test("resource content items project to their text, not the empty string", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    const out = await callProbe(pi, { resource_uri: "file:///tmp/x.json" });
    assert.equal(out.content[0].text, "resource body");
  } finally {
    cleanup(dir);
  }
});

test("error results re-throw with errorCode, errorCategory AND retryable", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    // retryable:true — a server-side retryable typed refusal.
    await assert.rejects(
      callProbe(pi, { error_shape: true, retryable: true }),
      (err) => {
        assert.equal(err.errorCode, "E_TEST_SHAPE");
        assert.equal(err.errorCategory, "test");
        assert.equal(err.retryable, true, "retryable must ride the throw (pi-adapter.md contract)");
        return true;
      },
    );
    // retryable:false (default) — the fatal variant stays fatal.
    await assert.rejects(
      callProbe(pi, { error_shape: true }),
      (err) => err.retryable === false,
    );
  } finally {
    cleanup(dir);
  }
});

test("oversized results: tail kept, cut announced, details carry the continuation", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    const out = await callProbe(pi, { huge: MAX_RESULT_CHARS + 10_000 });
    const text = out.content[0].text;
    assert.match(text, /characters truncated, tail kept/);
    assert.ok(text.length < MAX_RESULT_CHARS + 200);
    assert.equal(out.details.resultTruncated, true);
    assert.equal(out.details.resultDroppedChars, 10_000);
    assert.match(out.details.resultContinuation, /exprs_artifact_read/);
    // A truncated non-JSON document must NOT fabricate parsed details.
    assert.equal(out.details.status, undefined);
  } finally {
    cleanup(dir);
  }
});

test("listAllTools consumes the server's opt-in pagination to the end", async () => {
  const dir = mkdtempSync(join(tmpdir(), "r29t03-page-"));
  const counter = join(dir, "counter");
  const toolsFile = join(dir, "tools.json");
  writeFileSync(toolsFile, JSON.stringify(
    Array.from({ length: 5 }, (_, i) => ({ name: `meta:t${i}` })),
  ));
  const bridge = new McpBridge(fakeServer, ["--tools-file", toolsFile, counter]);
  try {
    // Single page when no limit is sent (the shell's registration shape).
    const whole = await listAllTools(bridge);
    assert.equal(whole.length, 5);
    // Paged when pageSize opts in: 2 + 2 + 1 across three pages.
    const paged = await listAllTools(bridge, { pageSize: 2 });
    assert.deepEqual(paged.map((t) => t.name), ["meta:t0", "meta:t1", "meta:t2", "meta:t3", "meta:t4"]);
  } finally {
    bridge.stop();
    rmSync(dir, { recursive: true, force: true });
  }
});
