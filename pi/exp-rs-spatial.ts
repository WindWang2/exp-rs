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
 *                         (default: "meta,spatial,data,temporal,cartography,symbology,workflow,workspace,layout,harness,mission"; e.g. add "rs,gdal,otb")
 *   SICNU_MCP_WORKSPACE   passed through; sandbox root for server file access.
 *                         Unset = the directory Pi runs in (the server
 *                         inherits Pi's CWD and sandboxes to it).
 *   SICNU_MODELS_DIR      passed through to locate model manifests
 */
import type { ExtensionAPI } from "@earendil-works/pi-coding-agent";
import { Type } from "typebox";
import { existsSync, statSync } from "node:fs";

// Single bridge implementation (Compiler 10.0 drift guard, review F-PI-2):
// the transport lives ONLY in ./mcp_bridge.ts — this extension shell keeps
// the Pi registration surface. A second McpBridge copy here is what let the
// two files' lifecycle fixes drift apart; pi/test/no_drift.test.mjs fails if
// the class, the constants, or the teardown hook reappear in this file.
import {
  McpBridge,
  clampPollIntervalMs,
  piToolName,
  toolCategory,
  truncateTail,
} from "./mcp_bridge.ts";

// piToolName (":" -> "_" sanitization), toolCategory (family extraction)
// and truncateTail (tail-keeping cut at MAX_RESULT_CHARS) live in
// mcp_bridge.ts since Track 9: one implementation, importable — and
// therefore contract-testable — from node --test against
// tests/surface_diff_snapshot.json.

/** R25-C catalog-drift bookkeeping: after a crash the bridge lazily
 * respawns a NEW server generation, but Pi's registered tool set is from
 * generation 1 forever. Pi's ExtensionAPI exposes no tool unregister, so
 * drift cannot be silently "fixed" here — it is DETECTED (re-list on every
 * respawn), reported through exprs_status, and warned loudly, so the agent
 * knows which registrations are stale instead of trusting them blindly. */
const catalogDrift = {
  lastCheckedGeneration: 0,
  serverVersion: null as string | null,
  previousServerVersion: null as string | null,
  added: [] as string[],
  removed: [] as string[],
  lastError: null as string | null,
};

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

function detectBinary(): string | null {
  const envBin = process.env.EXP_RS_MCP_BIN;
  if (envBin) return envBin;
  const bases = [
    "build/sicnu_geo_rs",
    "build-dev/sicnu_geo_rs",
    "../build/sicnu_geo_rs",
    "./sicnu_geo_rs",
  ];
  // #1186: Windows builds ship .exe — probe both spellings.
  const candidates = bases.flatMap((b) =>
    process.platform === "win32" ? [b + ".exe", b] : [b, b + ".exe"],
  );
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
  const raw =
    process.env.EXP_RS_TOOL_CATEGORIES ??
    "meta,spatial,data,temporal,cartography,symbology,workflow,workspace,layout,harness,mission";
  const set = new Set<string>();
  for (const part of raw.split(",")) {
    const trimmed = part.trim().toLowerCase();
    if (trimmed) set.add(trimmed);
  }
  return set;
}

export default async function (pi: ExtensionAPI) {
  const categories = wantedCategories();
  const extraArgs = (process.env.EXP_RS_MCP_ARGS ?? "").split(" ").filter(Boolean);
  const bin = detectBinary();
  /** MCP tool names registered with Pi for the CURRENT session (R25-C drift
   * detection — see catalogDrift above). */
  const registeredToolNames = new Set<string>();
  const bridge = new McpBridge(bin ?? "sicnu_geo_rs", extraArgs, {
    // R25-C: every completed handshake (re)checks the catalog so a respawn
    // onto a drifted server binary is REPORTED, not silently absorbed.
    onGeneration: (info) => {
      catalogDrift.lastCheckedGeneration = info.generation;
      catalogDrift.serverVersion = info.serverInfo?.version ?? null;
      catalogDrift.previousServerVersion = info.previousServerInfo?.version ?? null;
      if (info.generation <= 1 || registeredToolNames.size === 0) return;
      // Fire-and-forget: the handshake must not block on the re-list, and a
      // failed re-list must not break the fresh generation.
      bridge
        .request("tools/list", { includeSchemas: true })
        .then((list: any) => {
          const current = new Set<string>(
            (list?.tools ?? [])
              .map((t: any) => t.name)
              .filter((n: string) => categories.has(toolCategory(n))),
          );
          const added = [...current].filter((n) => !registeredToolNames.has(n));
          const removed = [...registeredToolNames].filter((n) => !current.has(n));
          catalogDrift.added = added;
          catalogDrift.removed = removed;
          catalogDrift.lastError = null;
          if (added.length || removed.length) {
            console.warn(
              `[exp-rs-spatial] server respawned (generation ${info.generation}` +
                `${info.previousServerInfo?.version !== undefined && info.previousServerInfo ? `, was v${info.previousServerInfo.version}` : ""}) and the tool catalog drifted: ` +
                `+[${added.join(", ")}] -[${removed.join(", ")}]. ` +
                `Pi registrations are from generation 1; restart the Pi session to re-register.`,
            );
          }
        })
        .catch((err: any) => {
          catalogDrift.lastError = String(err?.message ?? err);
        });
    },
  });

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
        // Model-controlled interval: clamp (R21-C F4) — 0/negative values
        // used to busy-loop the poll at event-loop speed for the whole
        // timeout window (the deadline check cannot fire early either).
        const interval = clampPollIntervalMs(params.poll_interval_ms);
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
    // includeSchemas: the bridge registers Pi tools WITH parameter schemas,
    // so it explicitly opts into the embedded-schema listing (the #701
    // default listing is compact; agents discover via search_tools +
    // get_tool_schema instead).
    const list = await bridge.request("tools/list", { includeSchemas: true });
    let count = 0;
    for (const tool of list?.tools ?? []) {
      const category = toolCategory(tool.name);
      if (!categories.has(category)) continue;
      registeredToolNames.add(tool.name);
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
        // R25-C generation honesty: how many server generations this session
        // burned (>=2 means a crash happened), which version answered the
        // last handshake, and whether the tool catalog drifted on respawn.
        const identity =
          `server: ${bridge.serverInfo?.name ?? "unknown"} v${bridge.serverInfo?.version ?? "?"}` +
          ` (protocol ${bridge.protocolVersion ?? "?"}), generation ${bridge.generations}`;
        let drift = "";
        if (bridge.generations > 1) {
          const parts: string[] = [];
          if (catalogDrift.added.length) parts.push(`added: ${catalogDrift.added.join(", ")}`);
          if (catalogDrift.removed.length) parts.push(`removed: ${catalogDrift.removed.join(", ")}`);
          if (catalogDrift.lastError) parts.push(`re-list failed: ${catalogDrift.lastError}`);
          drift =
            ` catalog drift since registration: ${parts.length ? parts.join("; ") : "none detected"}` +
            ` (Pi registrations are from generation 1; restart the session after a crash to re-register)`;
        }
        return {
          content: [{ type: "text" as const, text: `${live}\n${identity}${drift}\n${message}` }],
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
