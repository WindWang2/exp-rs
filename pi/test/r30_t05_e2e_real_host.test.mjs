/**
 * R30-T05 E2E (E1 + E2): the REAL extension shell driving a REAL MCP host
 * binary over actual OS pipes.
 *
 * E1 real-result chain: extension load → tool discovery through the real
 * transport → a real raster inspection (spatial_raster_inspect on a repo
 * fixture) → a real operator execution (rs:surface_noop on the surface
 * host) → exprs_wait_for_execution to a REAL terminal state → a >50k
 * artifact paged through exprs_artifact_read to the last byte and
 * sha256-verified against the local file → the truncated-default-page
 * contract (details.artifact carries path/cursor/checksum, R30-T05 WP04).
 *
 * E2 long-task control: rs:surface_sleep (100ms slices honouring
 * cancellation) → abort mid-wait cancels SERVER-side (status settles
 * canceled; the wait reports abort, not a fabricated canceled) → a wait
 * TIMEOUT stops waiting but does NOT cancel (the execution keeps running
 * → manual cancel → canceled) → the host keeps serving requests after
 * both windows.
 *
 * Skips honestly (never fails green) when no real host binary exists —
 * the skip is NOT_EXECUTED evidence, not a pass.
 *
 * Run: EXP_RS_MCP_BIN=<build>/tests/surface_mcp_host node --test pi/test/r30_t05_e2e_real_host.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { activeBridges } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(here, "..", "..");
const envBin = process.env.EXP_RS_MCP_BIN ?? "";
const defaultBin =
  envBin ||
  [
    join(repoRoot, "build-dev", "tests", "surface_mcp_host"),
    join(repoRoot, "build", "tests", "surface_mcp_host"),
  ].find((c) => existsSync(c)) ||
  "";
const binUsable = !!defaultBin;
const raster = join(repoRoot, "tests", "fixtures", "lab", "hsi_zones.tif");
const rasterUsable = existsSync(raster);

function fakePi() {
  const tools = [];
  return { tools, registerTool(def) { tools.push(def); } };
}
const toolByName = (pi, name) => pi.tools.find((t) => t.name === name);

const kShellEnvKeys = ["EXP_RS_MCP_BIN", "EXP_RS_MCP_ARGS", "EXP_RS_TOOL_CATEGORIES"];

/** The bridge snapshots process.env at SPAWN time, so SICNU_* must stay
 * set for the WHOLE test: the shell env keys are restored here, the
 * server-side sandbox keys are owned by the caller (holdServerEnv). */
async function loadShell(pi) {
  const saved = Object.fromEntries(kShellEnvKeys.map((k) => [k, process.env[k]]));
  try {
    process.env.EXP_RS_MCP_BIN = defaultBin;
    process.env.EXP_RS_MCP_ARGS = "";
    process.env.EXP_RS_TOOL_CATEGORIES = "meta,spatial,rs";
    const shellUrl = pathToFileURL(join(here, "..", "exp-rs-spatial.ts")).href;
    const mod = await import(`${shellUrl}?r30e=${Math.random()}`);
    await mod.default(pi);
  } finally {
    for (const [k, v] of Object.entries(saved)) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  }
}

/** Hold the server-side sandbox env for the whole test; returns a restore. */
const holdServerEnv = (workspace) => {
  const saved = {
    workspace: process.env.SICNU_MCP_WORKSPACE,
    platform: process.env.QT_QPA_PLATFORM,
  };
  process.env.SICNU_MCP_WORKSPACE = workspace;
  process.env.QT_QPA_PLATFORM = "offscreen";
  return () => {
    for (const [k, v] of Object.entries(saved)) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  };
};

function cleanup() {
  for (const b of [...activeBridges]) b.stop();
}

const call = async (pi, name, args, signal) => {
  const out = await toolByName(pi, name).execute("c", args ?? {}, signal);
  return { text: out.content[0].text, details: out.details };
};

test("E1: real host → discovery, raster inspect, execution, wait, paged artifact", { skip: binUsable ? false : "no real MCP host binary — NOT_EXECUTED" }, async () => {
  const workspace = mkdtempSync(join(tmpdir(), "r30t05-e1-"));
  const restoreEnv = holdServerEnv(workspace);
  const pi = fakePi();
  await loadShell(pi);
  try {
    // --- discovery through the real transport -----------------------------
    assert.ok(toolByName(pi, "exprs_status"), "status tool registered");
    assert.ok(toolByName(pi, "exprs_execute_algorithm"), "execute_algorithm bridged");
    assert.ok(toolByName(pi, "exprs_artifact_read"), "artifact_read bridged");
    assert.ok(toolByName(pi, "exprs_wait_for_execution"), "wait tool registered");
    const status = (await call(pi, "exprs_status")).details;
    assert.equal(status.connected, true);
    assert.equal(status.generation, 1);

    // --- real raster inspection (fixture copied INTO the sandbox) ----------
    if (rasterUsable) {
      const inWorkspace = join(workspace, "hsi_zones.tif");
      copyFileSync(raster, inWorkspace); // sandbox containment: only the
      // workspace is readable; the repo fixture rides in like user data
      const inspect = await call(pi, "exprs_spatial_raster_inspect", { path: inWorkspace });
      assert.match(inspect.text, /hsi_zones|size|band|width|height|crs/i);
    } else {
      console.log("E1 raster fixture missing — inspection segment NOT_EXECUTED");
    }

    // --- real operator execution → REAL terminal state ---------------------
    const started = await call(pi, "exprs_execute_algorithm", {
      algorithm_id: "rs:surface_noop",
    });
    const startedDoc = JSON.parse(started.text);
    const executionId = startedDoc.execution_id;
    assert.ok(executionId, `execution_id present: ${started.text.slice(0, 200)}`);
    const final = await call(pi, "exprs_wait_for_execution", {
      execution_id: executionId,
      timeout_seconds: 30,
      poll_interval_ms: 200,
    });
    assert.match(final.text, /completed/);
    assert.equal(final.details.status, "completed");

    // --- >50k artifact: page to the last byte and sha256-verify ------------
    const body = Buffer.from(
      "遥感辐射定标光谱段数据 sample payload — ".repeat(2200), // ~150 KB, multibyte
      "utf8",
    );
    const artifactPath = join(workspace, "big-result.json");
    writeFileSync(artifactPath, body);
    const expectedSha = createHash("sha256").update(body).digest("hex");

    // The DEFAULT page (no length) is 256 KiB > the whole file: one read,
    // but through Pi it exceeds the 50k budget → the R30-T05 continuation
    // contract kicks in on the truncated projection.
    const whole = await call(pi, "exprs_artifact_read", { path: artifactPath });
    assert.equal(whole.details.resultTruncated, true);
    assert.ok(whole.details.artifact, "details.artifact survives truncation");
    assert.equal(whole.details.artifact.sha256, expectedSha);
    assert.equal(whole.details.artifact.size_bytes, body.length);
    assert.match(whole.details.resultContinuation, /exprs_artifact_read/);

    // Page the WHOLE file in 40k-char pages: reassemble and verify bytes.
    const pages = [];
    let offset = 0;
    let guard = 0;
    for (;;) {
      const page = await call(pi, "exprs_artifact_read", {
        path: artifactPath, offset, length: 40_000,
      });
      const doc = JSON.parse(page.text);
      assert.equal(doc.sha256, expectedSha);
      assert.equal(doc.path, artifactPath);
      pages.push(doc.content);
      if (!doc.truncated) break;
      assert.ok(doc.next_offset > offset, "cursor strictly advances");
      offset = doc.next_offset;
      assert.ok(++guard < 40, "page guard");
    }
    const reassembled = Buffer.from(pages.join(""), "utf8");
    assert.equal(reassembled.length, body.length);
    assert.equal(
      createHash("sha256").update(reassembled).digest("hex"),
      expectedSha,
      "reassembled pages are byte-identical to the original file",
    );
  } finally {
    restoreEnv();
    cleanup();
    rmSync(workspace, { recursive: true, force: true });
  }
});

test("E2: long-task control — abort cancels server-side; timeout does not; host survives", { skip: binUsable ? false : "no real MCP host binary — NOT_EXECUTED" }, async () => {
  const workspace = mkdtempSync(join(tmpdir(), "r30t05-e2-"));
  const restoreEnv = holdServerEnv(workspace);
  const pi = fakePi();
  await loadShell(pi);
  try {
    // --- window 1: abort mid-wait cancels the REAL execution ----------------
    const s1 = JSON.parse((await call(pi, "exprs_execute_algorithm", {
      algorithm_id: "rs:surface_sleep", parameters: { slices: 200 },
    })).text);
    const controller = new AbortController();
    const waiting = call(pi, "exprs_wait_for_execution", {
      execution_id: s1.execution_id, timeout_seconds: 60, poll_interval_ms: 200,
    }, controller.signal);
    setTimeout(() => controller.abort(new Error("user cancelled")), 900);
    await assert.rejects(waiting, /abort/i);
    // The cancellation is the SERVER's fact, not the client's claim: poll
    // the real status until it settles canceled (slices stop within ~100ms).
    let settled = null;
    for (let i = 0; i < 100 && !settled; i++) {
      const doc = JSON.parse((await call(pi, "exprs_get_execution_status", {
        execution_id: s1.execution_id,
      })).text);
      if (doc.status === "canceled" || doc.status === "completed") settled = doc.status;
      else await new Promise((r) => setTimeout(r, 200));
    }
    assert.equal(settled, "canceled", "abort must reach the real execution");

    // --- window 2: wait timeout stops WAITING only ---------------------------
    const s2 = JSON.parse((await call(pi, "exprs_execute_algorithm", {
      algorithm_id: "rs:surface_sleep", parameters: { slices: 150 },
    })).text);
    await assert.rejects(
      call(pi, "exprs_wait_for_execution", {
        execution_id: s2.execution_id, timeout_seconds: 1, poll_interval_ms: 200,
      }),
      /timed out/i,
    );
    // The execution was NOT killed by the timeout: it is still running (or
    // at most settling) — the product contract distinguishes the two.
    const afterTimeout = JSON.parse((await call(pi, "exprs_get_execution_status", {
      execution_id: s2.execution_id,
    })).text);
    assert.ok(
      ["running", "completed"].includes(afterTimeout.status),
      `timeout must not cancel; status was ${afterTimeout.status}`,
    );
    // Manual cancel settles it, proving the control channel stayed live.
    await call(pi, "exprs_cancel_execution", { execution_id: s2.execution_id });
    let settled2 = null;
    for (let i = 0; i < 100 && !settled2; i++) {
      const doc = JSON.parse((await call(pi, "exprs_get_execution_status", {
        execution_id: s2.execution_id,
      })).text);
      if (doc.status === "canceled" || doc.status === "completed") settled2 = doc.status;
      else await new Promise((r) => setTimeout(r, 200));
    }
    assert.equal(settled2, "canceled");

    // --- the host keeps serving fresh work after both windows ---------------
    const s3 = JSON.parse((await call(pi, "exprs_execute_algorithm", {
      algorithm_id: "rs:surface_noop",
    })).text);
    const done = await call(pi, "exprs_wait_for_execution", {
      execution_id: s3.execution_id, timeout_seconds: 20, poll_interval_ms: 200,
    });
    assert.match(done.text, /completed/);
  } finally {
    restoreEnv();
    cleanup();
    rmSync(workspace, { recursive: true, force: true });
  }
});
