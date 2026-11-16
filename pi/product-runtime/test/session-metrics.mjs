// Measure SDK assembly only: no prompt, model connection, host tool or GUI work.
import { createControlledSession } from "../controlled-session.mjs";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";

const modules = process.env.SICNU_TEST_PI_NODE_MODULES;
if (!modules || !path.isAbsolute(modules)) throw new Error("Set an absolute SICNU_TEST_PI_NODE_MODULES");
const temporary = path.resolve(tmpdir());
const workspace = await mkdtemp(path.join(temporary, "sicnu-pi-p0-metrics-"));
for (const key of Object.keys(process.env)) {
  if (!["SystemRoot", "SYSTEMROOT", "WINDIR"].includes(key)) delete process.env[key];
}
Object.assign(process.env, {
  HOME: workspace, USERPROFILE: workspace, APPDATA: workspace, LOCALAPPDATA: workspace,
  TEMP: workspace, TMP: workspace, PI_OFFLINE: "1", PI_TELEMETRY: "0", PATH: path.dirname(process.execPath),
});
globalThis.fetch = async () => { throw new Error("Metrics probe must not make model or catalog requests"); };
let controlled;
try {
  const started = performance.now();
  controlled = await createControlledSession({
    nodeModules: modules, cwd: workspace,
    profile: { baseUrl: "http://127.0.0.1:9/v1", model: "fixture-metrics", apiKey: "fixture-memory-only", contextWindow: 32768, maxTokens: 1024 },
    tools: [], onToolRequest: async () => { throw new Error("No tool expected"); },
    systemPrompt: "Product P0 resource measurement; no model request.",
  });
  const memory = process.memoryUsage();
  console.log(JSON.stringify({
    node: process.version, sdk: controlled.diagnostics().version,
    sessionReadyMs: performance.now() - started, rssBytes: memory.rss, heapUsedBytes: memory.heapUsed,
    peakRssBytes: process.resourceUsage().maxRSS * 1024, modelRequests: 0, tools: controlled.diagnostics().tools,
  }));
} finally {
  controlled?.session.dispose();
  if (path.dirname(path.resolve(workspace)) !== temporary || !path.basename(workspace).startsWith("sicnu-pi-p0-metrics-")) {
    throw new Error("Unexpected metrics cleanup target");
  }
  await rm(workspace, { recursive: true, force: true });
}
