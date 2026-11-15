import path from "node:path";
import { pathToFileURL } from "node:url";

const root = process.argv[2];
const started = performance.now();
const mark = (phase) => {
  process.stderr.write(`[SICNU_PI_AUTH_PROBE] ${phase} elapsed_ms=${Math.round(performance.now() - started)}\n`);
};
mark("imports_started");
const { ModelRuntime } = await import(pathToFileURL(path.join(root, "@earendil-works/pi-coding-agent/dist/index.js")).href);
mark("sdk_imported");
const { InMemoryCredentialStore, InMemoryModelsStore } = await import(pathToFileURL(path.join(root, "@earendil-works/pi-ai/dist/index.js")).href);
mark("imports_complete");
const credentials = new InMemoryCredentialStore();
const hostProvider = "sicnu-host-fixture";
await credentials.modify(hostProvider, async () => ({ type: "api_key", key: "fixture-host-memory-key" }));
let envQueries = 0;
let fileQueries = 0;
const runtime = await ModelRuntime.create({
  credentials,
  modelsPath: null,
  modelsStore: new InMemoryModelsStore(),
  allowModelNetwork: false,
  refreshOnCreate: false,
  authContext: {
    async env() { envQueries += 1; return undefined; },
    async fileExists() { fileQueries += 1; return false; },
  },
});
mark("create_complete");

runtime.registerProvider(hostProvider, {
  api: "openai-completions",
  baseUrl: "http://127.0.0.1:9/v1",
  models: [{
    id: "fixture-model",
    name: "Fixture model",
    reasoning: false,
    input: ["text"],
    cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0 },
    contextWindow: 4096,
    maxTokens: 128,
  }],
});
mark("register_complete");
await runtime.refresh({ allowNetwork: false });
mark("refresh_complete");
const openaiAvailable = (await runtime.getAvailable("openai")).length;
const vertexAvailable = (await runtime.getAvailable("google-vertex")).length;
const hostModelAvailable = (await runtime.getAvailable(hostProvider)).some((model) => model.id === "fixture-model");
await runtime.settleCredentialWrites();
console.log(JSON.stringify({
  openaiAvailable,
  vertexAvailable,
  hostModelAvailable,
  envQueries,
  fileQueries,
  memoryCredentialCount: (await credentials.list()).length,
}));
