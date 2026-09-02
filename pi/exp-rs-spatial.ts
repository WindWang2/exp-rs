/**
 * exp-rs Spatial Intelligence bridge for the Pi agent runtime (ADR 0122).
 *
 * Pi (https://pi.dev) ships no MCP client support by design, so this
 * extension owns the transport: it spawns the exp-rs binary in `--mcp`
 * mode, performs the MCP JSON-RPC handshake over stdio, and registers every
 * server tool as a first-class Pi tool. The agent loop, planning, and
 * reasoning stay in Pi; spatial understanding, algorithms, workflows, and
 * models stay in exp-rs.
 *
 * Usage:
 *   pi -e pi/exp-rs-spatial.ts
 *
 * Configuration (environment):
 *   EXP_RS_MCP_BIN        binary to launch (default: auto-detect build dir)
 *   EXP_RS_MCP_ARGS       extra CLI args appended to --mcp
 *   EXP_RS_TOOL_CATEGORIES comma-separated tool prefixes to bridge
 *                         (default: "meta,spatial,data,temporal"; e.g. add "rs,gdal,otb")
 *   SICNU_MCP_WORKSPACE   passed through to restrict server file access
 *   SICNU_MODELS_DIR      passed through to locate model manifests
 */
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { spawn, type ChildProcess } from "node:child_process";
import { existsSync, statSync } from "node:fs";

/** Max characters of a tool result kept (tail preserved). */
const MAX_RESULT_CHARS = 50_000;
const REQUEST_TIMEOUT_MS = 10 * 60 * 1000;
const STARTUP_TIMEOUT_MS = 30 * 1000;
const MAX_LINE_BUFFER_CHARS = 32 * 1024 * 1024;

type Pending = {
  resolve: (value: any) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
};

/** Live bridges, so the (single) process-exit hook tears all of them down.
 * Extension reload used to leak the previous child for the whole session
 * because start() re-registered `process.on("exit")` per spawn and Pi never
 * invoked an unload hook (#645). */
const activeBridges = new Set<McpBridge>();
let exitHookInstalled = false;
function installExitHook(): void {
  if (exitHookInstalled) return;
  exitHookInstalled = true;
  // During exit, timers never fire, so stop()'s SIGKILL escalation cannot
  // run. Best-effort: SIGTERM + destroy stdin - the server exits on stdin
  // EOF, and the OS reaps the child when the parent dies.
  process.on("exit", () => {
    for (const bridge of activeBridges) bridge.stop();
  });
}

/** Minimal newline-delimited JSON-RPC client for the exp-rs MCP server. */
class McpBridge {
  private child: ChildProcess | null = null;
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private buffer = "";
  private startError: string | null = null;
  private exited = false;
  private stopped = false;
  private stderrTail = "";
  // Respawn bookkeeping (#669): a crash-looping child used to drive an
  // unbounded spawn loop because `exited` was never reset in start(), so
  // every request re-entered the respawn branch and leaked another child.
  private starting: Promise<void> | null = null;
  private lastSpawnAt = 0;
  private fastCrashCount = 0;
  private lastExitError: string | null = null;

  constructor(private readonly bin: string, private readonly extraArgs: string[]) {
    installExitHook();
    activeBridges.add(this);
  }

  get error(): string | null {
    return this.startError;
  }

  async start(): Promise<void> {
    // One spawn at a time: concurrent requests hitting the lazy-respawn
    // branch must share a single in-flight start, not spawn N children.
    if (this.starting) return this.starting;
    this.starting = (async () => {
      await this.spawn();
    })();
    try {
      await this.starting;
    } finally {
      this.starting = null;
    }
  }

  private async spawn(): Promise<void> {
    if (this.stopped) {
      throw new Error("exp-rs MCP bridge was stopped");
    }
    // Startup-race guard: if a previous spawn's child is somehow still alive
    // (e.g. an initialize timeout left it hanging with exited=false), kill it
    // before spawning a replacement — otherwise it blocks every respawn while
    // its stdio goes nowhere (review P2).
    if (this.child && !this.exited) {
      try {
        this.child.kill();
      } catch {
        /* already dead */
      }
    }
    const childEnv: Record<string, string> = {};
    for (const [k, v] of Object.entries(process.env)) {
      if (v !== undefined) childEnv[k] = v;
    }
    // The desktop binary resolves its own Qt/QGIS stack; inherited launcher
    // library paths (AppImage mounts, snap/flatpak) break its plugins.
    delete childEnv.LD_LIBRARY_PATH;
    childEnv.QT_QPA_PLATFORM = "offscreen";

    // Reset the stale crash state BEFORE spawning (#669): `exited` left over
    // from a previous crash made every subsequent request re-enter the
    // respawn branch and spawn yet another child — a fork bomb of leaked
    // processes while the (healthy) new child was already running.
    this.exited = false;
    this.startError = null;
    this.stderrTail = "";
    this.lastSpawnAt = Date.now();
    this.child = spawn(this.bin, ["--mcp", ...this.extraArgs], {
      stdio: ["pipe", "pipe", "pipe"],
      env: childEnv,
    });
    // spawn() reports late ENOENT/EACCES via an "error" event, not a throw.
    const spawnFailure = new Promise<never>((_, reject) => {
      this.child!.once("error", (err) => {
        this.startError = `Failed to launch ${this.bin}: ${err?.message ?? err}`;
        reject(new Error(this.startError));
      });
    });

    this.child.stdout!.setEncoding("utf8");
    this.child.stdout!.on("data", (chunk) => this.onStdout(chunk));
    // Keep a stderr tail so a child crash is diagnosable from exprs_status
    // and the exit error (Qt/QGIS plugin failures precede most crashes).
    this.child.stderr!.setEncoding("utf8");
    this.child.stderr!.on("data", (chunk: string) => {
      this.stderrTail = (this.stderrTail + chunk).slice(-4000);
    });
    // Persistent handler: once("error") left later child errors unhandled
    // (EventEmitter throws on an "error" event with no listener).
    this.child.on("error", (err) => {
      this.startError = `exp-rs MCP server error: ${err?.message ?? err}`;
    });
    this.child.on("exit", (code) => {
      this.exited = true;
      const tail = this.stderrTail.trim();
      const err = new Error(
        `exp-rs MCP server exited (code ${code})${tail ? `; stderr tail:\n${tail}` : ""}`,
      );
      this.lastExitError = err.message;
      // Circuit breaker accounting: a child dying within 10s of spawn is a
      // fast crash; 5 in a row disables lazy respawn instead of looping.
      if (Date.now() - this.lastSpawnAt < 10_000) this.fastCrashCount++;
      else this.fastCrashCount = 0;
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(err);
      }
      this.pending.clear();
    });
    this.child.stdin!.on("error", () => {});
    // Process-exit teardown is installed once at module level (see
    // installExitHook) - registering it here stacked a listener per
    // (re)spawn and MaxListeners-warned after ~10 respawns (#645).

    // Startup deadline: a child that spawns but hangs during Qt/QGIS init
    // must not stall extension load for the full 10-minute request timeout.
    const startupDeadline = new Promise<never>((_, reject) => {
      const t = setTimeout(
        () =>
          reject(
            new Error(
              `exp-rs MCP server did not initialize within ${STARTUP_TIMEOUT_MS / 1000}s`,
            ),
          ),
        STARTUP_TIMEOUT_MS,
      );
      spawnFailure.catch(() => {}).finally(() => clearTimeout(t));
    });
    await Promise.race([
      this.request("initialize", {
        protocolVersion: "2024-11-05",
        capabilities: {},
        clientInfo: { name: "pi-exp-rs-spatial", version: "1.0.0" },
      }),
      spawnFailure,
      startupDeadline,
    ]);
    this.notify("notifications/initialized", {});
  }

  private onStdout(chunk: string): void {
    this.buffer += chunk;
    if (this.buffer.length > MAX_LINE_BUFFER_CHARS) {
      // Fail fast instead of growing without bound on a runaway server.
      this.buffer = "";
      const err = new Error(
        `exp-rs MCP bridge: response line exceeded ${MAX_LINE_BUFFER_CHARS} chars`,
      );
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(err);
      }
      this.pending.clear();
      return;
    }
    let newline = this.buffer.indexOf("\n");
    while (newline >= 0) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (line) this.onLine(line);
      newline = this.buffer.indexOf("\n");
    }
  }

  private onLine(line: string): void {
    let msg: any;
    try {
      msg = JSON.parse(line);
    } catch {
      console.error("[exp-rs-spatial] dropping non-JSON line from server");
      return;
    }
    // A spec-conformant error response with id:null (e.g. a payload larger
    // than the server's 4 MiB line cap) answers ALL pending requests with a
    // parse fault - surface it instead of timing out for 10 minutes.
    if (msg.id === null && msg.error) {
      const err = new Error(msg.error.message ?? "MCP protocol error (id:null)");
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(err);
      }
      this.pending.clear();
      return;
    }
    if (msg.id === undefined || msg.id === null) return; // notification
    const pending = this.pending.get(msg.id);
    if (!pending) return;
    this.pending.delete(msg.id);
    clearTimeout(pending.timer);
    if (msg.error) {
      // Preserve the structured code/data (errorCode/errorCategory) so Pi
      // can classify retryable vs fatal instead of parsing prose.
      const err = new Error(msg.error.message ?? JSON.stringify(msg.error));
      (err as any).code = msg.error.code;
      (err as any).data = msg.error.data;
      pending.reject(err);
    } else {
      pending.resolve(msg.result);
    }
  }

  request(method: string, params: any, signal?: AbortSignal): Promise<any> {
    return new Promise((resolve, reject) => {
      if ((!this.child || this.exited) && this.stopped) {
        reject(new Error("exp-rs MCP bridge was stopped"));
        return;
      }
      if (!this.child || this.exited) {
        if (this.fastCrashCount >= 5) {
          reject(
            new Error(
              `exp-rs MCP server keeps crashing (${this.fastCrashCount} fast crashes); ` +
                `lazy respawn disabled. Last exit: ${this.lastExitError ?? "unknown"}`,
            ),
          );
          return;
        }
        // Lazy respawn after a crash: without this every tool failed
        // permanently until the Pi process restarted (#623).
        this.start()
          .then(() => this.request(method, params, signal).then(resolve, reject))
          .catch((err) =>
            reject(
              new Error(
                `exp-rs MCP server crashed and restart failed: ${err?.message ?? err}`,
              ),
            ),
          );
        return;
      }
      const id = this.nextId++;
      const cleanup = () => {
        if (signal) signal.removeEventListener("abort", onAbort);
      };
      const timer = setTimeout(() => {
        this.pending.delete(id);
        cleanup();
        reject(new Error(`MCP request timed out: ${method}`));
      }, REQUEST_TIMEOUT_MS);
      // Abort must also stop SERVER-side work: fire notifications/cancelled
      // for this rpc id so the server cancels the task behind the call -
      // without it an aborted long execution kept burning CPU (#623/#645).
      const onAbort = () => {
        this.pending.delete(id);
        clearTimeout(timer);
        this.notify("notifications/cancelled", { requestId: id });
        cleanup();
        reject(new Error("Aborted during exp-rs tool call"));
      };
      if (signal) {
        if (signal.aborted) {
          onAbort();
          return;
        }
        signal.addEventListener("abort", onAbort, { once: true });
      }
      // Every settle path must drop the abort listener: a long-lived signal
      // otherwise accumulated one onAbort closure per request, firing
      // spurious notifications/cancelled for already-completed rpc ids
      // (harmless server-side, but a leak) (#645).
      const wrappedResolve = (value: any) => {
        cleanup();
        resolve(value);
      };
      const wrappedReject = (err: Error) => {
        cleanup();
        reject(err);
      };
      this.pending.set(id, { resolve: wrappedResolve, reject: wrappedReject, timer });
      const payload = JSON.stringify({ jsonrpc: "2.0", id, method, params });
      this.child.stdin!.write(payload + "\n", (err) => {
        if (err) {
          this.pending.delete(id);
          clearTimeout(timer);
          cleanup();
          reject(new Error(`MCP write failed: ${err.message}`));
        }
      });
    });
  }

  private notify(method: string, params: any): void {
    if (!this.child || this.exited) return;
    this.child.stdin!.write(JSON.stringify({ jsonrpc: "2.0", method, params }) + "\n");
  }

  stop(): void {
    this.stopped = true;
    this.exited = true;
    const err = new Error("exp-rs MCP bridge stopped");
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(err);
    }
    this.pending.clear();
    const child = this.child;
    if (child) {
      // Grace ladder: SIGTERM lets the Qt app flush and tear down; escalate
      // to SIGKILL after a grace period so stop() cannot leak the child.
      // Destroying stdin first makes the server exit on EOF even when this
      // runs inside the process-exit hook, where timers (SIGKILL escalation)
      // never fire (#645).
      try {
        child.stdin?.destroy();
      } catch {
        // already gone
      }
      child.kill();
      const killer = setTimeout(() => child.kill("SIGKILL"), 5000);
      child.once("exit", () => clearTimeout(killer));
    }
    activeBridges.delete(this);
    this.child = null;
  }

  /** True when the child process is alive (for exprs_status liveness). */
  get alive(): boolean {
    return !!this.child && !this.exited;
  }
}

/** Runs a bridge request, cancellable via AbortSignal: on abort the local
 * promise rejects AND the server is told to cancel the work behind the rpc
 * id (notifications/cancelled) - previously the race was local-only and the
 * aborted execution kept running server-side (#645). */
async function requestOrAbort(
  bridge: McpBridge,
  method: string,
  params: any,
  signal?: AbortSignal,
): Promise<any> {
  return bridge.request(method, params, signal);
}

function truncateTail(text: string, max = MAX_RESULT_CHARS): string {
  if (text.length <= max) return text;
  const cut = text.length - max;
  return `[… ${cut} characters truncated, tail kept …]\n${text.slice(cut)}`;
}

/** MCP ids contain ":" which some providers reject; sanitize for Pi/OpenAI. */
function piToolName(mcpName: string): string {
  return "exprs_" + mcpName.replace(/[^a-zA-Z0-9_-]/g, "_");
}

function detectBinary(): string | null {
  const envBin = process.env.EXP_RS_MCP_BIN;
  if (envBin) return envBin;
  const candidates = [
    "build/sicnu_geo_rs",
    "build-dev/sicnu_geo_rs",
    "../build/sicnu_geo_rs",
    "./sicnu_geo_rs",
  ];
  for (const candidate of candidates) {
    try {
      if (existsSync(candidate) && statSync(candidate).isFile()) return candidate;
    } catch {
      // ignore
    }
  }
  return null;
}

/** Categories (tool prefixes) to bridge; meta = the protocol-level tools. */
function wantedCategories(): Set<string> {
  const raw = process.env.EXP_RS_TOOL_CATEGORIES ?? "meta,spatial,data,temporal";
  const set = new Set<string>();
  for (const part of raw.split(",")) {
    const trimmed = part.trim().toLowerCase();
    if (trimmed) set.add(trimmed);
  }
  return set;
}

function toolCategory(mcpName: string): string {
  if (!mcpName.includes(":")) return "meta";
  return mcpName.split(":", 1)[0];
}

export default async function (pi: ExtensionAPI) {
  const categories = wantedCategories();
  const extraArgs = (process.env.EXP_RS_MCP_ARGS ?? "").split(" ").filter(Boolean);
  const bin = detectBinary();
  const bridge = new McpBridge(bin ?? "sicnu_geo_rs", extraArgs);

  const guidelines = [
    "exp-rs spatial tools: call exprs_search_algorithms or exprs_list_algorithms to discover capabilities before planning a remote-sensing workflow.",
    "exprs_spatial_raster_inspect reveals CRS, resolution, band roles, and radiometric state — inspect inputs before choosing algorithms.",
    "exprs_preflight_algorithm validates parameters and estimates RAM without executing; prefer it over blind execution.",
    "exprs_execute_algorithm returns an execution_id immediately; poll exprs_get_execution_status or await it with exprs_wait_for_execution.",
    "Multi-step pipelines (preprocess → index → classify → vectorize) belong in exprs_run_workflow, not in chained single calls.",
  ];

  const registerWaitTool = () => {
    pi.registerTool({
      name: "exprs_wait_for_execution",
      label: "Wait for exp-rs execution",
      description:
        "Poll an exp-rs execution (execution_id from exprs_execute_algorithm or exprs_run_workflow steps) until it completes, fails, or is canceled. Returns the final status including the committed asset id when available.",
      parameters: Type.Object({
        execution_id: Type.String({ description: "Execution id (task-<id>) to wait for" }),
        timeout_seconds: Type.Optional(
          Type.Number({ description: "Give up after this many seconds (default 300)" }),
        ),
        poll_interval_ms: Type.Optional(
          Type.Number({ description: "Poll interval in milliseconds (default 1000)" }),
        ),
      }),
      async execute(_toolCallId, params, signal) {
        const deadline = Date.now() + (params.timeout_seconds ?? 300) * 1000;
        const interval = params.poll_interval_ms ?? 1000;
        // Abort must also stop SERVER-side work: fire-and-forget a cancel so
        // an aborted 30-minute classification does not keep burning CPU
        // while Pi believes it stopped (#623).
        const cancelServerSide = () => {
          bridge
            .request("tools/call", {
              name: "cancel_execution",
              arguments: { execution_id: params.execution_id },
            })
            .catch(() => {});
        };
        signal?.addEventListener("abort", cancelServerSide, { once: true });
        try {
          for (;;) {
            if (signal?.aborted) throw new Error("Aborted while waiting for execution");
            const result = await bridge.request("tools/call", {
              name: "get_execution_status",
              arguments: { execution_id: params.execution_id },
            });
            const text: string = result?.content?.[0]?.text ?? "{}";
            let status = "unknown";
            try {
              status = JSON.parse(text).status ?? status;
            } catch {
              // keep polling on malformed payloads
            }
            if (status === "completed" || status === "failed" || status === "canceled") {
              return { content: [{ type: "text" as const, text: truncateTail(text) }], details: {} };
            }
            if (Date.now() + interval > deadline) {
              // A timeout is a failure the model can distinguish from
              // completion, not a green result.
              throw new Error(
                `Timed out waiting for execution '${params.execution_id}' (still '${status}')`,
              );
            }
            await new Promise((r) => setTimeout(r, interval));
          }
        } finally {
          signal?.removeEventListener("abort", cancelServerSide);
        }
      },
    });
  };

  const registerBridgedTools = async (): Promise<number> => {
    const list = await bridge.request("tools/list", {});
    let count = 0;
    for (const tool of list?.tools ?? []) {
      const category = toolCategory(tool.name);
      if (!categories.has(category)) continue;
      const schema =
        tool.inputSchema && tool.inputSchema.type === "object"
          ? tool.inputSchema
          : { type: "object", properties: {} };
      pi.registerTool({
        name: piToolName(tool.name),
        label: `exp-rs ${tool.name}`,
        description: `[exp-rs ${tool.name}] ${tool.description ?? ""}`.slice(0, 8_000),
        parameters: schema as any,
        promptGuidelines: count === 0 ? guidelines : undefined,
        async execute(_toolCallId, params, signal, onUpdate) {
          onUpdate?.({ content: [{ type: "text", text: `exp-rs ${tool.name} …` }] });
          if (signal?.aborted) throw new Error("Aborted before dispatch");
          const result = await requestOrAbort(
            bridge,
            "tools/call",
            { name: tool.name, arguments: params },
            signal,
          );
          const text: string =
            result?.content?.map((c: any) => c.text ?? "").join("\n") ?? "(no content)";
          if (result?.isError) {
            const err = new Error(truncateTail(text));
            // Preserve the structured taxonomy (INVALID_PARAMETER,
            // PATH_OUTSIDE_WORKSPACE, ...) so Pi can classify instead of
            // parsing prose - protocol errors already carried code/data;
            // tool execution errors dropped them (#645).
            (err as any).errorCode = result?.errorCode;
            (err as any).errorCategory = result?.errorCategory;
            throw err;
          }
          return { content: [{ type: "text" as const, text: truncateTail(text) }], details: {} };
        },
      });
      count++;
    }
    return count;
  };

  const registerStatusTool = (message: string) => {
    pi.registerTool({
      name: "exprs_status",
      label: "exp-rs bridge status",
      description:
        "Reports whether the exp-rs spatial bridge is connected (live liveness, not the startup snapshot) and how to fix it (EXP_RS_MCP_BIN, build the project first).",
      parameters: Type.Object({}),
      async execute() {
        const live = bridge.alive
          ? "bridge: CONNECTED (child process alive)"
          : `bridge: DISCONNECTED${bridge.error ? ` (${bridge.error})` : ""}`;
        return {
          content: [{ type: "text" as const, text: `${live}\n${message}` }],
          details: {},
        };
      },
    });
  };

  if (!bin) {
    registerStatusTool(
      "exp-rs bridge: no binary found. Build the project (build/sicnu_geo_rs) or set EXP_RS_MCP_BIN to the desktop binary; it is launched with --mcp.",
    );
    return;
  }
  try {
    await bridge.start();
    const count = await registerBridgedTools();
    registerWaitTool();
    console.log(`[exp-rs-spatial] bridged ${count} tools from ${bin}`);
  } catch (err: any) {
    // start() may have spawned a healthy child that then failed the tool
    // registration - tear it down so nothing leaks (#623).
    bridge.stop();
    registerStatusTool(
      `exp-rs bridge failed to start (${bin}): ${err?.message ?? err}. Set EXP_RS_MCP_BIN or rebuild the project.`,
    );
  }
}
