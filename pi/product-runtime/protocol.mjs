// Private product protocol: deliberately not the upstream Pi RPC interface.
export const PROTOCOL = "sicnu.pi/1";
export const LIMITS = Object.freeze({
  frameBytes: 2 * 1024 * 1024, promptBytes: 64 * 1024,
  argumentsBytes: 1024 * 1024, resultBytes: 512 * 1024,
  pending: 16, modelRequests: 16, toolAttempts: 32,
  totalTokens: 131072, outputTokens: 16384,
  modelIdleMs: 120000, executionMs: 15 * 60 * 1000,
});

export class ProtocolError extends Error {
  constructor(code = "INVALID_FRAME") { super(code); this.code = code; }
}

export class JsonLineDecoder {
  #buffer = Buffer.alloc(0);
  push(chunk) {
    const frames = [];
    let start = 0;
    for (let end = 0; end < chunk.length; end++) {
      if (chunk[end] !== 0x0a) continue;
      const part = chunk.subarray(start, end);
      if (this.#buffer.length + part.length > LIMITS.frameBytes) throw new ProtocolError("FRAME_TOO_LARGE");
      const bytes = Buffer.concat([this.#buffer, part]);
      this.#buffer = Buffer.alloc(0);
      try {
        const text = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes);
        frames.push(JSON.parse(text));
      } catch { throw new ProtocolError("INVALID_FRAME"); }
      start = end + 1;
    }
    const tail = chunk.subarray(start);
    if (this.#buffer.length + tail.length > LIMITS.frameBytes) throw new ProtocolError("FRAME_TOO_LARGE");
    this.#buffer = Buffer.concat([this.#buffer, tail]);
    return frames;
  }
  end() { if (this.#buffer.length) throw new ProtocolError("INCOMPLETE_FRAME"); }
}

const ENVELOPE = ["protocol", "sessionId", "generation", "runId", "seq", "type"];
const COMMAND_KEYS = Object.freeze({
  initialize: ["cwd", "profile", "systemPrompt", "tools", "toolRevision", "sdkReceiptSha256"],
  prompt: ["text"], "tool.result": ["callId", "result"], cancel: [], shutdown: [],
});
function object(value) { return value !== null && typeof value === "object" && !Array.isArray(value); }
function exact(value, keys) {
  if (!object(value) || Object.keys(value).length !== keys.length || keys.some((key) => !Object.hasOwn(value, key))) throw new ProtocolError();
}
function string(value, max, empty = false) {
  if (typeof value !== "string" || (!empty && !value) || Buffer.byteLength(value, "utf8") > max) throw new ProtocolError();
}
function identity(value, empty = false) { string(value, 256, empty); if (/[\u0000-\u001f\u007f]/.test(value)) throw new ProtocolError(); }
function jsonBytes(value, max) {
  try { if (Buffer.byteLength(JSON.stringify(value), "utf8") > max) throw new Error(); }
  catch { throw new ProtocolError("PAYLOAD_TOO_LARGE"); }
}
const zeroUsage = () => ({ input: 0, output: 0, cacheRead: 0, cacheWrite: 0, totalTokens: 0 });

/** One private in-memory conversation. SDK objects never cross this boundary. */
export class PrivateSession {
  #metadata; #createSession; #emitFrame; #identity; #inSeq = 0; #outSeq = 0;
  #state = "new"; #controlled; #tools; #revision; #active; #unsubscribers = []; #runIds = new Set(); #maxTokens; #clock;
  constructor({ metadata, createSession, emit, clock = { now: () => performance.now(), setTimeout, clearTimeout } }) {
    this.#metadata = metadata; this.#createSession = createSession; this.#emitFrame = emit; this.#clock = clock;
  }
  async accept(frame) {
    if (this.#state === "closed" || !object(frame) || !Object.hasOwn(COMMAND_KEYS, frame.type)) throw new ProtocolError("INVALID_COMMAND");
    exact(frame, [...ENVELOPE, ...COMMAND_KEYS[frame.type]]);
    if (frame.protocol !== PROTOCOL || !Number.isSafeInteger(frame.seq) || frame.seq !== this.#inSeq + 1) throw new ProtocolError("INVALID_ENVELOPE");
    identity(frame.sessionId); identity(frame.generation); identity(frame.runId, true);
    if (this.#identity && (frame.sessionId !== this.#identity.sessionId || frame.generation !== this.#identity.generation)) throw new ProtocolError("STALE_IDENTITY");
    jsonBytes(frame, LIMITS.frameBytes);
    if (frame.type === "initialize") {
      if (this.#state !== "new" || frame.runId !== "") throw new ProtocolError("INVALID_STATE");
      this.#validateInitialize(frame);
      this.#identity = { sessionId: frame.sessionId, generation: frame.generation };
      this.#inSeq = frame.seq; this.#state = "initializing";
      this.#revision = frame.toolRevision; this.#tools = structuredClone(frame.tools); this.#maxTokens = frame.profile.maxTokens;
      try {
        this.#controlled = await this.#createSession({ cwd: frame.cwd, profile: frame.profile, tools: this.#tools, systemPrompt: frame.systemPrompt, onToolRequest: (request) => this.#hostTool(request) });
        if (this.#state === "closed") { this.#controlled.session.dispose(); return; }
        this.#installBudgetBoundary();
        this.#unsubscribers.push(this.#controlled.session.subscribe((event) => this.#sessionEvent(event)));
        this.#state = "idle";
        this.#send("ready", "", { ...this.#metadata, toolRevision: this.#revision });
      } catch { this.#state = "closed"; throw new ProtocolError("INITIALIZATION_FAILED"); }
      return;
    }
    if (!this.#controlled || this.#state === "initializing" || this.#state === "new") throw new ProtocolError("NOT_READY");
    if (frame.type === "shutdown") {
      if (frame.runId !== "") throw new ProtocolError("INVALID_STATE");
      this.#inSeq = frame.seq; await this.disconnect("shutdown"); return;
    }
    if (frame.type === "prompt") {
      if (this.#state !== "idle" || !frame.runId || this.#runIds.has(frame.runId)) throw new ProtocolError("INVALID_STATE");
      string(frame.text, LIMITS.promptBytes);
      this.#inSeq = frame.seq; this.#runIds.add(frame.runId); this.#state = "running";
      const run = this.#active = { id: frame.runId, pending: new Map(), calls: new Set(), cancelled: false, settled: false, promptDone: false, modelRequests: 0, toolAttempts: 0, usage: zeroUsage(), executionRemaining: LIMITS.executionMs };
      this.#resumeExecution(run);
      Promise.resolve().then(() => this.#controlled.prompt(frame.text)).then(() => {
        run.promptDone = true; this.#finish(run);
      }, () => {
        run.promptDone = true; run.settled = true; this.#fail(run, "MODEL_FAILED"); this.#finish(run);
      });
      return;
    }
    const run = this.#active;
    if (!run || frame.runId !== run.id) throw new ProtocolError("STALE_RUN");
    if (frame.type === "cancel") {
      this.#inSeq = frame.seq; this.#cancel(run); return;
    }
    if (frame.type === "tool.result") {
      identity(frame.callId);
      if (!object(frame.result)) throw new ProtocolError();
      jsonBytes(frame.result, LIMITS.resultBytes);
      const pending = run.pending.get(frame.callId);
      if (!pending) throw new ProtocolError("UNKNOWN_CALL");
      this.#inSeq = frame.seq; run.pending.delete(frame.callId); pending.detach(); pending.resolve(frame.result);
      this.#resumeExecution(run);
      this.#finish(run); return;
    }
  }
  #validateInitialize(frame) {
    string(frame.cwd, 32768); string(frame.systemPrompt, LIMITS.promptBytes);
    identity(frame.toolRevision);
    if (!/^[a-f0-9]{64}$/.test(frame.sdkReceiptSha256) || frame.sdkReceiptSha256 !== this.#metadata.sdkReceiptSha256) throw new ProtocolError("SDK_RECEIPT_MISMATCH");
    exact(frame.profile, ["baseUrl", "model", "apiKey", "contextWindow", "maxTokens"]);
    string(frame.profile.baseUrl, 16384); string(frame.profile.model, 256); string(frame.profile.apiKey, 8192);
    for (const field of ["contextWindow", "maxTokens"]) if (!Number.isSafeInteger(frame.profile[field]) || frame.profile[field] <= 0) throw new ProtocolError();
    if (!Array.isArray(frame.tools) || frame.tools.length > 256) throw new ProtocolError();
    const names = new Set(), ids = new Set();
    for (const tool of frame.tools) {
      exact(tool, ["name", "canonicalId", "description", "parameters"]);
      if (!/^[A-Za-z0-9_-]{1,64}$/.test(tool.name) || names.has(tool.name)) throw new ProtocolError();
      identity(tool.canonicalId); string(tool.description, LIMITS.promptBytes, true);
      if (ids.has(tool.canonicalId) || !object(tool.parameters) || tool.parameters.type !== "object") throw new ProtocolError();
      names.add(tool.name); ids.add(tool.canonicalId);
    }
  }
  #send(type, runId, payload = {}) {
    const frame = { protocol: PROTOCOL, ...this.#identity, runId, seq: this.#outSeq + 1, type, ...payload };
    jsonBytes(frame, LIMITS.frameBytes);
    this.#emitFrame(frame); this.#outSeq++;
  }
  #sessionEvent(event) {
    const run = this.#active;
    if (!run || this.#state === "closed") return;
    if (event.type === "message_update") {
      const update = event.assistantMessageEvent;
      if (update?.type === "text_delta" || update?.type === "thinking_delta") {
        if (typeof update.delta === "string" && !run.cancelled && !run.errorCode)
          this.#send(update.type === "text_delta" ? "content" : "reasoning", run.id, { text: update.delta });
      }
    } else if (event.type === "auto_compaction_start") {
      this.#send("compaction", run.id, { stage: "started", reason: event.reason === "overflow" ? "overflow" : "threshold" });
    } else if (event.type === "auto_compaction_end") {
      this.#send("compaction", run.id, { stage: event.aborted || event.errorMessage ? "failed" : "completed", reason: event.aborted ? "cancelled" : event.errorMessage ? "failed" : "completed" });
    } else if (event.type === "message_end" && event.message?.role === "assistant" && event.message.stopReason === "error") {
      this.#fail(run, "MODEL_FAILED");
    } else if (event.type === "agent_settled") {
      run.settled = true; run.cancelled ||= event.aborted === true; this.#finish(run);
    }
  }
  #installBudgetBoundary() {
    const agent = this.#controlled.session.agent;
    // Both are documented public SDK seams. The stream function also covers
    // automatic compaction, which bypasses agent turn_start events.
    const original = agent.streamFunction;
    agent.streamFunction = async (model, context, options) => {
      const run = this.#active;
      this.#requireRunning(run);
      if (run.modelRequests >= LIMITS.modelRequests) this.#stop(run, "MODEL_REQUEST_LIMIT");
      if (run.usage.totalTokens >= LIMITS.totalTokens || run.usage.output >= LIMITS.outputTokens) this.#stop(run, "TOKEN_LIMIT");
      run.modelRequests++;
      const remainingOutput = LIMITS.outputTokens - run.usage.output;
      run.modelActive = true; this.#touchModel(run);
      let stream;
      try { stream = await original(model, context, { ...options, maxTokens: Math.min(options?.maxTokens ?? this.#maxTokens, remainingOutput) }); }
      catch (error) { this.#modelDone(run); throw error; }
      // Observe the single original completion immediately. Calling result()
      // repeatedly or consuming the same SSE iterator cannot double-charge it.
      const completion = Promise.resolve(stream.result()).then((message) => {
        this.#modelDone(run);
        if (this.#active === run && this.#state !== "closed") this.#recordUsage(run, message.usage);
        return message;
      }, (error) => { this.#modelDone(run); throw error; });
      void completion.catch(() => {});
      const touch = () => this.#touchModel(run);
      return {
        async *[Symbol.asyncIterator]() { for await (const event of stream) { touch(); yield event; } },
        result: () => completion,
      };
    };
    this.#unsubscribers.push(agent.subscribe(async (event) => {
      if (event.type !== "tool_execution_start") return;
      const run = this.#active;
      this.#requireRunning(run);
      if (run.toolAttempts >= LIMITS.toolAttempts) this.#stop(run, "TOOL_ATTEMPT_LIMIT");
      run.toolAttempts++;
    }));
  }
  #requireRunning(run) {
    if (!run || this.#state === "closed" || run.cancelled || run.errorCode) throw new ProtocolError("RUN_STOPPED");
  }
  #stop(run, code) { this.#fail(run, code); this.#cancel(run); throw new ProtocolError(code); }
  #timer(callback, ms) { const timer = this.#clock.setTimeout(callback, Math.max(0, ms)); timer?.unref?.(); return timer; }
  #timeout(run, code) {
    if (this.#active !== run || this.#state === "closed" || run.cancelled || run.errorCode) return;
    this.#fail(run, code); this.#cancel(run);
  }
  #touchModel(run) {
    if (!run.modelActive || run.cancelled || this.#state === "closed") return;
    this.#clock.clearTimeout(run.idleTimer);
    run.idleTimer = this.#timer(() => this.#timeout(run, "MODEL_IDLE_TIMEOUT"), LIMITS.modelIdleMs);
  }
  #modelDone(run) { run.modelActive = false; this.#clock.clearTimeout(run.idleTimer); }
  #pauseExecution(run) {
    if (run.executionStarted !== undefined) run.executionRemaining -= this.#clock.now() - run.executionStarted;
    run.executionStarted = undefined; this.#clock.clearTimeout(run.executionTimer);
  }
  #resumeExecution(run) {
    if (run.pending.size || run.cancelled || run.errorCode || this.#state === "closed" || run.executionStarted !== undefined) return;
    run.executionStarted = this.#clock.now();
    run.executionTimer = this.#timer(() => this.#timeout(run, "EXECUTION_TIMEOUT"), run.executionRemaining);
  }
  #recordUsage(run, source) {
    const usage = zeroUsage();
    for (const key of Object.keys(usage)) {
      const value = source?.[key] ?? 0;
      if (!Number.isSafeInteger(value) || value < 0) this.#stop(run, "INVALID_USAGE");
      usage[key] = value; run.usage[key] += value;
      if (!Number.isSafeInteger(run.usage[key])) this.#stop(run, "INVALID_USAGE");
    }
    this.#send("usage", run.id, { usage, cumulativeUsage: { ...run.usage } });
    if (run.usage.totalTokens > LIMITS.totalTokens || run.usage.output > LIMITS.outputTokens) this.#stop(run, "TOKEN_LIMIT");
  }
  #fail(run, code) {
    if (run.errorCode || this.#state === "closed") return;
    run.errorCode = code; this.#send("error", run.id, { code, message: "The agent run could not complete." });
  }
  #cancel(run) {
    if (run.cancelled) return;
    run.cancelled = true;
    this.#pauseExecution(run); this.#modelDone(run);
    for (const [callId, pending] of run.pending) {
      if (!pending.cancelSent) { pending.cancelSent = true; this.#send("tool.cancel", run.id, { callId }); }
    }
    void this.#controlled.session.abort().catch(() => {});
  }
  #finish(run) {
    if (this.#active !== run || !run.promptDone || !run.settled || run.pending.size || this.#state === "closed") return;
    this.#pauseExecution(run); this.#modelDone(run);
    this.#send("settled", run.id, { aborted: run.cancelled, status: run.errorCode ? "failed" : run.cancelled ? "cancelled" : "completed", ...(run.errorCode ? { errorCode: run.errorCode } : {}) });
    this.#active = undefined; this.#state = "idle";
  }
  async #hostTool({ toolCallId: callId, canonicalId, arguments: args, signal }) {
    const run = this.#active;
    if (!run || run.cancelled || run.errorCode || this.#state === "closed") throw new ProtocolError("NO_ACTIVE_RUN");
    identity(callId);
    const tool = this.#tools.find((entry) => entry.canonicalId === canonicalId);
    if (!tool || run.calls.has(callId) || !object(args)) throw new ProtocolError("INVALID_TOOL_CALL");
    jsonBytes(args, LIMITS.argumentsBytes);
    if (run.pending.size >= LIMITS.pending || run.pending.size !== 0) throw new ProtocolError("TOOL_CONCURRENCY");
    signal?.throwIfAborted();
    run.calls.add(callId);
    return new Promise((resolve, reject) => {
      const onAbort = () => {
        const pending = run.pending.get(callId);
        if (pending && !pending.cancelSent && this.#state !== "closed") {
          pending.cancelSent = true; this.#send("tool.cancel", run.id, { callId });
        }
      };
      const detach = () => signal?.removeEventListener("abort", onAbort);
      run.pending.set(callId, { resolve, reject, detach, cancelSent: false });
      this.#pauseExecution(run); this.#modelDone(run);
      signal?.addEventListener("abort", onAbort, { once: true });
      try {
        this.#send("tool.request", run.id, { callId, canonicalId, name: tool.name, arguments: args, toolRevision: this.#revision });
      } catch (error) {
        // No host can acknowledge a request the transport did not deliver.
        run.pending.delete(callId); detach(); this.#resumeExecution(run); reject(error);
      }
    });
  }
  async disconnect(reason = "disconnect") {
    if (this.#state === "closed") return;
    this.#state = "closed";
    if (this.#active) { this.#pauseExecution(this.#active); this.#modelDone(this.#active); }
    for (const pending of this.#active?.pending.values() ?? []) { pending.detach(); pending.reject(new ProtocolError("DISCONNECTED")); }
    this.#active?.pending.clear();
    for (const unsubscribe of this.#unsubscribers) unsubscribe();
    if (this.#identity) this.#send("closed", "", { reason });
    if (this.#controlled) { await this.#controlled.session.abort(); this.#controlled.session.dispose(); }
  }
  protocolFailure(code) {
    if (this.#identity && this.#state !== "closed") this.#send("error", this.#active?.id ?? "", { code, message: "The private agent protocol was rejected." });
  }
}
