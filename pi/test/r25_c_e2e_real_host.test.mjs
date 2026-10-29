/**
 * R25-C E2E: the REAL bridge (pi/mcp_bridge.ts) spawning the REAL MCP host
 * binary (tests/surface_mcp_host — the production McpServer on real stdio)
 * over actual OS pipes. Chains exercised (W7, all against production code):
 *
 *   1. success     — initialize → tools/list → list_algorithms carries the
 *                    capability-honesty fields (prerequisites on a real
 *                    descriptor entry, W2).
 *   2. execution   — execute_algorithm rs:surface_noop → execution_id →
 *                    get_execution_status → completed (real TaskCenter).
 *   3. refusal     — get_workflow_status with a path-reaching run_id is
 *                    refused typed (INVALID_PARAMETER) through the real
 *                    dispatch; the host survives.
 *   4. cancel      — execute rs:surface_sleep → cancel_execution → canceled.
 *   5. crash-loop  — run_workflow (sleeping step) → wait for the checkpoint
 *                    → kill the host MID-RUN → a FRESH session (Pi after a
 *                    crash) resolves get_workflow_status(run_id) from the
 *                    CHECKPOINT the dead process left (W3 diagnostics) →
 *                    resume_workflow re-executes the unfinished step to
 *                    completion.
 *
 * Skips honestly (never fails green) when EXP_RS_MCP_BIN does not point at
 * a built surface_mcp_host — the same canary contract as the Windows
 * shebang skips; locally this file runs the real chain.
 *
 * Run: EXP_RS_MCP_BIN=<build>/surface_mcp_host node --test pi/test/r25_c_e2e_real_host.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { McpBridge } from "../mcp_bridge.ts";
import { fileURLToPath } from "node:url";
import { dirname } from "node:path";
const here = dirname(fileURLToPath(import.meta.url));

const bin = process.env.EXP_RS_MCP_BIN ?? "";
// Local convenience: the dev-default preset builds surface_mcp_host under
// <repo>/build-dev/tests/; EXP_RS_MCP_BIN still wins when set.
const repoRoot = join(here, "..", "..");
const defaultBin =
  bin ||
  [
    join(repoRoot, "build-dev", "tests", "surface_mcp_host"),
    join(repoRoot, "build", "tests", "surface_mcp_host"),
  ].find((c) => existsSync(c)) ||
  "";
const binUsable = !!defaultBin;

async function call(bridge, name, arguments_) {
  const result = await bridge.request("tools/call", { name, arguments: arguments_ });
  const text = result?.content?.map((c) => c.text ?? "").join("\n") ?? "";
  return { isError: !!result?.isError, errorCode: result?.errorCode, text };
}

const payload = (text) => JSON.parse(text);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

test(
  "real-binary E2E: capability honesty, execution, typed refusal, cancel, crash→diagnose→resume",
  { skip: binUsable ? false : "EXP_RS_MCP_BIN not set/missing — real-host chain NOT_EXECUTED here" },
  async () => {
    // PRIVATE checkpoint dir: the crash chain must not touch the developer's
    // real ~/.rs_studio/checkpoints. The bridge copies process.env at spawn
    // time, so this MUST be set before the first McpBridge is constructed —
    // every host generation below inherits it.
    const checkpointDir = mkdtempSync(join(tmpdir(), "r25c-e2e-ckpt-"));
    process.env.SICNU_CHECKPOINT_DIR = checkpointDir;

    const bridge = new McpBridge(defaultBin, []);
    try {
      // --- 1. success: handshake + honest capability fields -----------------
      const init = await bridge.request("initialize", {
        protocolVersion: "2024-11-05",
        capabilities: {},
        clientInfo: { name: "r25c-e2e", version: "0" },
      });
      assert.equal(init?.protocolVersion, "2024-11-05");
      assert.ok(bridge.serverInfo?.name, "serverInfo captured from the real handshake");
      assert.equal(bridge.generations, 1);

      const tools = await bridge.request("tools/list", { includeSchemas: true });
      const names = tools?.tools?.map((t) => t.name) ?? [];
      assert.ok(names.length > 50, `expected a large union surface, got ${names.length}`);

      // list_algorithms paginated scan for a known honest entry.
      let spectral = null;
      let cursor = 0;
      for (let page = 0; page < 40 && !spectral; page++) {
        const res = await call(bridge, "list_algorithms", { limit: 500, cursor });
        assert.ok(!res.isError, `list_algorithms failed: ${res.text.slice(0, 200)}`);
        const doc = payload(res.text);
        spectral = (doc.algorithms ?? []).find((a) => a.id === "rs:spectral_index");
        if (doc.nextCursor === undefined || doc.nextCursor < 0 || doc.nextCursor === cursor) break;
        cursor = doc.nextCursor;
      }
      assert.ok(spectral, "rs:spectral_index must be discoverable in list_algorithms");
      assert.ok(
        Array.isArray(spectral.prerequisites) && spectral.prerequisites.length >= 1,
        "real entry carries declared prerequisites (W2 honesty)",
      );

      // --- 2. execution: real TaskCenter round-trip -------------------------
      const started = await call(bridge, "execute_algorithm", {
        algorithm_id: "rs:surface_noop",
        parameters: {},
      });
      assert.ok(!started.isError, `execute failed: ${started.text.slice(0, 300)}`);
      const execId = payload(started.text).execution_id;
      assert.ok(execId, "execution_id returned");
      let status = "";
      for (let i = 0; i < 200 && status !== "completed"; i++) {
        const st = await call(bridge, "get_execution_status", { execution_id: execId });
        status = payload(st.text).status;
        if (status === "failed" || status === "canceled")
          throw new Error(`noop execution ended ${status}: ${st.text.slice(0, 200)}`);
        await sleep(25);
      }
      assert.equal(status, "completed");

      // --- 3. refusal: path-reaching run_id rejected typed ------------------
      const refused = await call(bridge, "get_workflow_status", { run_id: "../escape" });
      assert.ok(refused.isError, "path-reaching run_id must be refused");
      assert.equal(refused.errorCode, "INVALID_PARAMETER");
      assert.equal(bridge.alive, true, "host survives the refusal");

      // --- 4. cancel: sleeping execution canceled through TaskCenter -------
      const sleepy = await call(bridge, "execute_algorithm", {
        algorithm_id: "rs:surface_sleep",
        parameters: { slices: 600 },
      });
      const sleepId = payload(sleepy.text).execution_id;
      assert.ok(sleepId);
      await sleep(300); // let it enter Running
      await call(bridge, "cancel_execution", { execution_id: sleepId });
      let canceled = "";
      for (let i = 0; i < 200 && !canceled; i++) {
        const st = await call(bridge, "get_execution_status", { execution_id: sleepId });
        const s = payload(st.text).status;
        if (s === "canceled") canceled = s;
        else if (s === "completed")
          throw new Error("sleep finished before cancel took effect (increase slices)");
        else await sleep(25);
      }
      assert.equal(canceled, "canceled");

      // --- 5. crash → fresh session → checkpoint diagnostics → resume -------
      // 100 slices = 10 s: long enough to observe Running and kill mid-run,
      // short enough that the resumed step finishes inside the poll budget.
      const wf = await call(bridge, "run_workflow", {
        pipeline: {
          id: "r25c_e2e_crash",
          name: "r25c-e2e-crash",
          steps: [
            {
              id: "sleep1",
              title: "sleep",
              operator: "rs:surface_sleep",
              params: { slices: 100 },
              inputs: [],
            },
          ],
        },
        auto_load: false,
      });
      assert.ok(!wf.isError, `run_workflow failed: ${wf.text.slice(0, 300)}`);
      const wfDoc = payload(wf.text);
      assert.ok(wfDoc.pipeline_id >= 0, "pipeline_id returned");

      // Wait until the step is actually Running (checkpoint persisted) and
      // capture the run_id from the aggregate status.
      let runId = null;
      let stepRunning = false;
      for (let i = 0; i < 200 && !stepRunning; i++) {
        const st = await call(bridge, "get_workflow_status", { pipeline_id: wfDoc.pipeline_id });
        const doc = payload(st.text);
        runId = doc.run_id ?? runId;
        stepRunning = (doc.steps ?? []).some((s) => s.status === "running");
        if (!stepRunning) await sleep(25);
      }
      assert.ok(stepRunning, "workflow step reached Running before the crash");
      assert.ok(runId, "run_id observed in the aggregate status");

      // Kill the host generation mid-run: the bridge's teardown ladder
      // destroys stdin + SIGTERM/SIGKILL — the run is left non-terminal on
      // disk exactly as after a real crash.
      bridge.stop();
      assert.equal(bridge.generations, 1);

      // A fresh bridge = a fresh session (Pi restarting after a crash): the
      // checkpoint outlives the process.
      const bridge2 = new McpBridge(defaultBin, []);
      try {
        const init2 = await bridge2.request("initialize", {
          protocolVersion: "2024-11-05",
          capabilities: {},
          clientInfo: { name: "r25c-e2e-g2", version: "0" },
        });
        assert.ok(init2?.serverInfo, "generation-2 handshake");

        const diag = await call(bridge2, "get_workflow_status", { run_id: runId });
        assert.ok(!diag.isError, `run_id diagnostics failed: ${diag.text.slice(0, 300)}`);
        const d = payload(diag.text);
        assert.equal(d.run_id, runId);
        assert.ok(d.checkpoint_present, "checkpoint present after the crash");
        assert.ok(d.run_state, "checkpointed run_state reported");
        assert.ok(Array.isArray(d.steps) && d.steps.length === 1, "per-step plan from checkpoint");
        assert.equal(typeof d.resumable, "boolean");
        assert.ok(d.resume_hint, "resume hint present");

        // Resume: the unfinished sleeping step re-executes to completion.
        const resumed = await call(bridge2, "resume_workflow", { run_id: runId });
        assert.ok(!resumed.isError, `resume failed: ${resumed.text.slice(0, 300)}`);
        const rDoc = payload(resumed.text);
        assert.ok(rDoc.pipeline_id >= 0, "resumed pipeline id");
        let done = "";
        for (let i = 0; i < 800 && !done; i++) {
          const st = await call(bridge2, "get_workflow_status", {
            pipeline_id: rDoc.pipeline_id,
          });
          const doc = payload(st.text);
          if (doc.isFailed) throw new Error(`resumed run failed: ${st.text.slice(0, 300)}`);
          if (doc.isCompleted) done = "completed";
          else await sleep(50);
        }
        assert.equal(done, "completed", "resumed workflow completed");
      } finally {
        bridge2.stop();
      }
    } finally {
      bridge.stop();
      rmSync(checkpointDir, { recursive: true, force: true });
    }
  },
);
