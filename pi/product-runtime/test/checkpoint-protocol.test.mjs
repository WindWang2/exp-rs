// Uses the exact prepared SDK and loopback SSE only.
import assert from "node:assert/strict";
import { test } from "node:test";
import { createHash } from "node:crypto";
import { mkdtemp, mkdir, rm, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { PrivateSession, ProtocolError } from "../protocol.mjs";
import { createControlledSession } from "../controlled-session.mjs";
import { parseCheckpoint, MAX_CHECKPOINT_BYTES } from "../checkpoint.mjs";
import { loadInstallation } from "../main.mjs";
import { openAiFixture, lastUserText } from "./fixtures/openai-server.mjs";

assert.ok(process.env.SICNU_TEST_PI_NODE_MODULES, "Set SICNU_TEST_PI_NODE_MODULES to the prepared patched node_modules root");
const nodeModules = resolve(process.env.SICNU_TEST_PI_NODE_MODULES);
const sdkReceiptBytes = await readFile(join(nodeModules, "../../../../share/sicnu-geo-rs/pi-runtime.json"));
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const metadata = { nodeVersion: "24.22.3", piVersion: "1.5.2", sdkReceiptSha256: sha256(sdkReceiptBytes) };
const systemPrompt = "Use only the current approved native host tools.";
const fixtureKey = 'fixture-credential-quote"slash\\-not-user';
const tool = { name: "exprs_python_run_script", canonicalId: "python:run_script", description: "Approved host tool.",
  parameters: { type: "object", properties: { input: { type: "string" }, note: { type: "string" } }, required: ["input"], additionalProperties: false } };
const binding = { conversationId: "conversation-1", projectId: "project-1", systemPromptSha256: sha256(systemPrompt) };
const bindings = (value = binding) => ({ ...value, toolRevision: "catalog-1", sdkReceiptSha256: metadata.sdkReceiptSha256, allowedToolNames: [tool.name] });
const checkpointRequest = (checkpointId, extra = {}) => ({ checkpointId, hostSettled: true, hostIdle: true, ...extra });
const pause = () => new Promise((resolve) => setTimeout(resolve, 5));
async function waitFor(predicate, timeout = 20000) {
  const end = performance.now() + timeout;
  while (!predicate()) { if (performance.now() >= end) throw new Error("P3 fixture timed out"); await pause(); }
}

async function fixture(t, handler = () => ({ text: "Completed fixture." }), options = {}) {
  const root = await mkdtemp(join(tmpdir(), "Pi P3 wrapper 中文 "));
  const cwd = join(root, "owned"), restoredCwd = join(root, "restored"), home = join(root, "home");
  await Promise.all([mkdir(cwd), mkdir(restoredCwd), mkdir(home)]);
  const changed = { HOME: home, USERPROFILE: home, APPDATA: join(home, "AppData/Roaming"), LOCALAPPDATA: join(home, "AppData/Local"),
    XDG_CONFIG_HOME: join(home, ".config"), PI_CODING_AGENT_DIR: join(home, ".pi/agent"), PI_OFFLINE: "1", PI_TELEMETRY: "0" };
  const names = [...Object.keys(changed), "NODE_OPTIONS", "NODE_PATH"];
  const previous = Object.fromEntries(names.map((name) => [name, process.env[name]]));
  Object.assign(process.env, changed); delete process.env.NODE_OPTIONS; delete process.env.NODE_PATH;
  const server = await openAiFixture(handler);
  const originalFetch = globalThis.fetch;
  globalThis.fetch = (input, config) => {
    const url = new URL(typeof input === "string" || input instanceof URL ? input : input.url);
    assert.equal(url.origin, server.origin, "P3 fixture refuses every non-local model/network endpoint");
    return originalFetch(input, config);
  };
  const protocols = [], hosts = [];
  t.after(async () => {
    for (const protocol of protocols) await protocol.disconnect();
    await server.close(); globalThis.fetch = originalFetch;
    for (const [name, value] of Object.entries(previous)) value === undefined ? delete process.env[name] : process.env[name] = value;
    await rm(root, { recursive: true, force: true });
  });
  function create() {
    const frames = [];
    let seq = 0, controlled;
    const protocol = new PrivateSession({ metadata,
      createSession: async (config) => { controlled = await createControlledSession({ nodeModules, ...config }); return controlled; },
      emit: (frame) => {
        frames.push(frame);
        if (frame.type === "tool.request") {
          hosts.push(frame);
          if (options.autoHost !== false) queueMicrotask(() => { void send("tool.result", { callId: frame.callId,
            result: { status: "success", assetId: "asset-fixture", diagnostic: `host ${fixtureKey}`, nested: [fixtureKey] } }, frame.runId); });
        }
      },
    });
    const send = async (type, payload = {}, runId = "") => {
      await protocol.accept({ protocol: "sicnu.pi/1", sessionId: "session", generation: "generation-1", runId, seq: seq + 1, type, ...payload });
      seq++;
    };
    protocols.push(protocol);
    return { protocol, frames, send, controlled: () => controlled,
      async initialize(extra = {}) {
        await send("initialize", { cwd, profile: { baseUrl: server.baseUrl, model: "step-5-preview", apiKey: fixtureKey, contextWindow: 32768, maxTokens: 4096 },
          tools: [tool], toolRevision: "catalog-1", sdkReceiptSha256: metadata.sdkReceiptSha256, systemPrompt, checkpointBinding: binding, ...extra });
      },
      async run(runId, text) { await send("prompt", { text }, runId); await waitFor(() => frames.some((frame) => frame.type === "settled" && frame.runId === runId)); },
    };
  }
  return { root, cwd, restoredCwd, home, server, hosts, create };
}

test("real private protocol saves redacted complete history and restores through public SDK without any replay", { timeout: 120000 }, async (t) => {
  const f = await fixture(t, (body) => lastUserText(body) === "continue after restore" ? { text: "Continued without replay." }
    : body.messages.at(-1)?.role === "tool" ? { text: `Committed ${fixtureKey}` }
      : { tool: { id: "native-1", name: tool.name, arguments: { input: "变化.tif", note: `argument ${fixtureKey}` } } });
  const first = f.create();
  await first.initialize();
  assert.equal(f.server.requests.length, 0); assert.equal(f.hosts.length, 0);
  await first.run("run-1", "run the native fixture");
  assert.equal(first.frames.at(-1).status, "completed");
  assert.equal(f.server.requests.length, 2); assert.equal(f.hosts.length, 1);
  await first.send("checkpoint.request", checkpointRequest("save-1"), "run-1");
  const saved = first.frames.at(-1);
  assert.equal(saved.type, "checkpoint"); assert.equal(saved.runId, "run-1"); assert.equal(saved.checkpointId, "save-1");
  const parsed = parseCheckpoint(saved.checkpoint, bindings());
  const serialized = JSON.stringify(parsed);
  assert.equal(serialized.includes(fixtureKey), false);
  assert.equal(serialized.includes(JSON.stringify(fixtureKey).slice(1, -1)), false);
  assert.ok(serialized.includes("[REDACTED]"));
  assert.equal(parsed.header.cwd, "sicnu-product-owned"); assert.equal(serialized.includes(f.cwd), false);
  assert.equal(parsed.sdkReceiptSha256, metadata.sdkReceiptSha256); assert.equal(parsed.systemPromptSha256, binding.systemPromptSha256);
  const calls = parsed.entries.flatMap((entry) => entry.message?.role === "assistant" ? entry.message.content.filter((item) => item.type === "toolCall") : []);
  const results = parsed.entries.filter((entry) => entry.message?.role === "toolResult");
  assert.equal(calls.length, 1); assert.equal(results.length, 1); assert.equal(calls[0].id, results[0].message.toolCallId);

  const restored = f.create();
  await restored.initialize({ cwd: f.restoredCwd, checkpoint: parsed });
  assert.equal(restored.frames.at(-1).type, "ready");
  assert.equal(restored.controlled().session.isIdle, true);
  assert.equal(restored.controlled().session.sessionManager.isPersisted(), false);
  assert.equal(restored.controlled().session.sessionManager.getCwd(), f.restoredCwd);
  assert.equal(restored.controlled().session.sessionManager.getHeader().cwd, f.restoredCwd);
  assert.equal(f.server.requests.length, 2); assert.equal(f.hosts.length, 1);
  await assert.rejects(restored.send("prompt", { text: "forbidden old run replay" }, "run-1"), ProtocolError);
  await restored.send("checkpoint.request", checkpointRequest("save-restored"), "run-1");
  assert.equal(restored.frames.at(-1).type, "checkpoint");
  assert.equal(f.server.requests.length, 2); assert.equal(f.hosts.length, 1);
  await restored.run("run-2", "continue after restore");
  assert.equal(f.server.requests.length, 3); assert.equal(f.hosts.length, 1);
  const resumedBody = JSON.stringify(f.server.requests.at(-1).body);
  assert.ok(resumedBody.includes("[REDACTED]")); assert.ok(resumedBody.includes("run the native fixture"));
  assert.equal(resumedBody.includes(fixtureKey), false);
});

test("valid checkpoint commands reject pending, stale, duplicate and unacknowledged work nonfatally", { timeout: 120000 }, async (t) => {
  const f = await fixture(t, (body) => body.messages.at(-1)?.role === "tool" ? { text: "Committed." }
    : { tool: { id: "waiting-native", name: tool.name, arguments: { input: "fixture.tif" } } }, { autoHost: false });
  const session = f.create(); await session.initialize();
  await session.send("checkpoint.request", checkpointRequest("not-yet"), "run-never");
  assert.equal(session.frames.at(-1).code, "CHECKPOINT_STALE_RUN");
  await session.send("prompt", { text: "wait on native host" }, "run-1");
  await waitFor(() => f.hosts.length === 1);
  await session.send("checkpoint.request", checkpointRequest("pending"), "run-1");
  assert.equal(session.frames.at(-1).code, "CHECKPOINT_NOT_IDLE");
  await session.send("tool.result", { callId: "waiting-native", result: { status: "success" } }, "run-1");
  await waitFor(() => session.frames.some((frame) => frame.type === "settled"));
  for (const [id, runId, extra, code] of [
    ["stale", "older", {}, "CHECKPOINT_STALE_RUN"],
    ["host-busy", "run-1", { hostIdle: false }, "CHECKPOINT_NOT_SETTLED"],
    ["host-unsettled", "run-1", { hostSettled: false }, "CHECKPOINT_NOT_SETTLED"],
  ]) {
    await session.send("checkpoint.request", checkpointRequest(id, extra), runId);
    assert.equal(session.frames.at(-1).type, "checkpoint.failed"); assert.equal(session.frames.at(-1).code, code);
  }
  await session.send("checkpoint.request", checkpointRequest("saved"), "run-1");
  assert.equal(session.frames.at(-1).type, "checkpoint");
  await session.send("checkpoint.request", checkpointRequest("saved"), "run-1");
  assert.equal(session.frames.at(-1).code, "CHECKPOINT_DUPLICATE_ID");
  await assert.rejects(session.send("checkpoint.request", { ...checkpointRequest("unknown"), approved: true }, "run-1"), ProtocolError);
  await assert.rejects(session.send("checkpoint.request", checkpointRequest("bad-flag", { hostIdle: "true" }), "run-1"), ProtocolError);

  await session.send("prompt", { text: "cancel native host wait" }, "run-cancel");
  await waitFor(() => f.hosts.length === 2);
  await session.send("cancel", {}, "run-cancel");
  await session.send("tool.result", { callId: "waiting-native", result: { status: "cancelled" } }, "run-cancel");
  await waitFor(() => session.frames.some((frame) => frame.type === "settled" && frame.runId === "run-cancel"));
  assert.equal(session.frames.findLast((frame) => frame.type === "settled").aborted, true);
  await session.send("checkpoint.request", checkpointRequest("cancelled"), "run-cancel");
  assert.equal(session.frames.at(-1).code, "CHECKPOINT_NOT_COMPLETED");
  await session.send("checkpoint.request", checkpointRequest("former-success"), "run-1");
  assert.equal(session.frames.at(-1).code, "CHECKPOINT_STALE_RUN");
});

test("identity collisions and object-key secrets fail saving without terminating the real SDK conversation", { timeout: 120000 }, async (t) => {
  const f = await fixture(t, (body) => lastUserText(body) === "tool key secret" && body.messages.at(-1)?.role !== "tool"
    ? { tool: { id: "key-secret", name: tool.name, arguments: { input: "fixture.tif" } } } : { text: "Completed safely." }, { autoHost: false });
  const session = f.create();
  await session.initialize({ checkpointBinding: { ...binding, conversationId: `conversation-${fixtureKey}` } });
  await session.run("run-1", "complete before save");
  await session.send("checkpoint.request", checkpointRequest("identity-collision"), "run-1");
  assert.equal(session.frames.at(-1).type, "checkpoint.failed");
  assert.equal(session.frames.at(-1).code, "CHECKPOINT_REDACTION_FAILED");
  assert.equal(JSON.stringify(session.frames.at(-1)).includes(fixtureKey), false);
  await session.run("run-2", "still usable after rejected save");
  assert.equal(session.frames.at(-1).status, "completed");
  assert.equal(f.server.requests.length, 2); assert.equal(f.hosts.length, 0);
  const keys = f.create(); await keys.initialize();
  const completion = keys.run("key-run", "tool key secret");
  await waitFor(() => f.hosts.length === 1);
  await keys.send("tool.result", { callId: "key-secret", result: { status: "success", [fixtureKey]: "object property must not leak" } }, "key-run");
  await completion;
  await keys.send("checkpoint.request", checkpointRequest("object-key-collision"), "key-run");
  assert.equal(keys.frames.at(-1).code, "CHECKPOINT_REDACTION_FAILED");
  assert.equal(JSON.stringify(keys.frames.at(-1)).includes(fixtureKey), false);
  assert.equal(keys.frames.some((frame) => frame.type === "closed"), false);
  await keys.run("key-followup", "still usable after key collision");
  assert.equal(keys.frames.at(-1).status, "completed");
  const hashes = f.create();
  await hashes.initialize({ profile: { baseUrl: f.server.baseUrl, model: "step-5-preview", apiKey: metadata.sdkReceiptSha256, contextWindow: 32768, maxTokens: 4096 } });
  await hashes.run("hash-run", "hash identity collision");
  await hashes.send("checkpoint.request", checkpointRequest("hash-collision"), "hash-run");
  assert.equal(hashes.frames.at(-1).code, "CHECKPOINT_REDACTION_FAILED");
  assert.equal(hashes.frames.some((frame) => frame.type === "checkpoint"), false);
  await hashes.run("hash-followup", "still usable after hash collision");
  assert.equal(hashes.frames.at(-1).status, "completed");
});

test("legacy initialization remains usable but cannot persist without explicit checkpoint bindings", { timeout: 120000 }, async (t) => {
  const f = await fixture(t); const session = f.create();
  await session.send("initialize", { cwd: f.cwd, profile: { baseUrl: f.server.baseUrl, model: "step-5-preview", apiKey: fixtureKey, contextWindow: 32768, maxTokens: 4096 },
    tools: [tool], toolRevision: "catalog-1", sdkReceiptSha256: metadata.sdkReceiptSha256, systemPrompt });
  await session.run("legacy-run", "legacy roundtrip");
  await session.send("checkpoint.request", checkpointRequest("unavailable"), "legacy-run");
  assert.equal(session.frames.at(-1).code, "CHECKPOINT_UNAVAILABLE");
  await session.run("legacy-run-2", "another roundtrip");
  assert.equal(session.frames.at(-1).status, "completed");
});

test("initial checkpoint shape, prompt hash, receipt, tool revision and one-MiB bound fail before SDK initialization", async () => {
  let created = 0;
  const base = { protocol: "sicnu.pi/1", sessionId: "session", generation: "1", runId: "", seq: 1, type: "initialize",
    cwd: resolve("owned"), profile: { baseUrl: "http://127.0.0.1/v1", model: "fixture", apiKey: fixtureKey, contextWindow: 16384, maxTokens: 4096 },
    tools: [tool], toolRevision: "catalog-1", sdkReceiptSha256: metadata.sdkReceiptSha256, systemPrompt, checkpointBinding: binding };
  const timestamp = "2026-11-15T00:00:00.000Z";
  const checkpoint = { schema: "sicnu.pi-checkpoint/1", piVersion: "1.5.2", checkpointId: "synthetic", ...bindings(),
    completedRunId: "prior-run", status: "completed", header: { type: "session", version: 3, id: "session-1", cwd: "owned", timestamp },
    entries: [
      { type: "message", id: "user", parentId: null, timestamp, message: { role: "user", content: "completed question", timestamp: 1 } },
      { type: "message", id: "assistant", parentId: "user", timestamp, message: { role: "assistant", content: [{ type: "text", text: "completed answer" }],
        api: "openai-completions", provider: "exp-rs-product", model: "fixture", stopReason: "stop", timestamp: 1,
        usage: { input: 30, output: 20, cacheRead: 0, cacheWrite: 0, totalTokens: 50, cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, total: 0 } } } },
    ], leafId: "assistant" };
  delete checkpoint.allowedToolNames;
  assert.equal(parseCheckpoint(checkpoint, bindings()).status, "completed");
  for (const mutate of [
    (frame) => { frame.unknown = true; },
    (frame) => { frame.checkpointBinding.extra = true; },
    (frame) => { frame.checkpointBinding.systemPromptSha256 = "0".repeat(64); },
    (frame) => { frame.checkpoint = {}; delete frame.checkpointBinding; },
    (frame) => { frame.checkpoint = "not-an-object"; },
    (frame) => { frame.checkpoint = { oversized: "x".repeat(MAX_CHECKPOINT_BYTES) }; },
    (frame) => { frame.sdkReceiptSha256 = "0".repeat(64); },
    (frame) => { frame.checkpoint = { ...checkpoint, toolRevision: "older-catalog" }; },
    (frame) => { frame.checkpoint = { ...checkpoint, sdkReceiptSha256: "0".repeat(64) }; },
    (frame) => { frame.checkpoint = { ...checkpoint, projectId: "different-project" }; },
    (frame) => { frame.checkpoint = { ...checkpoint, conversationId: "different-conversation" }; },
    (frame) => { frame.checkpoint = { ...checkpoint, systemPromptSha256: "0".repeat(64) }; },
  ]) {
    const frame = structuredClone(base); mutate(frame);
    const protocol = new PrivateSession({ metadata, createSession: async () => { created++; throw new Error("Must not initialize"); }, emit() {} });
    await assert.rejects(protocol.accept(frame), ProtocolError);
  }
  assert.equal(created, 0);
});

test("installed wrapper receipt must bind the fourth checkpoint module", async (t) => {
  const root = await mkdtemp(join(tmpdir(), "Pi P3 receipt "));
  t.after(() => rm(root, { recursive: true, force: true }));
  const directory = join(root, "libexec/sicnu-geo-rs/pi-product"), share = join(root, "share/sicnu-geo-rs");
  await Promise.all([mkdir(directory, { recursive: true }), mkdir(share, { recursive: true })]);
  const files = [];
  for (const name of ["main.mjs", "protocol.mjs", "controlled-session.mjs", "checkpoint.mjs"]) {
    const bytes = await readFile(new URL(`../${name}`, import.meta.url));
    await writeFile(join(directory, name), bytes);
    files.push({ path: `libexec/sicnu-geo-rs/pi-product/${name}`, size: bytes.length, sha256: sha256(bytes) });
  }
  const sdk = JSON.parse(sdkReceiptBytes), recipes = `{${Object.keys(sdk.recipes).sort().map((key) => `${JSON.stringify(key)}:${JSON.stringify(sdk.recipes[key])}`).join(",")}}`;
  await writeFile(join(share, "pi-runtime.json"), sdkReceiptBytes);
  await writeFile(join(share, "pi-product.json"), JSON.stringify({ schema: "sicnu.pi-product/1", protocol_version: 1, entry: "libexec/sicnu-geo-rs/pi-product/main.mjs", files,
    requires_sdk: { target: sdk.target, pi_version: sdk.pi_version, node_version: sdk.node_version, receipt_sha256: metadata.sdkReceiptSha256, recipe_digest: sha256(recipes) } }));
  const { pathToFileURL } = await import("node:url");
  const entry = pathToFileURL(join(directory, "main.mjs")).href;
  assert.equal((await loadInstallation(entry)).metadata.sdkReceiptSha256, metadata.sdkReceiptSha256);
  await writeFile(join(directory, "checkpoint.mjs"), "tampered checkpoint module");
  await assert.rejects(loadInstallation(entry), (error) => error instanceof ProtocolError && error.code === "INSTALLATION_INVALID");
});
