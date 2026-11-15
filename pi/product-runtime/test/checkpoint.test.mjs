// Checkpoint data validation and real locked-SDK restore/compaction proofs.
import assert from "node:assert/strict";
import { test } from "node:test";
import { mkdtemp, mkdir, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { captureCheckpoint, parseCheckpoint, restoreCheckpoint, CheckpointError, CHECKPOINT_SCHEMA, CHECKPOINT_PI_VERSION, MAX_CHECKPOINT_BYTES } from "../checkpoint.mjs";
import { createControlledSession, EXCLUDED_TOOLS } from "../controlled-session.mjs";
import { openAiFixture, lastUserText } from "./fixtures/openai-server.mjs";

const configuredNodeModules = process.env.SICNU_TEST_PI_NODE_MODULES;
assert.ok(configuredNodeModules, "Set SICNU_TEST_PI_NODE_MODULES to the prepared patched node_modules root");

const time = "2026-11-15T00:00:00.000Z";
const toolName = "exprs_python_run_script";
const tool = {
  name: toolName, canonicalId: "python:run_script", description: "Execute only through the current approved host.",
  parameters: { type: "object", properties: { input: { type: "string" } }, required: ["input"], additionalProperties: false },
};
const binding = {
  conversationId: "conversation-1", projectId: "project-1", toolRevision: "catalog-1",
  sdkReceiptSha256: "a".repeat(64), systemPromptSha256: "b".repeat(64), allowedToolNames: [toolName],
};
const captureOptions = { ...binding, checkpointId: "checkpoint-1", completedRunId: "run-1", status: "completed", hostSettled: true, hostIdle: true };
const usage = () => ({ input: 30, output: 20, cacheRead: 0, cacheWrite: 0, totalTokens: 50, cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, total: 0 } });
const assistant = (content, stopReason = "stop") => ({ role: "assistant", content, api: "openai-completions", provider: "exp-rs-product", model: "step-5-preview", usage: usage(), stopReason, timestamp: 1 });
const entry = (id, parentId, message) => ({ type: "message", id, parentId, timestamp: time, message });
function baseline() {
  return {
    schema: CHECKPOINT_SCHEMA, piVersion: CHECKPOINT_PI_VERSION,
    checkpointId: "checkpoint-1", conversationId: binding.conversationId, projectId: binding.projectId,
    completedRunId: "run-1", toolRevision: binding.toolRevision, sdkReceiptSha256: binding.sdkReceiptSha256,
    systemPromptSha256: binding.systemPromptSha256, status: "completed",
    header: { type: "session", version: 3, id: "sdk-session-1", timestamp: time, cwd: resolve("build-logs/owned-before") },
    entries: [
      { type: "model_change", id: "model", parentId: null, timestamp: time, provider: "exp-rs-product", modelId: "step-5-preview" },
      { type: "thinking_level_change", id: "thinking", parentId: "model", timestamp: time, thinkingLevel: "off" },
      entry("system", "thinking", { role: "system", content: "Only host tools.", toolsAdded: [{ name: toolName, description: tool.description, parameters: tool.parameters }], timestamp: 1 }),
      entry("user", "system", { role: "user", content: "Run the approved host fixture.", timestamp: 1 }),
      entry("call", "user", assistant([{ type: "toolCall", id: "call-1", name: toolName, arguments: { input: "变化.tif" } }], "toolUse")),
      entry("result", "call", { role: "toolResult", toolCallId: "call-1", toolName, content: [{ type: "text", text: "committed" }], details: { status: "success", assetId: "asset-1" }, isError: false, timestamp: 1 }),
      entry("final", "result", assistant([{ type: "text", text: "The approved result was committed." }])),
    ], leafId: "final",
  };
}
function fakeSession(snapshot = baseline()) {
  return { isIdle: true, sessionManager: {
    getHeader: () => snapshot.header, getEntries: () => [...snapshot.entries], getLeafId: () => snapshot.leafId,
    buildSessionContext: () => ({ messages: snapshot.entries.filter((value) => value.type === "message").map((value) => value.message) }),
  } };
}
function rejectsCode(operation, code) {
  assert.throws(operation, (error) => error instanceof CheckpointError && error.code === code && error.message === code);
}

test("capture requires successful SDK and both native terminal boundaries", () => {
  for (const [hostSettled, hostIdle] of [[false, true], [true, false], [false, false]])
    rejectsCode(() => captureCheckpoint(fakeSession(), { ...captureOptions, hostSettled, hostIdle }), "CHECKPOINT_NOT_SETTLED");
  rejectsCode(() => captureCheckpoint({ ...fakeSession(), isIdle: false }, captureOptions), "CHECKPOINT_NOT_SETTLED");
  for (const status of ["running", "cancelled", "failed"])
    rejectsCode(() => captureCheckpoint(fakeSession(), { ...captureOptions, status }), "CHECKPOINT_NOT_COMPLETED");
  const captured = captureCheckpoint(fakeSession(), captureOptions);
  assert.equal(captured.status, "completed");
});

test("capture and parse deeply isolate SDK-owned headers, entries, arguments and results", () => {
  const original = baseline();
  original.entries[5].message.usage = undefined; // Real SDK host results keep this optional own property.
  const captured = captureCheckpoint(fakeSession(original), captureOptions);
  assert.equal(Object.hasOwn(captured.entries[5].message, "usage"), false);
  captured.header.cwd = "changed";
  captured.entries[4].message.content[0].arguments.input = "changed";
  captured.entries[5].message.details.assetId = "changed";
  assert.notEqual(original.header.cwd, "changed");
  assert.equal(original.entries[4].message.content[0].arguments.input, "变化.tif");
  assert.equal(original.entries[5].message.details.assetId, "asset-1");
  const parsed = parseCheckpoint(JSON.stringify(original), binding);
  assert.deepEqual(parsed, JSON.parse(JSON.stringify(original)));
  parsed.entries[6].message.content[0].text = "changed";
  assert.notEqual(original.entries[6].message.content[0].text, "changed");
});

test("strict envelopes reject malformed JSON, versions, secret/profile fields and mismatched bindings", () => {
  rejectsCode(() => parseCheckpoint("{broken", binding), "CHECKPOINT_INVALID_JSON");
  for (const mutate of [
    (c) => { c.schema = "sicnu.pi-checkpoint/2"; },
    (c) => { c.piVersion = "1.5.3"; },
    (c) => { c.header.version = 4; },
  ]) {
    const checkpoint = baseline(); mutate(checkpoint);
    rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_VERSION_UNSUPPORTED");
  }
  for (const key of ["credentials", "credential", "profile", "baseUrl", "apiKey", "generation", "pendingCalls", "approvalToken"]) {
    const checkpoint = baseline(); checkpoint[key] = "must-never-be-persisted";
    rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_INVALID");
  }
  for (const key of ["conversationId", "projectId", "toolRevision", "sdkReceiptSha256", "systemPromptSha256"])
    rejectsCode(() => parseCheckpoint(baseline(), { ...binding, [key]: "mismatch" }), "CHECKPOINT_BINDING_MISMATCH");
  const checkpoint = baseline(); checkpoint.status = "cancelled";
  rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_NOT_COMPLETED");
  // SDK model/provider metadata remains transcript data, not a persisted profile.
  assert.equal(parseCheckpoint(baseline(), binding).entries[6].message.model, "step-5-preview");
});

test("one MiB applies to encoded UTF-8 JSON, inclusively, without truncation", () => {
  const checkpoint = baseline();
  checkpoint.entries[3].message.content = "";
  const overhead = Buffer.byteLength(JSON.stringify(checkpoint), "utf8");
  checkpoint.entries[3].message.content = "x".repeat(MAX_CHECKPOINT_BYTES - overhead);
  const encoded = JSON.stringify(checkpoint);
  assert.equal(Buffer.byteLength(encoded, "utf8"), MAX_CHECKPOINT_BYTES);
  assert.equal(parseCheckpoint(encoded, binding).entries[3].message.content.length, MAX_CHECKPOINT_BYTES - overhead);
  checkpoint.entries[3].message.content += "x";
  rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_TOO_LARGE");
  rejectsCode(() => parseCheckpoint(`${encoded} `, binding), "CHECKPOINT_TOO_LARGE");
  checkpoint.entries[3].message.content = "中".repeat(MAX_CHECKPOINT_BYTES / 2);
  rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_TOO_LARGE");
});

test("malformed entry types, IDs, timestamps, parent trees and active leaves are rejected", () => {
  for (const mutate of [
    (c) => { c.entries[1].id = c.entries[0].id; },
    (c) => { c.entries[1].parentId = "missing"; },
    (c) => { c.entries[0].parentId = "final"; },
    (c) => { c.entries[1].parentId = null; },
    (c) => { c.leafId = "missing"; },
    (c) => { c.leafId = null; },
    (c) => { c.entries.push(entry("cycle-a", "cycle-b", { role: "user", content: "abandoned", timestamp: 1 }), entry("cycle-b", "cycle-a", assistant([{ type: "text", text: "abandoned" }]))); },
  ]) {
    const checkpoint = baseline(); mutate(checkpoint);
    rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_INVALID_TREE");
  }
  const unknown = baseline(); unknown.entries[0].type = "custom";
  rejectsCode(() => parseCheckpoint(unknown, binding), "CHECKPOINT_INVALID");
  const badTime = baseline(); badTime.entries[0].timestamp = "not-a-timestamp";
  rejectsCode(() => parseCheckpoint(badTime, binding), "CHECKPOINT_INVALID");
  const shape = baseline(); shape.entries[6].extra = "unknown";
  rejectsCode(() => parseCheckpoint(shape, binding), "CHECKPOINT_INVALID");
});

test("compaction/context-edit references must stay on their own ancestor chain", () => {
  const checkpoint = baseline();
  checkpoint.entries.push({ type: "compaction", id: "compact", parentId: "final", timestamp: time, summary: "Earlier approved work completed.", firstKeptEntryId: "user", tokensBefore: 5000 });
  checkpoint.leafId = "compact";
  assert.equal(parseCheckpoint(checkpoint, binding).leafId, "compact");
  checkpoint.entries.at(-1).firstKeptEntryId = "compact"; // Public SDK retain-none boundary.
  assert.equal(parseCheckpoint(checkpoint, binding).leafId, "compact");
  checkpoint.entries.at(-1).firstKeptEntryId = "missing";
  rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_INVALID_TREE");
  const edit = baseline();
  edit.entries.push({ type: "context_edit", id: "edit", parentId: "final", timestamp: time, targetId: "missing", replacement: null });
  edit.leafId = "edit";
  rejectsCode(() => parseCheckpoint(edit, binding), "CHECKPOINT_INVALID_TREE");
});

test("effective context edits and compaction must preserve complete projected tool pairs", () => {
  const omitted = baseline();
  omitted.entries.push({ type: "context_edit", id: "omit-result", parentId: "final", timestamp: time, targetId: "result", replacement: null });
  omitted.leafId = "omit-result";
  rejectsCode(() => parseCheckpoint(omitted, binding), "CHECKPOINT_TOOL_PAIRS");
  const replaced = baseline();
  replaced.entries.push({ type: "context_edit", id: "replace-final", parentId: "final", timestamp: time, targetId: "final",
    replacement: { content: [{ type: "toolCall", id: "unresolved-replacement", name: toolName, arguments: {} }] } });
  replaced.leafId = "replace-final";
  rejectsCode(() => parseCheckpoint(replaced, binding), "CHECKPOINT_TOOL_PAIRS");
  const compact = baseline();
  compact.entries.push({ type: "compaction", id: "bad-cut", parentId: "final", timestamp: time, summary: "Earlier work", firstKeptEntryId: "result", tokensBefore: 5000 });
  compact.leafId = "bad-cut";
  rejectsCode(() => parseCheckpoint(compact, binding), "CHECKPOINT_TOOL_PAIRS");
  // SDK recovery can legitimately omit an entire earlier assistant attempt.
  omitted.entries.push({ type: "context_edit", id: "omit-call", parentId: "omit-result", timestamp: time, targetId: "call", replacement: null });
  omitted.leafId = "omit-call";
  assert.equal(parseCheckpoint(omitted, binding).leafId, "omit-call");
});

test("only allowlisted complete active-branch tool pairs may be restored", () => {
  for (const mutate of [
    (c) => { c.entries[5].message.toolCallId = "missing"; },
    (c) => { c.entries[4].message.content.push({ ...c.entries[4].message.content[0] }); },
    (c) => { c.entries[5].message = assistant([{ type: "text", text: "tool still pending" }]); },
    (c) => { c.leafId = "call"; },
  ]) {
    const checkpoint = baseline(); mutate(checkpoint);
    rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_TOOL_PAIRS");
  }
  const unknown = baseline(); unknown.entries[4].message.content[0].name = "write";
  rejectsCode(() => parseCheckpoint(unknown, binding), "CHECKPOINT_TOOL_NOT_ALLOWED");
  const staleDeclaration = baseline(); staleDeclaration.entries[2].message.toolsAdded[0].name = "write";
  rejectsCode(() => parseCheckpoint(staleDeclaration, binding), "CHECKPOINT_TOOL_NOT_ALLOWED");
  for (const stopReason of ["pending", "deferred"]) {
    const pending = baseline(); pending.entries[6].message.stopReason = stopReason;
    rejectsCode(() => parseCheckpoint(pending, binding), "CHECKPOINT_PENDING");
  }
  const nested = baseline(); nested.entries[5].message.nestedCalls = { calls: [], complete: false };
  rejectsCode(() => parseCheckpoint(nested, binding), "CHECKPOINT_PENDING");
  const reused = baseline();
  reused.entries.push(entry("user-again", "final", { role: "user", content: "A separately completed run", timestamp: 2 }));
  reused.entries.push(entry("call-again", "user-again", structuredClone(reused.entries[4].message)));
  reused.entries.push(entry("result-again", "call-again", structuredClone(reused.entries[5].message)));
  reused.entries.push(entry("final-again", "result-again", structuredClone(reused.entries[6].message)));
  reused.leafId = "final-again";
  assert.equal(parseCheckpoint(reused, binding).leafId, "final-again", "Provider call IDs may be reused by separate completed runs");
});

test("abandoned branches survive while only the selected leaf supplies completed context", () => {
  const checkpoint = baseline();
  checkpoint.entries.push(entry("other-user", "final", { role: "user", content: "abandoned request", timestamp: 1 }));
  checkpoint.entries.push(entry("other-call", "other-user", assistant([{ type: "toolCall", id: "abandoned-call", name: toolName, arguments: { input: "abandoned.tif" } }], "toolUse")));
  assert.equal(parseCheckpoint(checkpoint, binding).leafId, "final");
  checkpoint.leafId = "other-call";
  rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_TOOL_PAIRS");
});

test("objects cannot execute accessors/toJSON, contain cycles or smuggle non-JSON values", () => {
  const checkpoint = baseline();
  Object.defineProperty(checkpoint, "profile", { enumerable: true, get() { assert.fail("Accessor must not execute"); } });
  rejectsCode(() => parseCheckpoint(checkpoint, binding), "CHECKPOINT_INVALID");
  const toJson = baseline(); toJson.toJSON = () => { assert.fail("toJSON must not execute"); };
  rejectsCode(() => parseCheckpoint(toJson, binding), "CHECKPOINT_INVALID");
  const cycle = baseline(); cycle.entries[5].message.details.self = cycle;
  rejectsCode(() => parseCheckpoint(cycle, binding), "CHECKPOINT_INVALID");
});

const systemPrompt = "You are an exp-rs fixture assistant. Only host tools may execute approved work.";
const fixtureKey = "p3-local-fixture-only-not-user-credential";
async function realFixture(t, handler) {
  const root = await mkdtemp(join(tmpdir(), "RS Pi P3 checkpoint 中文 "));
  const cwd = join(root, "work-before"), restoredCwd = join(root, "work-after"), home = join(root, "owned-home");
  await Promise.all([mkdir(cwd), mkdir(restoredCwd), mkdir(home)]);
  const changed = { HOME: home, USERPROFILE: home, APPDATA: join(home, "AppData", "Roaming"), LOCALAPPDATA: join(home, "AppData", "Local"), XDG_CONFIG_HOME: join(home, ".config"), PI_CODING_AGENT_DIR: join(home, ".pi", "agent"), PI_OFFLINE: "1", PI_TELEMETRY: "0" };
  const previous = Object.fromEntries([...Object.keys(changed), "NODE_OPTIONS", "NODE_PATH"].map((key) => [key, process.env[key]]));
  Object.assign(process.env, changed); delete process.env.NODE_OPTIONS; delete process.env.NODE_PATH;
  t.after(async () => {
    for (const [key, value] of Object.entries(previous)) value === undefined ? delete process.env[key] : process.env[key] = value;
    await rm(root, { recursive: true, force: true });
  });
  const server = await openAiFixture(handler);
  t.after(() => server.close());
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (input, options) => {
    const url = new URL(typeof input === "string" || input instanceof URL ? input : input.url);
    if (url.origin !== server.origin) throw new Error("P3 fixture rejected a non-local model/network endpoint");
    return originalFetch(input, options);
  };
  t.after(() => { globalThis.fetch = originalFetch; });
  const nodeModules = resolve(configuredNodeModules);
  const importStarted = performance.now();
  const sdk = await import(pathToFileURL(join(nodeModules, "@earendil-works/pi-coding-agent/dist/index.js")).href);
  t.diagnostic(`Locked SDK import: ${Math.round(performance.now() - importStarted)} ms`);
  const calls = [];
  const onToolRequest = async (request) => { calls.push(request); return { status: "success", assetId: `fixture-asset-${calls.length}` }; };
  const profile = { baseUrl: server.baseUrl, model: "step-5-preview", apiKey: fixtureKey, contextWindow: 32768, maxTokens: 4096 };
  const controlled = await createControlledSession({ nodeModules, cwd, profile, systemPrompt, tools: [tool], onToolRequest });
  t.after(() => controlled.session.dispose());
  return { root, cwd, restoredCwd, server, sdk, nodeModules, profile, calls, onToolRequest, ...controlled };
}
function emptyResources(sdk) {
  const extensions = { extensions: [], errors: [], runtime: sdk.createExtensionRuntime() };
  return {
    getExtensions: () => extensions, getSkills: () => ({ skills: [], diagnostics: [] }), getPrompts: () => ({ prompts: [], diagnostics: [] }),
    getThemes: () => ({ themes: [], diagnostics: [] }), getAgentsFiles: () => ({ agentsFiles: [] }), getSystemPrompt: () => systemPrompt,
    getSystemPromptSource: () => undefined, getAppendSystemPrompt: () => [], getAppendSystemPromptSources: () => [], extendResources: () => {}, reload: async () => {},
  };
}
async function controlledRestore(t, fixture, checkpoint) {
  const beforeRequests = fixture.server.requests.length, beforeCalls = fixture.calls.length;
  const manager = restoreCheckpoint(fixture.sdk.SessionManager, JSON.stringify(checkpoint), { ...binding, currentOwnedCwd: fixture.restoredCwd });
  assert.equal(manager.isPersisted(), false);
  assert.equal(manager.getSessionFile(), undefined);
  assert.equal(manager.getHeader().cwd, fixture.restoredCwd);
  assert.equal(manager.getCwd(), fixture.restoredCwd);
  // Assemble fresh controlled credentials/model state, just as an app restart will.
  // All SDK dependencies remain explicit. No private member or monkeypatch is used.
  const seed = await createControlledSession({ nodeModules: fixture.nodeModules, cwd: fixture.restoredCwd, profile: fixture.profile, tools: [tool], systemPrompt, onToolRequest: fixture.onToolRequest });
  t.after(() => seed.session.dispose());
  const { session } = await fixture.sdk.createAgentSession({
    cwd: fixture.restoredCwd, agentDir: join(fixture.restoredCwd, ".product-agent-unused"),
    modelRuntime: seed.session.modelRuntime, model: seed.session.model, thinkingLevel: "off",
    settingsManager: seed.session.settingsManager, sessionManager: manager, resourceLoader: emptyResources(fixture.sdk),
    noTools: "all", tools: [toolName], excludeTools: [...EXCLUDED_TOOLS],
    customTools: [{ name: toolName, label: toolName, description: tool.description, parameters: tool.parameters, executionMode: "sequential",
      async execute(toolCallId, args, signal) {
        const result = await fixture.onToolRequest({ toolCallId, canonicalId: tool.canonicalId, arguments: args, signal });
        return { content: [{ type: "text", text: JSON.stringify(result) }], details: result };
      } }],
  });
  t.after(() => session.dispose());
  assert.equal(session.isIdle, true);
  assert.deepEqual(session.getActiveToolNames(), [toolName]);
  assert.deepEqual(session.getCallableToolNames(), [toolName]);
  assert.equal(fixture.server.requests.length, beforeRequests, "Restore/init must not submit a model request");
  assert.equal(fixture.calls.length, beforeCalls, "Restore/init must not replay a host call");
  return session;
}

test("real locked SDK tool roundtrip restores a selected branch with no request or native replay", { timeout: 120000 }, async (t) => {
  const fixture = await realFixture(t, (body) => {
    if (body.messages.at(-1).role === "tool") return { text: "Approved host work completed." };
    if (lastUserText(body) === "Explicit new user tool request") return { tool: { id: "call-new-user", name: toolName, arguments: { input: "new.tif" } } };
    return { tool: { id: "call-original", name: toolName, arguments: { input: "original.tif" } } };
  });
  await fixture.prompt("Original approved host request");
  assert.equal(fixture.calls.length, 1);
  const manager = fixture.session.sessionManager;
  const selectedLeaf = manager.getLeafId();
  const expectedContext = JSON.parse(JSON.stringify(manager.buildSessionContext()));
  manager.appendMessage({ role: "user", content: "Abandoned later branch", timestamp: 2 });
  manager.appendMessage(assistant([{ type: "text", text: "Abandoned later answer" }]));
  manager.branch(selectedLeaf);
  const checkpoint = captureCheckpoint(fixture.session, captureOptions);
  const snapshotText = JSON.stringify(checkpoint);
  assert.equal(snapshotText.includes(fixtureKey), false);
  assert.equal(Object.hasOwn(checkpoint, "profile"), false);
  fixture.session.dispose();
  const restored = await controlledRestore(t, fixture, checkpoint);
  assert.equal(restored.sessionManager.getLeafId(), selectedLeaf);
  assert.deepEqual(JSON.parse(JSON.stringify(restored.sessionManager.buildSessionContext())), expectedContext);
  assert.equal(restored.sessionManager.getEntries().length, checkpoint.entries.length, "Abandoned history is retained");
  await restored.prompt("Explicit new user tool request", { expandPromptTemplates: false });
  assert.equal(fixture.calls.length, 2);
  assert.deepEqual(fixture.calls.map((call) => call.arguments.input), ["original.tif", "new.tif"]);
  assert.equal(restored.getLastAssistantText(), "Approved host work completed.");
  assert.equal(fixture.server.requests.length, 4);
  t.diagnostic(`Tool roundtrip: ${fixture.server.requests.length} local model requests, ${fixture.calls.length} host calls; restore/init added 0 of each`);
});

test("real locked SDK compaction survives restore and the next approved host loop", { timeout: 120000 }, async (t) => {
  let summaries = 0;
  const fixture = await realFixture(t, (body) => {
    const user = lastUserText(body);
    if (user.includes("<conversation>") || user.startsWith("# Conversation\n")) {
      summaries++;
      return { text: "Checkpoint: earlier fixture dialogue completed; approved host work still requires host tools." };
    }
    if (user === "Use host after restore") return body.messages.at(-1).role === "tool"
      ? { text: "Post-restore host result received." } : { tool: { id: "call-restored-compact", name: toolName, arguments: { input: "compacted.tif" } } };
    return { text: "A completed, deliberately long fixture response. ".repeat(240) };
  });
  for (let turn = 0; turn < 3; turn++) await fixture.prompt(`Fixture history turn ${turn}`);
  await fixture.session.compact("Keep the host-tool requirement.");
  assert.equal(summaries, 2, "Fixed 1.5.2 fixture splits history and user-span prefix summaries");
  const expectedContext = JSON.parse(JSON.stringify(fixture.session.sessionManager.buildSessionContext()));
  const checkpoint = captureCheckpoint(fixture.session, { ...captureOptions, completedRunId: "run-compacted" });
  assert.ok(checkpoint.entries.some((value) => value.type === "compaction"));
  fixture.session.dispose();
  const restored = await controlledRestore(t, fixture, checkpoint);
  assert.deepEqual(JSON.parse(JSON.stringify(restored.sessionManager.buildSessionContext())), expectedContext);
  assert.ok(restored.messages.some((value) => value.role === "compactionSummary"));
  await restored.prompt("Use host after restore", { expandPromptTemplates: false });
  assert.equal(fixture.calls.length, 1);
  assert.equal(restored.getLastAssistantText(), "Post-restore host result received.");
  // Fixed fixture: three history completions, two split-span summaries, then
  // one tool response and one final answer. Additional summary calls fail.
  assert.equal(summaries, 2);
  assert.equal(fixture.server.requests.length, 7);
  t.diagnostic(`Compaction restore: ${fixture.server.requests.length} local model requests including ${summaries} summary, ${fixture.calls.length} host call; restore/init added 0 of each`);
});
