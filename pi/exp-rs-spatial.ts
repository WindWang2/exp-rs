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
 *                         (default: "meta,spatial,data"; e.g. add "rs,gdal,otb")
 *   SICNU_MCP_WORKSPACE   passed through to restrict server file access
 *   SICNU_MODELS_DIR      passed through to locate model manifests
 */
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { spawn, type ChildProcess } from "node:child_process";
import { existsSync } from "node:fs";

/** Max characters of a tool result kept (tail preserved). */
const MAX_RESULT_CHARS = 50_000;
const REQUEST_TIMEOUT_MS = 10 * 60_1_000;

type Pending = {
  resolve: (value: any) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
};

/** Minimal newline-delimited JSON-RPC client for the exp-rs MCP server. */
class McpBridge {
  private child: ChildProcess | null = null;
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private buffer = "";
  private startError: string | null = null;
  private exited = false;

  constructor(private readonly bin: string, private readonly extraArgs: string[]) {}

  get error(): string | null {
    return this.startError;
  }

  async start(): Promise<void> {
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
    this.child.stderr!.setEncoding("utf8");
    this.child.stderr!.on("data", () => {});
    this.child.on("exit", (code) => {
      this.exited = true;
      const err = new Error(`exp-rs MCP server exited (code ${code})`);
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.reject(err);
      }
      this.pending.clear();
    });
    this.child.stdin!.on("error", () => {});
    process.on("exit", () => this.child?.kill());

    await Promise.race([
      this.request("initialize", {
        protocolVersion: "2024-11-05",
        capabilities: {},
        clientInfo: { name: "pi-exp-rs-spatial", version: "1.0.0" },
      }),
      spawnFailure,
    ]);
    this.notify("notifications/initialized", {});
  }

  private onStdout(chunk: string): void {
    this.buffer += chunk;
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
      return;
    }
    if (msg.id === undefined || msg.id === null) return; // notification
    const pending = this.pending.get(msg.id);
    if (!pending) return;
    this.pending.delete(msg.id);
    clearTimeout(pending.timer);
    if (msg.error) {
      pending.reject(new Error(msg.error.message ?? JSON.stringify(msg.error)));
    } else {
      pending.resolve(msg.result);
    }
  }

  request(method: string, params: any): Promise<any> {
    return new Promise((resolve, reject) => {
      if (!this.child || this.exited) {
        reject(new Error("exp-rs MCP server is not running"));
        return;
      }
      const id = this.nextId++;
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`MCP request timed out: ${method}`));
      }, REQUEST_TIMEOUT_MS);
      this.pending.set(id, { resolve, reject, timer });
      const payload = JSON.stringify({ jsonrpc: "2.0", id, method, params });
      this.child.stdin!.write(payload + "\n", (err) => {
        if (err) {
          this.pending.delete(id);
          clearTimeout(timer);
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
    this.exited = true;
    const err = new Error("exp-rs MCP bridge stopped");
    for (const p of this.pending.values()) {
      clearTimeout(p.timer);
      p.reject(err);
    }
    this.pending.clear();
    this.child?.kill();
    this.child = null;
  }
}

/** Races a bridge request against an AbortSignal so long calls stay cancellable. */
async function requestOrAbort(
  bridge: McpBridge,
  method: string,
  params: any,
  signal?: AbortSignal,
): Promise<any> {
  if (!signal) return bridge.request(method, params);
  const aborted = new Promise<never>((_, reject) => {
    const onAbort = () => reject(new Error("Aborted during exp-rs tool call"));
    if (signal.aborted) onAbort();
    else signal.addEventListener("abort", onAbort, { once: true });
  });
  return Promise.race([bridge.request(method, params), aborted]);
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
      if (existsSync(candidate)) return candidate;
    } catch {
      // ignore
    }
  }
  return null;
}

/** Categories (tool prefixes) to bridge; meta = the protocol-level tools. */
function wantedCategories(): Set<string> {
  const raw = process.env.EXP_RS_TOOL_CATEGORIES ?? "meta,spatial,data";
  const set = new Set<string>();
  for (const part of raw.split(",")) {
    const trimmed = part.trim();
    if (trimmed) set.add(trimmed.toLowerCase() === "meta" ? "meta" : trimmed);
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
        const deadline = Date.now() + (params.timeout_seconds ?? 300) * 1_000;
        const interval = params.poll_interval_ms ?? 1_000;
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
            return {
              content: [
                {
                  type: "text" as const,
                  text: truncateTail(`Timed out still '${status}'. Last response:\n${text}`),
                },
              ],
              details: {},
            };
          }
          await new Promise((r) => setTimeout(r, interval));
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
          if (result?.isError) throw new Error(truncateTail(text));
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
        "Reports whether the exp-rs spatial bridge is connected and how to fix it (EXP_RS_MCP_BIN, build the project first).",
      parameters: Type.Object({}),
      async execute() {
        return { content: [{ type: "text" as const, text: message }], details: {} };
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
    registerStatusTool(
      `exp-rs bridge failed to start (${bin}): ${err?.message ?? err}. Set EXP_RS_MCP_BIN or rebuild the project.`,
    );
  }
}
