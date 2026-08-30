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
export const MAX_LINE_BUFFER_CHARS = 32 * 1024 * 1024;

export type Pending = {
  resolve: (value: any) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
};

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
  // run. Best-effort: SIGTERM + destroy stdin - the server exits on stdin
  // EOF, and the OS reaps the child when the parent dies.
  process.on("exit", () => {
    for (const bridge of activeBridges) bridge.stop();
  });
}

export class McpBridge {
  private child: ChildProcess | null = null;
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private buffer = "";
  private startError: string | null = null;
  private exited = false;
  private stopped = false;
  private stderrTail = "";
  /** In-flight start, so concurrent requests after a crash share ONE
   * respawn instead of each forking its own child (#669). */
  private starting: Promise<void> | null = null;

  readonly bin: string;
  readonly extraArgs: string[];

  constructor(bin: string, extraArgs: string[]) {
    this.bin = bin;
    this.extraArgs = extraArgs;
    installExitHook();
    activeBridges.add(this);
  }

  get error(): string | null {
    return this.startError;
  }

  /** Idempotent lifecycle entry: a healthy child short-circuits, concurrent
   * callers share one in-flight start, and after a crash exactly one
   * respawn happens per start batch (#669). */
  start(): Promise<void> {
    if (this.stopped) {
      return Promise.reject(new Error("exp-rs MCP bridge was stopped"));
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
        // Lazy respawn after a crash: without this every tool failed
        // permanently until the Pi process restarted (#623). start() is
        // idempotent, so concurrent callers share one respawn (#669).
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
