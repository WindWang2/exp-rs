/**
 * R30-T05 E2E (E3): server restart and durable evidence, through the REAL
 * extension shell.
 *
 * Segment A (surface host — has the cancellable sleeping operator): a
 * workflow running through the extension's bridge is killed MID-RUN via
 * killNow(); the next extension call lazily respawns (generation 2) — the
 * R30-T05 dispatch gate must let the call through only after generation
 * 2's catalog comparison lands (unchanged catalog) — then the durable
 * checkpoint answers get_workflow_status and resume_workflow completes the
 * unfinished step.
 *
 * Segment B (production sicnu_geo_rs --mcp, when built): a session id this
 * host never launched answers UNKNOWN_SESSION pointing at reconcile; two
 * REAL on-disk journals reconcile to their contract verdicts — the
 * pre-plan journal is RESUMABLE, the post-plan journal with a recorded
 * run submission is refused (RESUME_PAST_PLAN_SEAM) with the
 * duplicate-submit hazard and the submitted run ids visible. This pins
 * the "durable evidence exists but re-submission is not allowed"
 * distinction against the production driver.
 *
 * Skips honestly when the host binaries are missing (NOT_EXECUTED).
 *
 * Run: node --test pi/test/r30_t05_e2e_restart_durable.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { activeBridges } from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(here, "..", "..");
const pick = (...candidates) => candidates.find((c) => existsSync(c)) || "";
const surfaceHost = process.env.EXP_RS_MCP_BIN && existsSync(process.env.EXP_RS_MCP_BIN)
  ? process.env.EXP_RS_MCP_BIN
  : pick(join(repoRoot, "build-dev", "tests", "surface_mcp_host"));
const productionBin = pick(
  join(repoRoot, "build-dev", "sicnu_geo_rs"),
  join(repoRoot, "build", "sicnu_geo_rs"),
);

function fakePi() {
  const tools = [];
  return { tools, registerTool(def) { tools.push(def); } };
}
const toolByName = (pi, name) => pi.tools.find((t) => t.name === name);

const kShellEnvKeys = ["EXP_RS_MCP_BIN", "EXP_RS_MCP_ARGS", "EXP_RS_TOOL_CATEGORIES"];

/** The bridge snapshots process.env at SPAWN time, so SICNU_* must stay
 * set for the WHOLE test (restored by the returned closure, not here). */
async function loadShell(pi, bin, categories) {
  const saved = Object.fromEntries(kShellEnvKeys.map((k) => [k, process.env[k]]));
  try {
    process.env.EXP_RS_MCP_BIN = bin;
    process.env.EXP_RS_MCP_ARGS = "";
    process.env.EXP_RS_TOOL_CATEGORIES = categories;
    const shellUrl = pathToFileURL(join(here, "..", "exp-rs-spatial.ts")).href;
    const mod = await import(`${shellUrl}?r30r=${Math.random()}`);
    await mod.default(pi);
  } finally {
    for (const [k, v] of Object.entries(saved)) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  }
}

const holdServerEnv = (workspace, checkpointDir) => {
  const saved = {
    workspace: process.env.SICNU_MCP_WORKSPACE,
    checkpoint: process.env.SICNU_CHECKPOINT_DIR,
    platform: process.env.QT_QPA_PLATFORM,
  };
  process.env.SICNU_MCP_WORKSPACE = workspace;
  if (checkpointDir) process.env.SICNU_CHECKPOINT_DIR = checkpointDir;
  process.env.QT_QPA_PLATFORM = "offscreen";
  return () => {
    for (const [k, v] of Object.entries(saved)) {
      if (v === undefined) delete process.env[k];
      else process.env[k] = v;
    }
  };
};

const cleanup = () => {
  for (const b of [...activeBridges]) b.stop();
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const call = async (pi, name, args, signal) => {
  const out = await toolByName(pi, name).execute("c", args ?? {}, signal);
  return { text: out.content[0].text, details: out.details };
};
const doc = (t) => JSON.parse(t);

test("E3-A: kill mid-run → extension respawn (gate) → durable checkpoint → resume", { skip: surfaceHost ? false : "no surface host binary — NOT_EXECUTED" }, async () => {
  const workspace = mkdtempSync(join(tmpdir(), "r30t05-e3a-ws-"));
  const checkpointDir = mkdtempSync(join(tmpdir(), "r30t05-e3a-ckpt-"));
  const restoreEnv = holdServerEnv(workspace, checkpointDir);
  const pi = fakePi();
  await loadShell(pi, surfaceHost, "meta,rs");
  try {
    const started = await call(pi, "exprs_run_workflow", {
      pipeline: {
        id: "r30t05_e3a_crash",
        name: "r30t05-e3a-crash",
        steps: [{
          id: "sleep1", title: "sleep", operator: "rs:surface_sleep",
          params: { slices: 100 }, inputs: [],
        }],
      },
      auto_load: false,
    });
    const startDoc = doc(started.text);
    assert.ok(startDoc.pipeline_id >= 0, `run_workflow failed: ${started.text.slice(0, 300)}`);

    // Reach Running so the checkpoint holds a non-terminal run, then kill
    // the server generation under the extension's feet.
    let runId = null;
    let stepRunning = false;
    for (let i = 0; i < 200 && !stepRunning; i++) {
      const st = doc((await call(pi, "exprs_get_workflow_status", {
        pipeline_id: startDoc.pipeline_id,
      })).text);
      runId = st.run_id ?? runId;
      stepRunning = (st.steps ?? []).some((s) => s.status === "running");
      if (!stepRunning) await sleep(25);
    }
    assert.ok(stepRunning, "step reached Running before the kill");
    assert.ok(runId, "run_id observed");

    const bridge = [...activeBridges].find((b) => b.bin === surfaceHost);
    assert.ok(bridge, "extension bridge discoverable via activeBridges");
    const genBefore = bridge.generations;
    bridge.killNow();
    for (let i = 0; i < 200 && bridge.alive; i++) await sleep(25);
    assert.equal(bridge.alive, false, "server generation killed mid-run");

    // The next extension call lazily respawns: the dispatch gate must wait
    // for generation 2's catalog comparison (unchanged catalog → pass).
    const diagText = await call(pi, "exprs_get_workflow_status", { run_id: runId });
    const diag = doc(diagText.text);
    assert.ok(diag.checkpoint_present, `durable checkpoint after the crash: ${diagText.text.slice(0, 300)}`);
    assert.equal(diag.run_id, runId);
    assert.equal(bridge.generations, genBefore + 1, "a fresh generation served the call");
    const status = (await call(pi, "exprs_status")).details;
    assert.equal(status.generation, genBefore + 1);
    assert.deepEqual(status.drift.changed, [], "unchanged catalog: no drift, dispatch allowed");

    // Resume: the unfinished step re-executes to completion on gen 2. The
    // dead holder's flock release can lag; the typed "owned by a live
    // process" refusal during that window is correct — retry bounded.
    let resumed = null;
    for (let attempt = 0; attempt < 20 && !resumed; attempt++) {
      try {
        resumed = await call(pi, "exprs_resume_workflow", { run_id: runId });
      } catch (err) {
        if (!/owned by a live process/i.test(String(err?.message ?? err))) throw err;
        await sleep(500);
      }
    }
    assert.ok(resumed, "resume succeeded after the dead holder's lock released");
    const rDoc = doc(resumed.text);
    let done = "";
    for (let i = 0; i < 800 && !done; i++) {
      const st = doc((await call(pi, "exprs_get_workflow_status", {
        pipeline_id: rDoc.pipeline_id,
      })).text);
      if (st.isFailed) throw new Error(`resumed run failed: ${JSON.stringify(st).slice(0, 300)}`);
      if (st.isCompleted) done = "completed";
      else await sleep(50);
    }
    assert.equal(done, "completed", "resumed workflow completed after the restart");
  } finally {
    restoreEnv();
    cleanup();
    rmSync(workspace, { recursive: true, force: true });
    rmSync(checkpointDir, { recursive: true, force: true });
  }
});

test("E3-B: production host — UNKNOWN_SESSION + journal reconcile verdicts", { skip: productionBin ? false : "no production binary — NOT_EXECUTED" }, async () => {
  const workspace = mkdtempSync(join(tmpdir(), "r30t05-e3b-ws-"));
  const journals = join(workspace, "journals");
  mkdirSync(journals, { recursive: true });
  writeFileSync(join(journals, "r30t05-pre.json"), JSON.stringify({
    schema_version: "1.0",
    session_id: "r30t05-pre",
    next_seq: 2,
    entries: [
      { seq: 1, at: 1, stage: "goal_normalization", event: "stage_enter", payload: {} },
      { seq: 2, at: 2, stage: "data_state_snapshot", event: "stage_enter", payload: {} },
    ],
  }));
  writeFileSync(join(journals, "r30t05-post.json"), JSON.stringify({
    schema_version: "1.0",
    session_id: "r30t05-post",
    next_seq: 3,
    entries: [
      { seq: 1, at: 1, stage: "goal_normalization", event: "stage_enter", payload: {} },
      { seq: 2, at: 2, stage: "plan_request", event: "stage_enter", payload: {} },
      {
        seq: 3, at: 3, stage: "execute", event: "stage_enter", payload: {},
        decision: {
          schema_version: "1.0", decision_id: "d-e3b-1", session_id: "r30t05-post",
          stage: "execute", recorded_at: 3,
          inputs: { run_id: "run-e3b-submitted" }, alternatives: [],
          selected: { action: "run" }, reason: "submitted the plan",
          evidence: [], policy: {},
        },
      },
    ],
  }));
  const restoreEnv = holdServerEnv(workspace);
  const pi = fakePi();
  await loadShell(pi, productionBin, "meta,scientific");
  try {
    assert.ok(toolByName(pi, "exprs_scientific_agent_session"),
      "scientific:agent_session bridged from the production host");

    // --- a session this host never launched --------------------------------
    const unknown = doc((await call(pi, "exprs_scientific_agent_session", {
      action: "status", session_id: "r30t05-pre",
    })).text);
    assert.equal(unknown.error, "UNKNOWN_SESSION");
    assert.match(unknown.message, /reconcile/i);

    // --- pre-plan journal: resume allowed -----------------------------------
    const pre = doc((await call(pi, "exprs_scientific_agent_session", {
      action: "reconcile", journal_directory: journals, session_id: "r30t05-pre",
    })).text);
    // The driver nests the reconciler verdict under `reconcile`.
    assert.equal(pre.reconcile.reason_code, "RESUMABLE", JSON.stringify(pre).slice(0, 300));
    assert.equal(pre.reconcile.resumable, true);
    assert.equal(pre.reconcile.duplicate_submit_risk, false);

    // --- post-plan journal with a recorded submission: NO resume, hazard on -
    const post = doc((await call(pi, "exprs_scientific_agent_session", {
      action: "reconcile", journal_directory: journals, session_id: "r30t05-post",
    })).text);
    assert.equal(post.reconcile.reason_code, "RESUME_PAST_PLAN_SEAM");
    assert.equal(post.reconcile.resumable, false);
    assert.equal(post.reconcile.duplicate_submit_risk, true);
    assert.ok(post.reconcile.submitted_run_ids.includes("run-e3b-submitted"),
      "the submitted run id is visible for duplicate-submit avoidance");

    // --- the same journal reconciles to the SAME facts twice (no flip) ------
    const postAgain = doc((await call(pi, "exprs_scientific_agent_session", {
      action: "reconcile", journal_directory: journals, session_id: "r30t05-post",
    })).text);
    assert.equal(postAgain.reconcile.reason_code, post.reconcile.reason_code);
    assert.equal(postAgain.reconcile.duplicate_submit_risk, true);
  } finally {
    restoreEnv();
    cleanup();
    rmSync(workspace, { recursive: true, force: true });
  }
});
