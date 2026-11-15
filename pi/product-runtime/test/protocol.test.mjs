import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, mkdir, writeFile, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { createHash } from "node:crypto";
import { spawn } from "node:child_process";
import { JsonLineDecoder, ProtocolError, LIMITS, PrivateSession } from "../protocol.mjs";
import { loadInstallation } from "../main.mjs";
import { createControlledSession } from "../controlled-session.mjs";
import { openAiFixture } from "./fixtures/openai-server.mjs";

test("JSONL accepts split UTF-8 and rejects invalid bytes, partial EOF and oversized physical lines", () => {
  const decoder = new JsonLineDecoder();
  const bytes = Buffer.from('{"text":"中文"}\n');
  assert.deepEqual(decoder.push(bytes.subarray(0, 10)), []);
  assert.deepEqual(decoder.push(bytes.subarray(10)), [{ text: "中文" }]);
  decoder.end();
  assert.throws(() => new JsonLineDecoder().push(Buffer.from([0xff, 0x0a])), ProtocolError);
  assert.throws(() => new JsonLineDecoder().push(Buffer.alloc(LIMITS.frameBytes + 1, 0x20)), ProtocolError);
  const partial = new JsonLineDecoder();
  partial.push(Buffer.from("{}"));
  assert.throws(() => partial.end(), ProtocolError);
});

test("installed entry independently verifies its SDK receipt, versions and wrapper bytes", async (t) => {
  const prefix = await mkdtemp(join(tmpdir(), "Pi protocol 收据 "));
  t.after(() => rm(prefix, { recursive: true, force: true }));
  const share = join(prefix, "share/sicnu-geo-rs");
  const directory = join(prefix, "libexec/sicnu-geo-rs/pi-product");
  await mkdir(share, { recursive: true }); await mkdir(directory, { recursive: true });
  const sha = (bytes) => createHash("sha256").update(bytes).digest("hex");
  const sdk = { schema: "sicnu.pi-runtime/1", target: "x86_64-pc-windows-msvc", node_version: "24.22.3", pi_version: "1.5.2", recipes: { "b.json": "second", "a.json": "first" } };
  const sdkBytes = JSON.stringify(sdk);
  await writeFile(join(share, "pi-runtime.json"), sdkBytes);
  const files = [];
  for (const name of ["main.mjs", "protocol.mjs", "controlled-session.mjs", "checkpoint.mjs"]) {
    const bytes = `// fixture ${name}\n`;
    await writeFile(join(directory, name), bytes);
    files.push({ path: `libexec/sicnu-geo-rs/pi-product/${name}`, size: Buffer.byteLength(bytes), sha256: sha(bytes) });
  }
  const product = { schema: "sicnu.pi-product/1", protocol_version: 1, entry: "libexec/sicnu-geo-rs/pi-product/main.mjs", requires_sdk: {
    target: sdk.target, pi_version: sdk.pi_version, node_version: sdk.node_version,
    receipt_sha256: sha(sdkBytes), recipe_digest: sha('{"a.json":"first","b.json":"second"}'),
  }, files };
  await writeFile(join(share, "pi-product.json"), JSON.stringify(product));
  const entry = pathToFileURL(join(directory, "main.mjs"));
  const installed = await loadInstallation(entry);
  assert.equal(installed.metadata.sdkReceiptSha256, sha(sdkBytes));
  assert.equal(installed.nodeModules, join(prefix, "libexec/sicnu-geo-rs/pi-agent/node_modules"));
  await writeFile(join(directory, "protocol.mjs"), "// changed wrapper");
  await assert.rejects(loadInstallation(entry), /INSTALLATION_INVALID/);
  // Even restoring wrapper bytes cannot turn a host-claimed SDK identity into authority.
  await writeFile(join(directory, "protocol.mjs"), "// fixture protocol.mjs\n");
  product.requires_sdk.receipt_sha256 = "0".repeat(64);
  await writeFile(join(share, "pi-product.json"), JSON.stringify(product));
  await assert.rejects(loadInstallation(entry), /INSTALLATION_INVALID/);
});

test("installed private entry exits when the host disconnects during SDK initialization", { timeout: 10000 }, async (t) => {
  const prefix = await mkdtemp(join(tmpdir(), "Pi init EOF "));
  t.after(() => rm(prefix, { recursive: true, force: true }));
  const directory = join(prefix, "libexec/sicnu-geo-rs/pi-product");
  const share = join(prefix, "share/sicnu-geo-rs");
  await mkdir(directory, { recursive: true }); await mkdir(share, { recursive: true });
  const sha = (bytes) => createHash("sha256").update(bytes).digest("hex");
  const files = [];
  for (const name of ["main.mjs", "protocol.mjs", "controlled-session.mjs", "checkpoint.mjs"]) {
    const bytes = await readFile(new URL(`../${name}`, import.meta.url));
    await writeFile(join(directory, name), bytes);
    files.push({ path: `libexec/sicnu-geo-rs/pi-product/${name}`, size: bytes.length, sha256: sha(bytes) });
  }
  // These owned fixture modules stop at import, before any SDK functionality.
  // A real outstanding timer makes a missing EOF handler observable as a hang.
  for (const name of ["pi-coding-agent", "pi-ai"]) {
    const packageDir = join(prefix, "libexec/sicnu-geo-rs/pi-agent/node_modules/@earendil-works", name);
    await mkdir(join(packageDir, "dist"), { recursive: true });
    await writeFile(join(packageDir, "package.json"), JSON.stringify({ version: "1.5.2", type: "module" }));
    await writeFile(join(packageDir, "dist/index.js"), name === "pi-coding-agent" ? 'process.stderr.write("FIXTURE_INITIALIZING\\n"); await new Promise(resolve => setTimeout(resolve, 30000));' : "export {};");
  }
  const sdk = { schema: "sicnu.pi-runtime/1", target: "x86_64-pc-windows-msvc", node_version: "24.22.3", pi_version: "1.5.2", recipes: {} };
  const sdkBytes = JSON.stringify(sdk), sdkReceiptSha256 = sha(sdkBytes);
  await writeFile(join(share, "pi-runtime.json"), sdkBytes);
  await writeFile(join(share, "pi-product.json"), JSON.stringify({ schema: "sicnu.pi-product/1", protocol_version: 1, entry: "libexec/sicnu-geo-rs/pi-product/main.mjs", files,
    requires_sdk: { target: sdk.target, node_version: sdk.node_version, pi_version: sdk.pi_version, receipt_sha256: sdkReceiptSha256, recipe_digest: sha("{}") } }));
  const child = spawn(process.execPath, [join(directory, "main.mjs")], { windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
  t.after(() => { if (child.exitCode === null) child.kill(); });
  const decoder = new JsonLineDecoder(), frames = [];
  child.stdout.on("data", (bytes) => frames.push(...decoder.push(bytes)));
  child.stderr.on("data", (bytes) => { if (bytes.toString().includes("FIXTURE_INITIALIZING")) child.stdin.end(); });
  const completion = new Promise((resolve, reject) => {
    const timer = setTimeout(() => { child.kill(); reject(new Error("EOF during initialize left the owned Node running")); }, 4000);
    child.once("error", (error) => { clearTimeout(timer); reject(error); });
    child.once("exit", (code) => { clearTimeout(timer); resolve(code); });
  });
  child.stdin.write(`${JSON.stringify({ protocol: "sicnu.pi/1", sessionId: "init-eof", generation: "1", runId: "", seq: 1, type: "initialize", ...initialize, cwd: prefix, sdkReceiptSha256 })}\n`);
  assert.equal(await completion, 0);
  assert.equal(frames.at(-1)?.type, "closed");
  assert.equal(frames.at(-1)?.reason, "eof");
  assert.equal(frames.some((frame) => frame.type === "ready"), false);
});

const hash = "a".repeat(64);
const tool = { name: "ndvi_mask", canonicalId: "rs:ndvi-mask", description: "Change mask", parameters: { type: "object" } };
const initialize = {
  cwd: "C:/owned", profile: { baseUrl: "https://model.invalid/v1", model: "fixture", apiKey: "fixture-secret", contextWindow: 16384, maxTokens: 4096 },
  systemPrompt: "Use the host tools", tools: [tool], toolRevision: "qt-catalog-1", sdkReceiptSha256: hash,
};
const tick = () => new Promise((resolve) => setImmediate(resolve));
function fixture(script = async () => {}, { clock, streamFactory, failEmit } = {}) {
  const frames = [], listeners = new Set(), agentListeners = new Set();
  let host, calls = 0, aborted = false;
  const sdk = {
    session: {
      agent: {
        streamFunction: async () => { calls++; return streamFactory ? streamFactory() : { push() {}, async *[Symbol.asyncIterator]() {}, result: async () => ({ usage: { input: 30, output: 20, cacheRead: 0, cacheWrite: 0, totalTokens: 50 } }) }; },
        subscribe: (listener) => { agentListeners.add(listener); return () => agentListeners.delete(listener); },
      },
      subscribe: (listener) => { listeners.add(listener); return () => listeners.delete(listener); },
      abort: async () => { aborted = true; }, dispose() {},
    },
    prompt: async (text) => {
      try { await script({ text, host, sdk, emit: (event) => { for (const listener of listeners) listener(event); }, agentEmit: async (event) => { for (const listener of agentListeners) await listener(event); } }); }
      finally { for (const listener of listeners) listener({ type: "agent_settled", aborted }); }
    },
  };
  const protocol = new PrivateSession({
    metadata: { nodeVersion: "24.22.3", piVersion: "1.5.2", sdkReceiptSha256: hash },
    createSession: async (options) => { host = options.onToolRequest; return sdk; }, emit: (frame) => { failEmit?.(frame); frames.push(frame); }, clock,
  });
  let seq = 0;
  const send = (type, payload = {}, runId = "") => protocol.accept({ protocol: "sicnu.pi/1", sessionId: "session", generation: "epoch-1", runId, seq: ++seq, type, ...payload });
  return { protocol, send, frames, requests: () => calls };
}

test("private session validates identity, revision and strict commands before SDK initialization", async () => {
  const f = fixture();
  await f.send("initialize", initialize);
  assert.deepEqual(f.frames[0], { protocol: "sicnu.pi/1", sessionId: "session", generation: "epoch-1", runId: "", seq: 1, type: "ready", nodeVersion: "24.22.3", piVersion: "1.5.2", sdkReceiptSha256: hash, toolRevision: "qt-catalog-1" });
  await assert.rejects(f.send("prompt", { text: "hi", approved: true }, "r1"), ProtocolError);
  assert.equal(f.requests(), 0);
  await f.protocol.disconnect();
  const wrongReceipt = fixture();
  await assert.rejects(wrongReceipt.send("initialize", { ...initialize, sdkReceiptSha256: "b".repeat(64) }), ProtocolError);
  assert.equal(wrongReceipt.frames.length, 0);
  const stale = fixture();
  await stale.send("initialize", initialize);
  await assert.rejects(stale.protocol.accept({ protocol: "sicnu.pi/1", sessionId: "session", generation: "older", runId: "r1", seq: 2, type: "prompt", text: "hi" }), ProtocolError);
  await stale.protocol.disconnect();
});

function manualClock() {
  let now = 0, nextId = 0;
  const timers = new Map();
  return {
    now: () => now,
    setTimeout: (fn, ms) => { const id = ++nextId; timers.set(id, { fn, at: now + ms }); return id; },
    clearTimeout: (id) => timers.delete(id),
    advance(ms) { now += ms; for (const [id, timer] of [...timers]) if (timer.at <= now && timers.delete(id)) timer.fn(); },
  };
}

test("host waiting pauses execution time while a silent model request hits its idle deadline", async () => {
  const clock = manualClock();
  const f = fixture(async ({ host }) => { await host({ toolCallId: "waiting", canonicalId: tool.canonicalId, arguments: {} }); }, { clock });
  await f.send("initialize", initialize);
  await f.send("prompt", { text: "wait for native approval" }, "host-wait");
  await tick();
  clock.advance(16 * 60 * 1000);
  assert.equal(f.frames.some((frame) => frame.type === "error"), false);
  await f.send("tool.result", { callId: "waiting", result: { status: "completed" } }, "host-wait");
  await tick();
  assert.equal(f.frames.at(-1).status, "completed");
  await f.protocol.disconnect();
  const idleClock = manualClock();
  const idle = fixture(async ({ sdk }) => { const stream = await sdk.session.agent.streamFunction({}, {}, {}); await stream.result(); }, {
    clock: idleClock, streamFactory: () => ({ push() {}, async *[Symbol.asyncIterator]() {}, result: () => new Promise(() => {}) }),
  });
  await idle.send("initialize", initialize);
  await idle.send("prompt", { text: "silent model" }, "model-idle");
  await tick();
  idleClock.advance(120001);
  assert.equal(idle.frames.find((frame) => frame.type === "error").code, "MODEL_IDLE_TIMEOUT");
  await idle.protocol.disconnect();
});

test("result-only compaction remains active across 180 seconds of public stream events without consuming its iterator", async () => {
  const clock = manualClock(), forwarded = [];
  let resolveResult, iterations = 0;
  const completion = new Promise((resolve) => { resolveResult = resolve; });
  const source = {
    push(event) { forwarded.push(event); if (event.type === "done") resolveResult(event.message); },
    async *[Symbol.asyncIterator]() { iterations++; throw new Error("Result-only compaction must not require a second stream consumer"); },
    result: () => completion,
  };
  const f = fixture(async ({ sdk, emit }) => {
    emit({ type: "compaction_start", reason: "threshold" });
    const stream = await sdk.session.agent.streamFunction({}, {}, {});
    await stream.result();
    await stream.result();
    emit({ type: "compaction_end", aborted: false });
  }, { clock, streamFactory: () => source });
  await f.send("initialize", initialize);
  await f.send("prompt", { text: "Summarize the context" }, "result-only-compaction");
  await tick();
  for (let index = 0; index < 3; index++) {
    clock.advance(60000);
    source.push({ type: "text_delta", delta: `checkpoint ${index}` });
    await tick();
  }
  source.push({ type: "done", message: { usage: { input: 30, output: 20, cacheRead: 0, cacheWrite: 0, totalTokens: 50 } } });
  await tick();
  assert.equal(clock.now(), 180000);
  assert.equal(f.frames.at(-1).status, "completed");
  assert.equal(f.frames.some((frame) => frame.type === "error"), false);
  assert.equal(iterations, 0);
  assert.equal(forwarded.length, 4);
  assert.equal(f.frames.filter((frame) => frame.type === "usage").length, 1);
  assert.equal(f.frames.find((frame) => frame.type === "usage").cumulativeUsage.totalTokens, 50);
  await f.protocol.disconnect();
});

test("host tools carry the frozen allowlist identity and cancellation settles only after host acknowledgement", async () => {
  const f = fixture(async ({ host, emit }) => {
    emit({ type: "message_update", assistantMessageEvent: { type: "text_delta", delta: "分析中" } });
    const result = await host({ toolCallId: "call-1", canonicalId: tool.canonicalId, arguments: { threshold: 0.2 } });
    assert.deepEqual(result, { status: "cancelled", assets: [] });
  });
  await f.send("initialize", initialize);
  await f.send("prompt", { text: "NDVI mask" }, "run-1");
  await tick();
  assert.deepEqual(f.frames.find((frame) => frame.type === "tool.request"), {
    protocol: "sicnu.pi/1", sessionId: "session", generation: "epoch-1", runId: "run-1", seq: 3, type: "tool.request",
    callId: "call-1", canonicalId: tool.canonicalId, name: tool.name, arguments: { threshold: 0.2 }, toolRevision: "qt-catalog-1",
  });
  await f.send("cancel", {}, "run-1");
  await tick();
  assert.equal(f.frames.at(-1).type, "tool.cancel");
  assert.equal(f.frames.some((frame) => frame.type === "settled"), false);
  await assert.rejects(f.send("tool.result", { callId: "other-call", result: { status: "cancelled" } }, "run-1"), ProtocolError);
  // A rejected frame is not acknowledged and cannot advance the accepted sequence.
  await f.protocol.accept({ protocol: "sicnu.pi/1", sessionId: "session", generation: "epoch-1", seq: 4, runId: "run-1", type: "tool.result", callId: "call-1", result: { status: "cancelled", assets: [] } });
  await tick();
  assert.deepEqual(f.frames.at(-1), { protocol: "sicnu.pi/1", sessionId: "session", generation: "epoch-1", runId: "run-1", seq: 5, type: "settled", aborted: true, status: "cancelled" });
  await f.protocol.disconnect();
});

test("a failed tool-request transport does not leave an undelivered call waiting forever", async () => {
  const f = fixture(async ({ host }) => {
    await host({ toolCallId: "undelivered", canonicalId: tool.canonicalId, arguments: {} });
  }, { failEmit: (frame) => { if (frame.type === "tool.request") throw new Error("Fixture transport failure"); } });
  await f.send("initialize", initialize);
  await f.send("prompt", { text: "fixture transport" }, "transport-run");
  await tick();
  assert.equal(f.frames.some((frame) => frame.type === "tool.request"), false);
  assert.equal(f.frames.at(-1).type, "settled");
  assert.equal(f.frames.at(-1).status, "failed");
  assert.deepEqual(f.frames.map((frame) => frame.seq), [1, 2, 3]);
  await f.protocol.disconnect();
});

test("request budget blocks request 17 before the SDK provider and counts compaction usage exactly once", async () => {
  const f = fixture(async ({ sdk, emit }) => {
    for (let index = 0; index < 17; index++) {
      if (index === 0) emit({ type: "compaction_start", reason: "threshold" });
      const stream = await sdk.session.agent.streamFunction({}, {}, {});
      await stream.result();
      await stream.result();
      if (index === 0) emit({ type: "compaction_end", aborted: false });
    }
  });
  await f.send("initialize", initialize);
  await f.send("prompt", { text: "loop" }, "request-budget");
  await tick();
  assert.equal(f.requests(), 16);
  const usage = f.frames.filter((frame) => frame.type === "usage");
  assert.equal(usage.length, 16);
  assert.deepEqual(usage.at(-1).cumulativeUsage, { input: 480, output: 320, cacheRead: 0, cacheWrite: 0, totalTokens: 800 });
  assert.deepEqual(f.frames.filter((frame) => frame.type === "compaction").map((frame) => frame.stage), ["started", "completed"]);
  assert.equal(f.frames.at(-1).errorCode, "MODEL_REQUEST_LIMIT");
  await f.protocol.disconnect();
});

test("tool attempt budget includes SDK refusals and blocks attempt 33 before tool preparation", async () => {
  let prepared = 0;
  const f = fixture(async ({ agentEmit }) => {
    for (let index = 0; index < 33; index++) {
      await agentEmit({ type: "tool_execution_start", toolCallId: `refused-${index}`, toolName: "unknown", args: {} });
      prepared++;
    }
  });
  await f.send("initialize", initialize);
  await f.send("prompt", { text: "attempts" }, "tool-budget");
  await tick();
  assert.equal(prepared, 32);
  assert.equal(f.frames.at(-1).errorCode, "TOOL_ATTEMPT_LIMIT");
  assert.equal(f.frames.some((frame) => frame.type === "tool.request"), false);
  await f.protocol.disconnect();
});

const nodeModules = process.env.SICNU_TEST_PI_NODE_MODULES;
async function realProtocol(t, server, tools = [tool], { clock, profile = {}, onFrame } = {}) {
  const cwd = await mkdtemp(join(tmpdir(), "Pi private 本地 "));
  t.after(() => rm(cwd, { recursive: true, force: true }));
  process.env.PI_OFFLINE = "1"; process.env.PI_TELEMETRY = "0";
  const frames = [];
  let resolveSettled;
  const settled = new Promise((resolve) => { resolveSettled = resolve; });
  let seq = 0, toolRequests = 0;
  const send = (type, payload = {}, runId = "") => protocol.accept({ protocol: "sicnu.pi/1", sessionId: "real-session", generation: "1", runId, seq: ++seq, type, ...payload });
  const protocol = new PrivateSession({
    metadata: { nodeVersion: "24.22.3", piVersion: "1.5.2", sdkReceiptSha256: hash },
    createSession: (options) => createControlledSession({ ...options, nodeModules }),
    clock,
    emit(frame) {
      frames.push(frame);
      onFrame?.(frame);
      if (frame.type === "tool.request") {
        toolRequests++;
        queueMicrotask(() => { void send("tool.result", { callId: frame.callId, result: { status: "error", message: "Fixture native refusal" } }, frame.runId); });
      }
      if (frame.type === "settled") resolveSettled(frame);
    },
  });
  t.after(() => protocol.disconnect());
  await send("initialize", { ...initialize, cwd, profile: { ...initialize.profile, baseUrl: server.baseUrl, ...profile }, tools });
  return { protocol, send, frames, settled, toolRequests: () => toolRequests };
}

test("real SDK private protocol stops before model request 17", { skip: !nodeModules, timeout: 60000 }, async (t) => {
  const server = await openAiFixture((_body, index) => ({ tool: { name: tool.name, id: `call-${index}`, arguments: {} } }));
  t.after(() => server.close());
  const f = await realProtocol(t, server);
  await f.send("prompt", { text: "Continue attempting the host tool" }, "budget-run");
  const settled = await f.settled;
  assert.equal(settled.errorCode, "MODEL_REQUEST_LIMIT");
  assert.equal(server.requests.length, 16);
  assert.equal(f.toolRequests(), 16);
  assert.equal(f.frames.filter((frame) => frame.type === "usage").at(-1).cumulativeUsage.totalTokens, 800);
});

test("real SDK automatic compaction preserves result-only stream activity and counts each summary usage once", { skip: !nodeModules, timeout: 60000 }, async (t) => {
  const clock = manualClock();
  let idleArms = 0, summaries = 0;
  const schedule = clock.setTimeout;
  clock.setTimeout = (callback, ms) => { if (ms === LIMITS.modelIdleMs) idleArms++; return schedule(callback, ms); };
  const server = await openAiFixture(async (body, index, response) => {
    const user = body.messages.findLast((message) => message.role === "user");
    const text = typeof user?.content === "string" ? user.content : user?.content?.map((part) => part.text ?? "").join("");
    const summary = text?.includes("<conversation>") || text?.startsWith("# Conversation\n");
    if (index === 1) return { text: "Earlier completed fixture history. ".repeat(400) };
    response.writeHead(200, { "content-type": "text/event-stream" });
    const emit = (delta, finish_reason = null) => response.write(`data: ${JSON.stringify({ id: "compaction-fixture", object: "chat.completion.chunk", model: body.model, choices: [{ index: 0, delta, finish_reason }] })}\n\n`);
    const activeChunk = async (delta) => {
      const before = idleArms;
      emit(delta);
      // Synchronize only with the injected public clock boundary: let the real
      // HTTP parser deliver this SSE event before advancing virtual time again.
      const until = performance.now() + 2000;
      while (idleArms === before && performance.now() < until) await new Promise((resolve) => setTimeout(resolve, 2));
      assert.ok(idleArms > before, "SSE activity must rearm the model idle deadline");
    };
    if (summary) {
      summaries++;
      await activeChunk({ role: "assistant", content: "" });
      for (let chunk = 0; chunk < 3; chunk++) {
        clock.advance(60000);
        await activeChunk({ content: `Checkpoint segment ${chunk}. ` });
      }
    } else {
      emit({ role: "assistant", content: "" });
      emit({ content: "Current completed fixture context. ".repeat(400) });
    }
    emit({}, "stop");
    const input = summary ? 30 : 7500;
    response.end(`data: ${JSON.stringify({ choices: [], usage: { prompt_tokens: input, completion_tokens: 20, total_tokens: input + 20 } })}\n\ndata: [DONE]\n\n`);
    return { handled: true };
  });
  t.after(() => server.close());
  let resolveTarget;
  const targetSettled = new Promise((resolve) => { resolveTarget = resolve; });
  const f = await realProtocol(t, server, [], { clock, profile: { contextWindow: 8192 }, onFrame: (frame) => { if (frame.type === "settled" && frame.runId === "compact-target") resolveTarget(frame); } });
  await f.send("prompt", { text: "Seed earlier completed history." }, "compact-seed");
  assert.equal((await f.settled).status, "completed");
  await f.send("prompt", { text: "Complete the next long context." }, "compact-target");
  assert.equal((await targetSettled).status, "completed");
  assert.ok(summaries >= 1, "The actual SDK must request a compaction summary");
  assert.ok(server.requests.some(({ body }) => JSON.stringify(body.messages).includes("<conversation>")), "A real serialized conversation summary request must reach the local provider");
  assert.ok(clock.now() >= 180000 && clock.now() < LIMITS.executionMs);
  const events = f.frames.filter((frame) => frame.runId === "compact-target");
  assert.deepEqual(events.filter((frame) => frame.type === "compaction").map((frame) => frame.stage), ["started", "completed"]);
  assert.equal(events.some((frame) => frame.type === "error"), false);
  const usage = events.filter((frame) => frame.type === "usage");
  assert.equal(usage.length, summaries + 1);
  assert.equal(usage.at(-1).cumulativeUsage.totalTokens, 7520 + summaries * 50);
  assert.equal(usage.at(-1).cumulativeUsage.output, (summaries + 1) * 20);
});

test("real SDK private protocol counts unknown tools and blocks tool attempt 33 before host execution", { skip: !nodeModules, timeout: 60000 }, async (t) => {
  const server = await openAiFixture((body, _index, response) => {
    response.writeHead(200, { "content-type": "text/event-stream" });
    const emit = (delta, finish_reason = null) => response.write(`data: ${JSON.stringify({ id: "many-tools", object: "chat.completion.chunk", model: body.model, choices: [{ index: 0, delta, finish_reason }] })}\n\n`);
    emit({ role: "assistant", content: "" });
    emit({ tool_calls: Array.from({ length: 33 }, (_, index) => ({ index, id: `attempt-${index}`, type: "function", function: { name: index % 2 ? "unknown_refused_tool" : tool.name, arguments: "{}" } })) });
    emit({}, "tool_calls");
    response.end('data: {"choices":[],"usage":{"prompt_tokens":30,"completion_tokens":20,"total_tokens":50}}\n\ndata: [DONE]\n\n');
    return { handled: true };
  });
  t.after(() => server.close());
  const f = await realProtocol(t, server);
  await f.send("prompt", { text: "Attempt this tool batch" }, "tools-run");
  const settled = await f.settled;
  assert.equal(settled.errorCode, "TOOL_ATTEMPT_LIMIT");
  assert.equal(server.requests.length, 1);
  assert.equal(f.toolRequests(), 16); // 16 unknown refusals also consume the first 32 attempts.
  assert.equal(f.frames.some((frame) => frame.callId === "attempt-32"), false);
});

const productPrefix = process.env.SICNU_TEST_PI_PRODUCT_PREFIX;
test("relocated installed entry completes a real SDK tool loop using only private JSONL", { skip: !productPrefix, timeout: 60000 }, async (t) => {
  const cwd = await mkdtemp(join(tmpdir(), "Pi relocated 私有 "));
  t.after(() => rm(cwd, { recursive: true, force: true }));
  const server = await openAiFixture((_body, index) => index === 1 ? { tool: { name: tool.name, arguments: { threshold: 0.2 } } } : { text: "NDVI fixture asset delivered" });
  t.after(() => server.close());
  const { readFile } = await import("node:fs/promises");
  const sdkReceiptSha256 = createHash("sha256").update(await readFile(join(productPrefix, "share/sicnu-geo-rs/pi-runtime.json"))).digest("hex");
  const env = { ...process.env, PATH: "", HOME: cwd, USERPROFILE: cwd, XDG_CONFIG_HOME: cwd };
  for (const key of ["NODE_OPTIONS", "NODE_PATH", "SICNU_TEST_PI_NODE_MODULES", "SICNU_TEST_PI_PRODUCT_PREFIX"]) delete env[key];
  const child = spawn(join(productPrefix, "libexec/sicnu-geo-rs/node/node.exe"), [join(productPrefix, "libexec/sicnu-geo-rs/pi-product/main.mjs")], { cwd, env, windowsHide: true, stdio: ["pipe", "pipe", "pipe"] });
  t.after(() => { if (child.exitCode === null) child.kill(); });
  const decoder = new JsonLineDecoder(), frames = [];
  let seq = 0, stderr = "";
  const send = (type, payload = {}, runId = "") => child.stdin.write(`${JSON.stringify({ protocol: "sicnu.pi/1", sessionId: "relocated", generation: "opaque-epoch", runId, seq: ++seq, type, ...payload })}\n`);
  const completion = new Promise((resolve, reject) => {
    child.once("error", reject);
    child.stderr.on("data", (chunk) => { stderr += chunk.toString(); });
    child.stdout.on("data", (chunk) => {
      try {
        for (const frame of decoder.push(chunk)) {
          assert.equal(frame.seq, frames.length + 1); frames.push(frame);
          if (frame.type === "ready") send("prompt", { text: "Run NDVI fixture mask" }, "relocated-run");
          if (frame.type === "tool.request") {
            assert.equal(frame.canonicalId, tool.canonicalId);
            assert.equal(frame.toolRevision, "qt-catalog-1");
            send("tool.result", { callId: frame.callId, result: { status: "completed", assets: [{ id: "fixture-mask-asset" }] } }, frame.runId);
          }
          if (frame.type === "settled") { assert.equal(frame.status, "completed"); send("shutdown"); }
        }
      } catch (error) { child.kill(); reject(error); }
    });
    child.once("exit", (code) => { try { assert.equal(code, 0, stderr); resolve(); } catch (error) { reject(error); } });
  });
  send("initialize", { ...initialize, cwd, profile: { ...initialize.profile, baseUrl: server.baseUrl }, sdkReceiptSha256 });
  await completion;
  assert.equal(server.requests.length, 2);
  assert.ok(server.requests[1].body.messages.some((message) => message.role === "tool" && message.content.includes("fixture-mask-asset")));
  assert.equal(frames.at(-1).type, "closed");
  assert.equal(frames.filter((frame) => frame.type === "usage").at(-1).cumulativeUsage.totalTokens, 100);
  assert.ok(frames.some((frame) => frame.type === "content" && frame.text.includes("NDVI fixture asset delivered")));
  assert.equal(stderr.includes("fixture-secret"), false);
});
