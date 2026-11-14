/**
 * R30-T05 WP04: bounded results must carry an EXECUTABLE continuation.
 *
 * The server's paged artifact_read envelope serializes alphabetically
 * (QVariantMap → compact JSON), so the giant "content" lands FIRST and the
 * actionable metadata (path/sha256/next_offset/…) LAST. After the 50k
 * tail-keeping cut the document no longer parses — details used to carry
 * only a generic artifact_read hint with no path, cursor or checksum,
 * pointing at continuation parameters that did not exist. These tests pin
 * the extraction contract against the REAL envelope shape.
 *
 * Run: node --test pi/test/r30_t05_details_continuation.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { activeBridges, MAX_RESULT_CHARS } from "../mcp_bridge.ts";

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
  const dir = mkdtempSync(join(tmpdir(), "r30t05-cont-"));
  const toolsFile = join(dir, "tools.json");
  writeFileSync(toolsFile, JSON.stringify([
    { name: "meta:probe", inputSchema: { type: "object", properties: {} } },
  ]));
  const saved = Object.fromEntries(kEnvKeys.map((k) => [k, process.env[k]]));
  try {
    process.env.EXP_RS_MCP_BIN = fakeServer;
    process.env.EXP_RS_MCP_ARGS = `--tools-file ${toolsFile} ${join(dir, "counter")}`;
    process.env.EXP_RS_TOOL_CATEGORIES = "meta";
    const mod = await import(`${shellUrl}?r30c=${Math.random()}`);
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

/** The server's artifact_read envelope shape: alphabetical QVariantMap
 * serialization puts "content" FIRST, the paging metadata LAST. */
function artifactEnvelope(content, overrides = {}) {
  return {
    content,
    encoding: "text",
    length: content.length,
    next_offset: 0 + content.length,
    offset: 0,
    path: "/workspace/runs/r1/out/result.json",
    sha256: createHash("sha256").update("whole-file-bytes").digest("hex"),
    size_bytes: 262370,
    truncated: true,
    ...overrides,
  };
}

test("truncated artifact envelope: details keep path/cursor/checksum and an executable continuation", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    const envelope = artifactEnvelope("y".repeat(MAX_RESULT_CHARS + 12_000));
    const out = await callProbe(pi, { json_result: envelope });
    assert.equal(out.details.resultTruncated, true);
    // Dropped chars count the WHOLE document (envelope metadata rides the
    // head that the tail-keeping cut removes).
    assert.equal(out.details.resultDroppedChars, JSON.stringify(envelope).length - MAX_RESULT_CHARS);
    const artifact = out.details.artifact;
    assert.ok(artifact, "details.artifact must survive the truncation");
    assert.equal(artifact.path, "/workspace/runs/r1/out/result.json");
    assert.equal(artifact.sha256, envelope.sha256);
    assert.equal(artifact.size_bytes, 262370);
    assert.equal(artifact.next_offset, envelope.next_offset);
    assert.equal(artifact.truncated, true);
    // The continuation is EXECUTABLE: real tool name, real path, real
    // resume offset, and a page size that fits the budget.
    assert.match(out.details.resultContinuation, /exprs_artifact_read/);
    assert.match(out.details.resultContinuation, /result\.json/);
    assert.match(out.details.resultContinuation, new RegExp(`"offset": ${envelope.next_offset}`));
    assert.match(out.details.resultContinuation, /sha256/);
    // R2-N1: the head-cut note must report the HONEST count —
    // resultDroppedChars (the serialized document head: metadata + the
    // start of the content), never the whole page span — and name the
    // lossless re-read offset (the page's own offset, 0 here).
    assert.match(
      out.details.resultContinuation,
      new RegExp(`first ${out.details.resultDroppedChars} chars of the document head`),
    );
    assert.match(out.details.resultContinuation, /re-read from offset 0/);
  } finally {
    cleanup(dir);
  }
});

test("JSON embedded inside content cannot forge the envelope (escaped quotes)", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    // The content string itself embeds a JSON document with its own
    // path/sha256-looking fields — escaped quotes mean the envelope-level
    // regex must not match them, and the real envelope tail still wins.
    const embedded = JSON.stringify({
      path: "/evil/inside",
      sha256: "0".repeat(64),
      size_bytes: 1,
    });
    const envelope = artifactEnvelope(
      ("x".repeat(MAX_RESULT_CHARS + 5_000)) + embedded,
    );
    const out = await callProbe(pi, { json_result: envelope });
    assert.equal(out.details.artifact.path, "/workspace/runs/r1/out/result.json");
    assert.notEqual(out.details.artifact.sha256, "0".repeat(64));
  } finally {
    cleanup(dir);
  }
});

test("CJK content truncates with the envelope intact (multibyte payload)", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    const cjk = "光谱反射率".repeat(11_000); // 3 bytes/char, > 50k chars
    const envelope = artifactEnvelope(cjk);
    const out = await callProbe(pi, { json_result: envelope });
    assert.equal(out.details.resultTruncated, true);
    assert.equal(out.details.artifact.path, "/workspace/runs/r1/out/result.json");
    assert.equal(out.details.artifact.next_offset, cjk.length);
  } finally {
    cleanup(dir);
  }
});

test("structuredContent mirror obeys the budget: huge strings are capped, not copied", async () => {
  const pi = fakePi();
  const dir = await loadShell(pi);
  try {
    const out = await callProbe(pi, {
      structured: { path: "/ws/a.json", payload: "z".repeat(MAX_RESULT_CHARS + 40_000) },
    });
    // Short metadata mirrors verbatim...
    assert.equal(out.details.path, "/ws/a.json");
    // ...but a 90k-char string must not ride details past the 50k budget.
    assert.ok(typeof out.details.payload === "string");
    assert.ok(out.details.payload.length < MAX_RESULT_CHARS, `payload mirror was ${out.details.payload.length} chars`);
    assert.match(out.details.payload, /truncated/);
  } finally {
    cleanup(dir);
  }
});
