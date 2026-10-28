/**
 * Minimal newline-delimited JSON-RPC client for the exp-rs MCP server,
 * extracted from exp-rs-spatial.ts so the lifecycle can be unit-tested
 * without the Pi extension host (#669/#706).
 *
 * Uses only erasable TypeScript syntax (no parameter properties, enums, or
 * namespaces) so Node's native type-stripping can import it directly.
 */
import { spawn, type ChildProcess } from "node:child_process";

/** Max characters of a tool result kept (tail preserved). */
export const MAX_RESULT_CHARS = 50_000;
export const REQUEST_TIMEOUT_MS = 10 * 60 * 1000;
export const STARTUP_TIMEOUT_MS = 30 * 1000;
/** Inbound response-line cap in UTF-8 BYTES (desync detector — the server's
 * own line cap is 4 MiB, so a runaway buffer means stdio framing broke).
 * Byte-based like the outbound guard: JS string .length counts UTF-16 code
 * units and under-reports a CJK flood by up to 3x. */
export const MAX_LINE_BUFFER_BYTES = 32 * 1024 * 1024;
/** The server's stdin line cap (McpServer kMaxMcpLine, 4 MiB). A request
 * payload larger than this is answered by a -32700 id:null error — which the
 * server cannot attribute to any single request, so the bridge-side handling
 * rejects ALL pending requests. Refusing the oversized request BEFORE it is
 * written keeps the failure to that one call (R14-C FIX-4). */
export const MAX_OUTBOUND_LINE_BYTES = 4 * 1024 * 1024;

/** Typed bridge-failure codes (R25-C). Every error the BRIDGE itself creates
 * carries one (err.bridgeError + err.retryable) so Pi classifies
 * retryability without parsing prose — the same rationale as the tool-level
 * errorCode/errorCategory passthrough (#645). Server-originated JSON-RPC
 * errors are NOT retyped: they already carry their own code/data. */
export const BRIDGE_ERRORS = {
  /** Child died mid-conversation; the lazy respawn makes a retry sane. */
  CHILD_EXITED: "CHILD_EXITED",
  /** A crash-triggered (or failed-start) respawn failed; the next request
   * retries the start automatically, so retrying is allowed. */
  RESTART_FAILED: "RESTART_FAILED",
  /** Spawn-class failure (ENOENT/EACCES/EFTYPE) — the binary itself. */
  LAUNCH_FAILED: "LAUNCH_FAILED",
  /** Per-request deadline fired; notifications/cancelled was already sent
   * so server-side work stopped too (R18-C C05). */
  REQUEST_TIMEOUT: "REQUEST_TIMEOUT",
  /** >32 MiB inbound line: stdio framing desynced; the child is killed and
   * the next request lazy-respawns (F-PI-1). */
  STREAM_DESYNC: "STREAM_DESYNC",
  /** Outbound payload exceeds the server's 4 MiB line cap. NOT retryable
   * as-is: the caller must split the argument or use a file path. */
  PAYLOAD_TOO_LARGE: "PAYLOAD_TOO_LARGE",
  /** Explicit stop(); terminal — the bridge is never resurrected. */
  BRIDGE_STOPPED: "BRIDGE_STOPPED",
  /** Caller aborted via AbortSignal; server-side cancel already fired. */
  ABORTED: "ABORTED",
} as const;
export type BridgeErrorCode = (typeof BRIDGE_ERRORS)[keyof typeof BRIDGE_ERRORS];

/** One bridge-thrown error with its typed classification attached. */
export function bridgeFailure(
  message: string,
  code: BridgeErrorCode,
  retryable: boolean,
): Error {
  const err = new Error(message);
  (err as any).bridgeError = code;
  (err as any).retryable = retryable;
  return err;
}

/** Identity + drift report for one completed handshake (R25-C). The shell
 * uses previousServerInfo to detect a respawn onto a DIFFERENT binary
 * version and re-list the tool catalog (stale registerTool sets are
 * otherwise undetectable — the bridge respawns transparently). */
export interface GenerationInfo
{
  generation: number;
  serverInfo: any;
  previousServerInfo: any;
}

export type Pending = {
  resolve: (value: any) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
};

/** MCP ids contain ":" which some providers reject; sanitize for Pi/OpenAI.
 * Lives beside the bridge (not in the extension shell) so the mapping is
 * importable — and therefore contract-testable — from node --test
 * (pi/test/surface_snapshot.test.mjs pins it against the surface snapshot). */
export function piToolName(mcpName: string): string {
  return "exprs_" + mcpName.replace(/[^a-zA-Z0-9_-]/g, "_");
}

/** The family (category) of an MCP tool id: the text before the first ':';
 * unprefixed ids are protocol-level "meta" tools. Consumed by the shell's
 * registration filter and pinned by pi/test/surface_snapshot.test.mjs
 * against the shared surface snapshot (single source, same reason as
 * piToolName). */
export function toolCategory(mcpName: string): string {
  if (!mcpName.includes(":")) return "meta";
  return mcpName.split(":", 1)[0];
}

/** Poll-interval clamp for the wait tool (R21-C F4): the model controls
 * poll_interval_ms, and 0/negative/non-finite values used to hit setTimeout
 * directly — an event-loop-speed poll storm for the whole timeout window,
 * where even the deadline check `Date.now() + interval > deadline` can
 * never fire early with interval <= 0. Floor 100 ms, ceiling 60 s, default
 * 1 s when absent or unusable. Exported beside piToolName so the shell
 * imports it and node --test pins it (single source, same reason). */
export const POLL_INTERVAL_FLOOR_MS = 100;
export const POLL_INTERVAL_CEILING_MS = 60_000;
export function clampPollIntervalMs(
  value: number | undefined,
  fallback = 1_000,
): number {
  if (typeof value !== "number" || !Number.isFinite(value)) return fallback;
  return Math.min(
    POLL_INTERVAL_CEILING_MS,
    Math.max(POLL_INTERVAL_FLOOR_MS, Math.floor(value)),
  );
}

/** Tail-keeping truncation for tool results: the caller sees HOW MUCH was
 * dropped (never a silent cut), and the tail — the part agents act on —
 * survives. Track 9: moved here for the same single-source reason as
 * piToolName; the shell imports it. */
export function truncateTail(text: string, max = MAX_RESULT_CHARS): string {
  if (text.length <= max) return text;
  const cut = text.length - max;
  return `[… ${cut} characters truncated, tail kept …]\n${text.slice(cut)}`;
}

/** Live bridges, so the (single) process-exit hook tears all of them down.
 * Extension reload used to leak the previous child for the whole session
 * because start() re-registered `process.on("exit")` per spawn and Pi never
 * invoked an unload hook (#645). */
export const activeBridges = new Set<McpBridge>();
let exitHookInstalled = false;
export function installExitHook(): void {
  if (exitHookInstalled) return;
  exitHookInstalled = true;
  // During exit, timers never fire, so stop()'s SIGKILL escalation cannot
  // run. Best-effort: SIGTERM + destroy stdin — the server exits on stdin
  // EOF (an orphaned child would otherwise linger, not be reaped).
  process.on("exit", () => {
    for (const bridge of activeBridges) bridge.stop();
  });
}

export class McpBridge {
  private child: ChildProcess | null = null;
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private buffer = "";
  private bufferBytes = 0;
  private startError: string | null = null;
  private exited = false;
  private stopped = false;
  private stderrTail = "";
  /** In-flight start, so concurrent requests after a crash share ONE
   * respawn instead of each forking its own child (#669). */
  private starting: Promise<void> | null = null;
  /** Circuit-breaker accounting (absorbed from the exp-rs-spatial.ts copy):
   * a child dying within 10s of spawn is a fast crash; 5 in a row disables
   * lazy respawn instead of looping. */
  private lastSpawnAt = 0;
  private fastCrashCount = 0;
  private lastExitError: string | null = null;
  /** Handshake identity of the CURRENT generation (R25-C): captured from
   * the initialize response so a respawn onto a different binary version is
   * observable instead of silently absorbed. Null before the first
   * successful handshake. */
  private serverInfoValue: any = null;
  private protocolVersionValue: string | null = null;
  /** Number of completed handshakes (successful generations), R25-C. */
  private generationsValue = 0;
  /** Fired after every completed handshake (including the first). */
  private onGeneration?: (info: GenerationInfo) => void;

  readonly bin: string;
  readonly extraArgs: string[];
  readonly startupTimeoutMs: number;
  readonly requestTimeoutMs: number;

  constructor(
    bin: string,
    extraArgs: string[],
    options: {
      startupTimeoutMs?: number;
      requestTimeoutMs?: number;
      onGeneration?: (info: GenerationInfo) => void;
    } = {},
  ) {
    this.bin = bin;
    this.extraArgs = extraArgs;
    this.startupTimeoutMs = options.startupTimeoutMs ?? STARTUP_TIMEOUT_MS;
    this.requestTimeoutMs = options.requestTimeoutMs ?? REQUEST_TIMEOUT_MS;
    this.onGeneration = options.onGeneration;
    installExitHook();
    activeBridges.add(this);
  }

  get error(): string | null {
    return this.startError;
  }

  /** The CURRENT generation's serverInfo ({name, version}) from the last
   * completed initialize handshake; null before the first (R25-C). */
  get serverInfo(): any {
    return this.serverInfoValue;
  }

  /** The CURRENT generation's protocolVersion string; null before the first
   * completed handshake (R25-C). */
  get protocolVersion(): string | null {
    return this.protocolVersionValue;
  }

  /** Count of completed handshakes — increments on every successful
   * (re)spawn, so a value >1 means the child crashed and was replaced
   * (R25-C drift observability). */
  get generations(): number {
    return this.generationsValue;
  }

  /** Idempotent lifecycle entry: a healthy child short-circuits, concurrent
   * callers share one in-flight start, and after a crash exactly one
   * respawn happens per start batch (#669). */
  start(): Promise<void> {
    if (this.stopped) {
      return Promise.reject(
        bridgeFailure("exp-rs MCP bridge was stopped", BRIDGE_ERRORS.BRIDGE_STOPPED, false),
      );
    }
    if (this.child && !this.exited) return Promise.resolve();
    if (!this.starting) {
      this.starting = this.spawnAndInitialize().finally(() => {
        this.starting = null;
      });
    }
    return this.starting;
  }

  private async spawnAndInitialize(): Promise<void> {
    // Startup-race guard (absorbed from the exp-rs-spatial.ts copy): if a
    // previous spawn's child is somehow still alive (e.g. an initialize
    // timeout left it hanging with exited=false), kill it before spawning a
    // replacement — otherwise it blocks every respawn while its stdio goes
    // nowhere. terminateChild, not a bare kill(): a hung child ignoring
    // SIGTERM would keep its pipe handles open forever.
    if (this.child && !this.exited) {
      this.terminateChild(this.child);
    }
    this.lastSpawnAt = Date.now();
    // Clear the crash latch BEFORE any await: the initialize request below
    // flows through request(), which re-reads this.exited to decide whether
    // it needs another start(). Leaving the latch set across that await is
    // what made every post-crash request re-enter start() and fork children
    // in an unbounded microtask loop (#669).
    this.exited = false;
    this.startError = null;
    const childEnv: Record<string, string> = {};
    for (const [k, v] of Object.entries(process.env)) {
      if (v !== undefined) childEnv[k] = v;
    }
    // The desktop binary resolves its own Qt/QGIS stack; inherited launcher
    // library paths (AppImage mounts, snap/flatpak) break its plugins.
    delete childEnv.LD_LIBRARY_PATH;
    childEnv.QT_QPA_PLATFORM = "offscreen";

    // A desync kill leaves unread bytes in the OLD child's pipe; they must
    // never glue onto the new generation's frames (F-PI-1 replay bug):
    // reset the framing buffer and drop stdout chunks from any generation
    // that is no longer current.
    this.buffer = "";
    this.bufferBytes = 0;
    // Same replay reason as buffer: a new generation must not report the
    // PREVIOUS child's crash output in its own exit error.
    this.stderrTail = "";
    const child = (this.child = spawn(this.bin, ["--mcp", ...this.extraArgs], {
      stdio: ["pipe", "pipe", "pipe"],
      env: childEnv,
    }));
    this.child.stdout!.setEncoding("utf8");
    this.child.stdout!.on("data", (chunk) => {
      if (this.child !== child) return; // stale generation
      this.onStdout(chunk);
    });
    // spawn() reports late ENOENT/EACCES via an "error" event, not a throw.
    const spawnFailure = new Promise<never>((_, reject) => {
      child.once("error", (err) => {
        this.startError = `Failed to launch ${this.bin}: ${err?.message ?? err}`;
        reject(bridgeFailure(this.startError, BRIDGE_ERRORS.LAUNCH_FAILED, false));
      });
    });

    // Keep a stderr tail so a child crash is diagnosable from exprs_status
    // and the exit error (Qt/QGIS plugin failures precede most crashes).
    child.stderr!.setEncoding("utf8");
    child.stderr!.on("data", (chunk: string) => {
      if (this.child !== child) return;
      this.stderrTail = (this.stderrTail + chunk).slice(-4000);
    });
    // Persistent handler: once("error") left later child errors unhandled
    // (EventEmitter throws on an "error" event with no listener).
    child.on("error", (err) => {
      if (this.child !== child) return; // stale event from a previous child
      // The spawn-failure listener above records the more specific
      // "Failed to launch <bin>: ..." first; don't clobber it with the
      // generic wording — it is what exprs_status surfaces.
      this.startError ??= `exp-rs MCP server error: ${err?.message ?? err}`;
    });
    const spawned = this.child;
    this.child.on("exit", (code) => {
      // Only the CURRENT generation's exit may flip bridge state: a stale
      // child killed by a later spawn must not reject the new child's
      // in-flight calls (review R-B5).
      if (this.child !== spawned) return;
      this.exited = true;
      const tail = this.stderrTail.trim();
      const err = bridgeFailure(
        `exp-rs MCP server exited (code ${code})${tail ? `; stderr tail:\n${tail}` : ""}`,
        BRIDGE_ERRORS.CHILD_EXITED,
        true,
      );
      this.lastExitError = err.message;
      // Circuit-breaker accounting: a child dying within 10s of spawn is a
      // fast crash; 5 in a row disables lazy respawn instead of looping.
      if (Date.now() - this.lastSpawnAt < 10_000) this.fastCrashCount++;
      else this.fastCrashCount = 0;
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(err);
      }
      this.pending.clear();
    });
    child.stdin!.on("error", () => {});
    // Process-exit teardown is installed once at module level (see
    // installExitHook) - registering it here stacked a listener per
    // (re)spawn and MaxListeners-warned after ~10 respawns (#645).

    // Startup deadline: a child that spawns but hangs during Qt/QGIS init
    // must not stall extension load for the full 10-minute request timeout.
    let cancelStartupDeadline: () => void = () => {};
    const startupDeadline = new Promise<never>((_, reject) => {
      const t = setTimeout(
        () =>
          reject(
            new Error(
              `exp-rs MCP server did not initialize within ${this.startupTimeoutMs / 1000}s`,
            ),
          ),
        this.startupTimeoutMs,
      );
      cancelStartupDeadline = () => clearTimeout(t);
      spawnFailure.catch(() => {}).finally(() => clearTimeout(t));
    });
    let generationInfo: GenerationInfo | null = null;
    try {
      const initResult = await Promise.race([
        this.request("initialize", {
          protocolVersion: "2024-11-05",
          capabilities: {},
          clientInfo: { name: "pi-exp-rs-spatial", version: "1.0.0" },
        }),
        spawnFailure,
        startupDeadline,
      ]);
      // R25-C: capture the handshake identity so generation drift (a
      // respawn onto a rebuilt/older binary) is observable — the response
      // used to be discarded, making every respawn indistinguishable.
      this.protocolVersionValue = initResult?.protocolVersion ?? null;
      const previousServerInfo = this.serverInfoValue;
      this.serverInfoValue = initResult?.serverInfo ?? null;
      this.generationsValue++;
      generationInfo = {
        generation: this.generationsValue,
        serverInfo: this.serverInfoValue,
        previousServerInfo,
      };
    } catch (err) {
      this.discardFailedStart(child, err);
      throw err;
    } finally {
      // Clear on EVERY settle path: a healthy initialize used to leave the
      // 30s timer armed, pinning the event loop after every spawn/respawn.
      cancelStartupDeadline();
    }
    // A child that completed the handshake is healthy: re-arm the fast-crash
    // circuit from zero. Without this, five crashes that each happened <10s
    // after their spawn — spread arbitrarily far apart by healthy uptime —
    // accumulated to a permanent respawn disable for the whole session
    // (R14-C FIX-4).
    this.fastCrashCount = 0;
    this.notify("notifications/initialized", {});
    // R25-C: announce the completed generation AFTER the child is fully
    // initialized, so a shell callback may immediately re-list the tool
    // catalog. Wrapped: a throwing callback must not break the start path
    // the rest of the session depends on.
    if (generationInfo) {
      try {
        this.onGeneration?.(generationInfo);
      } catch (err) {
        console.error(
          "[exp-rs-spatial] onGeneration callback failed:",
          err instanceof Error ? err.message : err,
        );
      }
    }
  }

  /** A generation whose handshake failed (spawn ENOENT/EACCES, initialize
   * timeout) is DEAD, not "healthy": Node emits no "exit" for a child that
   * never spawned, and a hung child stays alive. Leaving it as `this.child`
   * with exited=false made start() short-circuit as "healthy", so every
   * later request wrote into a destroyed/uninitialized stream with no
   * respawn and exprs_status reported CONNECTED. Tear the generation down,
   * book it as a fast crash for the circuit breaker, and let the next
   * request respawn. */
  private discardFailedStart(child: ChildProcess, err: unknown): void {
    if (this.child !== child) return; // superseded by a newer generation
    const message = err instanceof Error ? err.message : String(err);
    this.child = null;
    this.exited = true;
    this.startError = this.startError ?? message;
    this.lastExitError = `exp-rs MCP server failed to start: ${message}`;
    this.fastCrashCount++;
    // RESTART_FAILED, retryable: the next request automatically retries the
    // start, so a caller-side retry is consistent with bridge behavior.
    const failure = bridgeFailure(
      this.lastExitError,
      BRIDGE_ERRORS.RESTART_FAILED,
      true,
    );
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(failure);
    }
    this.pending.clear();
    this.terminateChild(child);
  }

  /** Shared teardown ladder: destroy stdin (the server exits on EOF even
   * inside the process-exit hook, where timers never run), SIGTERM for a
   * graceful Qt flush, then an armed SIGKILL so a stalled child cannot
   * keep `exited` false forever. The killer is unref'd so an about-to-die
   * bridge never pins the event loop for the 5s grace period. */
  private terminateChild(child: ChildProcess): void {
    try {
      child.stdin?.destroy();
    } catch {
      // already gone
    }
    child.kill();
    const killer = setTimeout(() => child.kill("SIGKILL"), 5000);
    killer.unref();
    child.once("exit", () => clearTimeout(killer));
  }

  private onStdout(chunk: string): void {
    this.buffer += chunk;
    this.bufferBytes += Buffer.byteLength(chunk, "utf8");
    if (this.bufferBytes > MAX_LINE_BUFFER_BYTES) {
      // A >32 MiB "line" means stdio framing is desynced (the server's own
      // line cap is 4 MiB). Without a terminating newline every later
      // response glues onto this one: each tool call would burn the full
      // 10-minute timeout with no diagnosis. Reject pending AND kill the
      // child so the next request takes the lazy-respawn path instead of
      // talking into a zombie stream (whole-repo review F-PI-1).
      this.failDesyncedStream(
        bridgeFailure(
          `exp-rs MCP bridge: response line exceeded ${MAX_LINE_BUFFER_BYTES} bytes; ` +
            `stdio framing desynced - respawning`,
          BRIDGE_ERRORS.STREAM_DESYNC,
          true,
        ),
      );
      return;
    }
    let newline = this.buffer.indexOf("\n");
    while (newline >= 0) {
      const line = this.buffer.slice(0, newline).trim();
      this.buffer = this.buffer.slice(newline + 1);
      if (line) this.onLine(line);
      newline = this.buffer.indexOf("\n");
    }
    // The post-consumption tail is usually tiny, so recounting it is cheap
    // and immune to surrogate-pair drift across chunk boundaries.
    this.bufferBytes = Buffer.byteLength(this.buffer, "utf8");
  }

  /** Shared desync teardown: reject everything, then tear the child down so
   * `exited` flips and lazy respawn replaces it (F-PI-1). */
  private failDesyncedStream(err: Error): void {
    this.buffer = "";
    this.bufferBytes = 0;
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(err);
    }
    this.pending.clear();
    const child = this.child;
    if (child && !this.exited) {
      // A child stalling in SIGTERM teardown would otherwise keep `exited`
      // false forever and the lazy respawn would never fire (review R-B4).
      this.terminateChild(child);
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
      if (this.stopped) {
        reject(
          bridgeFailure("exp-rs MCP bridge was stopped", BRIDGE_ERRORS.BRIDGE_STOPPED, false),
        );
        return;
      }
      if (!this.child || this.exited) {
        if (this.fastCrashCount >= 5) {
          reject(
            bridgeFailure(
              `exp-rs MCP server keeps crashing (${this.fastCrashCount} fast crashes); ` +
                `lazy respawn disabled. Last exit: ${this.lastExitError ?? "unknown"}`,
              // CHILD_EXITED but NOT retryable: the circuit breaker disabled
              // lazy respawn, so a retry cannot succeed until a manual start.
              BRIDGE_ERRORS.CHILD_EXITED,
              false,
            ),
          );
          return;
        }
        // Lazy respawn after a crash: without this every tool failed
        // permanently until the Pi process restarted (#623). start() is
        // idempotent, so concurrent callers share one respawn (#669).
        this.start()
          .then(() => this.request(method, params, signal).then(resolve, reject))
          .catch((err) =>
            reject(
              bridgeFailure(
                `exp-rs MCP server crashed and restart failed: ${err?.message ?? err}`,
                // A start() failure here already burned one respawn attempt;
                // the next request retries automatically, so allow a retry.
                BRIDGE_ERRORS.RESTART_FAILED,
                true,
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
        // A timeout must stop SERVER-side work too — the same reason the
        // abort path sends notifications/cancelled (#623/#645): without it a
        // timed-out tools/call (e.g. a stuck classification) keeps burning
        // CPU after the client has already given up on it (R18-C C05).
        this.notify("notifications/cancelled", { requestId: id });
        cleanup();
        reject(
          bridgeFailure(
            `MCP request timed out: ${method}`,
            BRIDGE_ERRORS.REQUEST_TIMEOUT,
            true,
          ),
        );
      }, this.requestTimeoutMs);
      // Abort must also stop SERVER-side work: fire notifications/cancelled
      // for this rpc id so the server cancels the task behind the call -
      // without it an aborted long execution kept burning CPU (#623/#645).
      const onAbort = () => {
        this.pending.delete(id);
        clearTimeout(timer);
        this.notify("notifications/cancelled", { requestId: id });
        cleanup();
        reject(bridgeFailure("Aborted during exp-rs tool call", BRIDGE_ERRORS.ABORTED, true));
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
      // Outbound size guard: the server caps stdin lines at 4 MiB of UTF-8
      // BYTES and answers anything larger with an id:null parse fault that
      // cannot be matched to this request — the bridge-side handling then
      // rejects EVERY pending request. Refuse here so only the oversized
      // call fails (e.g. a tools/call embedding a giant GeoJSON argument
      // must not kill ten concurrent exprs_wait_for_execution polls). The
      // check is byte-based: string .length counts UTF-16 code units, which
      // under-reports multibyte (CJK) payloads by up to 3x (review R1-P2).
      const payloadBytes = Buffer.byteLength(payload, "utf8");
      if (payloadBytes > MAX_OUTBOUND_LINE_BYTES) {
        this.pending.delete(id);
        clearTimeout(timer);
        cleanup();
        reject(
          bridgeFailure(
            `MCP request payload exceeds the server's ${MAX_OUTBOUND_LINE_BYTES}-byte line cap ` +
              `(${payloadBytes} bytes, method ${method}); split the argument or use a file path`,
            BRIDGE_ERRORS.PAYLOAD_TOO_LARGE,
            false,
          ),
        );
        return;
      }
      this.child.stdin!.write(payload + "\n", (err) => {
        if (err) {
          this.pending.delete(id);
          clearTimeout(timer);
          cleanup();
          // Write failures are virtually always EPIPE-class: the child's
          // stdin closed because it is dying. Typed CHILD_EXITED/retryable —
          // the exit event or the next request's lazy respawn will confirm.
          reject(
            bridgeFailure(
              `MCP write failed: ${err.message}`,
              BRIDGE_ERRORS.CHILD_EXITED,
              true,
            ),
          );
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
    const err = bridgeFailure("exp-rs MCP bridge stopped", BRIDGE_ERRORS.BRIDGE_STOPPED, false);
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(err);
    }
    this.pending.clear();
    const child = this.child;
    if (child) {
      // Destroying stdin first (inside terminateChild) makes the server
      // exit on EOF even when this runs inside the process-exit hook,
      // where timers (SIGKILL escalation) never fire (#645).
      this.terminateChild(child);
    }
    activeBridges.delete(this);
    this.child = null;
  }

  /** True when the child process is alive (for exprs_status liveness). */
  get alive(): boolean {
    return !!this.child && !this.exited;
  }
}
