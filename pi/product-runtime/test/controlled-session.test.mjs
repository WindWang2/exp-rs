import assert from "node:assert/strict";
import { mkdtemp, mkdir, writeFile, readFile, readdir, access, rm } from "node:fs/promises";
import { createHash } from "node:crypto";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { test } from "node:test";
import { createControlledSession, EXCLUDED_TOOLS } from "../controlled-session.mjs";
import { openAiFixture, lastUserText } from "./fixtures/openai-server.mjs";

const nodeModules = process.env.SICNU_TEST_PI_NODE_MODULES;
assert.ok(nodeModules, "Set SICNU_TEST_PI_NODE_MODULES to the prepared patched node_modules root");
const tool = {
  name: "exprs_python_run_script",
  canonicalId: "python:run_script",
  description: "Execute a host-governed script after native approval.",
  parameters: {
    type: "object", additionalProperties: false,
    properties: { input: { type: "string" }, threshold: { type: "number" } },
    required: ["input", "threshold"],
  },
};
const toolCall = { name: tool.name, arguments: { input: "变化.tif", threshold: 0.2 } };
const systemPrompt = "You are an exp-rs fixture assistant. Only host tools may execute work.";
const fixtureKey = "local-fixture-key-not-a-user-credential";

async function fileInventory(root) {
  const files = await readdir(root, { recursive: true, withFileTypes: true });
  const hashes = [];
  for (const entry of files) {
    if (!entry.isFile()) continue;
    const path = join(entry.parentPath, entry.name);
    hashes.push([path.slice(root.length), createHash("sha256").update(await readFile(path)).digest("hex")]);
  }
  return hashes.sort(([a], [b]) => a.localeCompare(b));
}

async function workspace(t) {
  const root = await mkdtemp(join(tmpdir(), "RS Pi P0 中文 "));
  const home = join(root, "home");
  const cwd = join(root, "project");
  await mkdir(join(home, ".pi", "agent"), { recursive: true });
  await mkdir(join(cwd, ".pi", "extensions"), { recursive: true });
  const marker = join(root, "extension-ran.txt");
  await writeFile(join(cwd, "AGENTS.md"), "BAIT_AGENT_INSTRUCTION_MUST_NOT_ENTER_MODEL");
  await writeFile(join(cwd, "SYSTEM.md"), "BAIT_SYSTEM_PROMPT_MUST_NOT_ENTER_MODEL");
  await writeFile(join(cwd, ".pi", "settings.json"), JSON.stringify({ defaultProvider: "bait-provider" }));
  await writeFile(join(home, ".pi", "agent", "auth.json"), JSON.stringify({ openai: { type: "api_key", key: "bait-home-key" } }));
  await writeFile(join(cwd, ".pi", "extensions", "bait.mjs"), `import {writeFileSync} from 'node:fs';writeFileSync(${JSON.stringify(marker)},'loaded');export default function(){};`);
  const changed = {
    HOME: home, USERPROFILE: home, XDG_CONFIG_HOME: join(home, ".config"),
    APPDATA: join(home, "AppData", "Roaming"), LOCALAPPDATA: join(home, "AppData", "Local"),
    PI_OFFLINE: "1", PI_TELEMETRY: "0", PI_CODING_AGENT_DIR: join(home, ".pi", "agent"),
    OPENAI_API_KEY: "bait-environment-key",
  };
  const previous = Object.fromEntries(Object.keys(changed).map((key) => [key, process.env[key]]));
  Object.assign(process.env, changed);
  t.after(async () => {
    for (const [key, value] of Object.entries(previous)) value === undefined ? delete process.env[key] : process.env[key] = value;
    await rm(root, { recursive: true, force: true });
  });
  return { root, cwd, marker, initialFiles: await fileInventory(root) };
}

async function start(t, handler, onToolRequest) {
  const files = await workspace(t);
  const server = await openAiFixture(handler);
  t.after(() => server.close());
  const fetched = [];
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (input, options) => {
    const url = new URL(typeof input === "string" || input instanceof URL ? input : input.url);
    fetched.push(url.href);
    if (url.origin !== server.origin) throw new Error("Unexpected network endpoint refused by P0 fixture");
    return originalFetch(input, options);
  };
  t.after(() => { globalThis.fetch = originalFetch; });
  const controlled = await createControlledSession({
    nodeModules: resolve(nodeModules), cwd: files.cwd, systemPrompt, tools: [tool], onToolRequest,
    profile: { baseUrl: server.baseUrl, model: "step-5-preview", apiKey: fixtureKey, contextWindow: 32768, maxTokens: 4096 },
  });
  t.after(() => controlled.session.dispose());
  return { ...files, ...controlled, server, fetched };
}

test("Controlled session rejects non-loopback plaintext credential endpoints before SDK loading", async (t) => {
  const files = await workspace(t);
  const construct = (baseUrl) => createControlledSession({
    nodeModules: join(files.root, "must-not-be-opened"), cwd: files.cwd,
    systemPrompt, tools: [tool], onToolRequest: async () => { assert.fail("Endpoint validation must not submit host work"); },
    profile: { baseUrl, model: "step-5-preview", apiKey: fixtureKey, contextWindow: 32768, maxTokens: 4096 },
  });
  for (const endpoint of [
    "http://example.invalid/v1", "http://192.0.2.1/v1", "http://[2001:db8::1]/v1",
    "http://localhost.example.invalid/v1", "http://127.0.0.1.example.invalid/v1",
    "http://[::ffff:192.0.2.1]/v1",
  ]) await assert.rejects(construct(endpoint), /Refusing to send.*plaintext HTTP.*non-loopback/i, endpoint);
  // An absent SDK root makes acceptance observable without loading Pi or opening
  // any connection. Accepted transports proceed to that owned missing path.
  for (const endpoint of [
    "https://example.invalid/v1", "http://localhost/v1", "http://127.0.0.1/v1",
    "http://127.9.8.7/v1", "http://[::1]/v1", "http://[::ffff:127.0.0.1]/v1",
  ]) await assert.rejects(construct(endpoint), { code: "ENOENT" }, endpoint);
});

test("Pi SDK runs the host tool loop with an exact controlled loadout and literal user input", { timeout: 30000 }, async (t) => {
  const calls = [];
  const delivered = { status: "success", assetId: "fixture-asset-only", statistics: { validPixels: 9, changedPixels: 4 } };
  const run = await start(t, (body) => {
    if (lastUserText(body).startsWith("/skill:")) return { text: "Literal input received." };
    if (body.messages.at(-1).role === "tool") return { text: "Verified host fixture result received." };
    return { tool: toolCall };
  }, async (request) => { calls.push(request); return delivered; });
  const events = [];
  run.session.subscribe((event) => events.push(event));
  await run.prompt("Run the host fixture.");
  assert.equal(calls.length, 1);
  assert.equal(calls[0].canonicalId, "python:run_script");
  assert.deepEqual(calls[0].arguments, toolCall.arguments);
  assert.equal(run.session.getLastAssistantText(), "Verified host fixture result received.");
  // The fixture's final SSE usage is per completion, not the sum of both
  // requests in this tool loop; it reports no cache-read or cache-write tokens.
  const assistant = run.session.messages.findLast((message) => message.role === "assistant");
  assert.ok(assistant);
  assert.equal(assistant.usage.input, 30);
  assert.equal(assistant.usage.output, 20);
  assert.equal(assistant.usage.cacheRead, 0);
  assert.equal(assistant.usage.cacheWrite, 0);
  assert.equal(assistant.usage.totalTokens, 50);
  assert.equal(assistant.usage.totalTokens, assistant.usage.input + assistant.usage.output + assistant.usage.cacheRead + assistant.usage.cacheWrite);
  const settled = events.filter((event) => event.type === "agent_settled");
  assert.equal(settled.length, 1);
  assert.equal(settled[0].aborted, false);
  const resultEvent = events.find((event) => event.type === "tool_execution_end");
  assert.deepEqual(resultEvent.result.details, delivered);
  const info = run.diagnostics();
  assert.ok(info.authRequests.environment > 0 && info.authRequests.file > 0);
  assert.deepEqual(info.tools, [tool.name]);
  assert.deepEqual(info.callableTools, [tool.name]);
  for (const name of EXCLUDED_TOOLS) assert.ok(!run.session.getCallableToolNames().includes(name));
  assert.equal(run.session.settingsManager.getCacheWarmingMode(), "off");
  assert.equal(run.session.settingsManager.getEnableInstallTelemetry(), false);
  await run.prompt("/skill:bait literal input");
  assert.equal(lastUserText(run.server.requests.at(-1).body), "/skill:bait literal input");
  assert.equal(run.server.requests.length, 3);
  for (const request of run.server.requests) {
    assert.equal(request.headers.authorization, `Bearer ${fixtureKey}`);
    assert.equal(request.body.model, "step-5-preview");
    assert.ok(request.body.tools.every((entry) => entry.function.name === tool.name));
    assert.ok(!JSON.stringify(request.body).includes("BAIT_"));
    assert.ok(!JSON.stringify(request.body).includes("bait-home-key"));
    assert.ok(!JSON.stringify(request.body).includes("bait-environment-key"));
  }
  assert.equal(run.fetched.length, 3);
  await assert.rejects(access(run.marker), { code: "ENOENT" });
  assert.deepEqual(await fileInventory(run.root), run.initialFiles, "Memory-only SDK assembly must not persist settings, models, credentials or session files");
});

test("Pi abort stops a streaming model response and settles aborted", { timeout: 30000 }, async (t) => {
  let began;
  const streaming = new Promise((resolve) => { began = resolve; });
  let closed;
  const disconnected = new Promise((resolve) => { closed = resolve; });
  const run = await start(t, (_body, _index, response) => {
    response.writeHead(200, { "content-type": "text/event-stream" });
    response.write(`data: ${JSON.stringify({ id: "stall", choices: [{ index: 0, delta: { role: "assistant", content: "Working" }, finish_reason: null }] })}\n\n`);
    response.once("close", closed);
    began();
    return { handled: true };
  }, async () => { assert.fail("A streaming-only turn must not execute a host tool"); });
  const events = [];
  run.session.subscribe((event) => events.push(event));
  const pending = run.prompt("Stream until cancelled.");
  await streaming;
  await run.session.abort();
  await pending;
  await disconnected;
  assert.equal(run.session.isStreaming, false);
  assert.equal(events.findLast((event) => event.type === "agent_settled").aborted, true);
  assert.equal(run.server.requests.length, 1);
});

test("Pi propagates cancellation to the owned host tool request", { timeout: 30000 }, async (t) => {
  let began;
  const executing = new Promise((resolve) => { began = resolve; });
  let cancelled = false;
  const run = await start(t, () => ({ tool: toolCall }), async ({ signal }) => {
    assert.ok(signal);
    began();
    return new Promise((_resolve, reject) => {
      const abort = () => { cancelled = true; reject(new DOMException("Host fixture cancelled", "AbortError")); };
      signal.addEventListener("abort", abort, { once: true });
      if (signal.aborted) abort();
    });
  });
  const pending = run.prompt("Run a cancellable host fixture.");
  await executing;
  await run.session.abort();
  await pending;
  assert.equal(cancelled, true);
  assert.equal(run.server.requests.length, 1);
  assert.equal(run.session.isStreaming, false);
});

test("A model cannot activate Pi's excluded built-in file writer", { timeout: 30000 }, async (t) => {
  let run;
  run = await start(t, (body) => body.messages.at(-1).role === "tool"
    ? { text: "Unavailable tool was refused." }
    : { tool: { name: "write", arguments: { path: join(run.cwd, "unapproved.txt"), content: "must not be written" } } },
  async () => { assert.fail("An excluded SDK tool must never become a host request"); });
  await run.prompt("Attempt the unregistered file tool.");
  assert.equal(run.server.requests.length, 2);
  const failure = run.server.requests[1].body.messages.findLast((message) => message.role === "tool");
  assert.ok(failure.content.includes("write"));
  assert.equal(run.session.getLastAssistantText(), "Unavailable tool was refused.");
  await assert.rejects(access(join(run.cwd, "unapproved.txt")), { code: "ENOENT" });
});

test("Pi performs real SDK compaction and continues the host tool loop", { timeout: 30000 }, async (t) => {
  let calls = 0;
  let summaries = 0;
  const run = await start(t, (body) => {
    const user = lastUserText(body);
    if (user.includes("<conversation>")) { summaries++; return { text: "Checkpoint: earlier fixture dialogue completed; host work still requires host tools." }; }
    if (user === "Use host tool after compaction.")
      return body.messages.at(-1).role === "tool" ? { text: "Post-compaction host result received." } : { tool: toolCall };
    return { text: "A completed, deliberately long fixture response. ".repeat(240) };
  }, async () => { calls++; return { status: "success", assetId: "post-compaction-fixture-only" }; });
  for (let turn = 0; turn < 3; turn++) await run.prompt(`Fixture history turn ${turn}`);
  const compacted = await run.session.compact("Keep the host-tool requirement.");
  assert.ok(compacted.summary.includes("Checkpoint:"));
  assert.ok(summaries >= 1);
  assert.ok(run.session.sessionManager.getBranch().some((entry) => entry.type === "compaction"));
  await run.prompt("Use host tool after compaction.");
  assert.equal(calls, 1);
  assert.equal(run.session.getLastAssistantText(), "Post-compaction host result received.");
  assert.deepEqual(run.session.getCallableToolNames(), [tool.name]);
});

test("Pi model refusal is not automatically retried", { timeout: 30000 }, async (t) => {
  const run = await start(t, () => ({ status: 500 }), async () => { assert.fail("Provider refusal must not submit host work"); });
  await run.prompt("Refuse this provider request.");
  assert.equal(run.server.requests.length, 1);
  assert.equal(run.session.messages.at(-1).stopReason, "error");
});
