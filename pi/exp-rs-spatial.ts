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
import { existsSync, statSync } from "node:fs";

import {
  MAX_RESULT_CHARS,
  McpBridge,
} from "./mcp_bridge.ts";

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
  const raw = process.env.EXP_RS_TOOL_CATEGORIES ?? "meta,spatial,data";
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
