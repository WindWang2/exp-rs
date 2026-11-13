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
import { existsSync, statSync } from "node:fs";

// Single bridge implementation (Compiler 10.0 drift guard, review F-PI-2):
// the transport lives ONLY in ./mcp_bridge.ts — this extension shell keeps
// the Pi registration surface. A second McpBridge copy here is what let the
// two files' lifecycle fixes drift apart; pi/test/no_drift.test.mjs fails if
// the class, the constants, or the teardown hook reappear in this file.
import {
  BRIDGE_ERRORS,
  McpBridge,
  MAX_RESULT_CHARS,
  bridgeFailure,
  clampPollIntervalMs,
  listAllTools,
  piToolName,
  stableStringify,
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
 * knows which registrations are stale instead of trusting them blindly.
 * R29-T03: the comparison is SCHEMA-level — `changed` lists tools whose
 * inputSchema differs from the schema registered with Pi, because calling
 * a new-arity tool with the old Pi-side parameter schema mis-executes just
 * as silently as calling a removed one. */
const catalogDrift = {
  added: [] as string[],
  removed: [] as string[],
  changed: [] as string[],
  lastError: null as string | null,
  /// True while a post-respawn re-list is still in flight. exprs_status must
  /// not claim "none detected" while the comparison has not completed.
  reListInFlight: false,
  /// Generation whose comparison last LANDED (0 = none beyond generation 1).
  /// The dispatch gate refuses to send a bridged call until this equals the
  /// bridge's live generation — knowing the drift is not the same as having
  /// constrained dispatch on it (R30-T05).
  comparedGeneration: 0,
  /// Settled-or-in-flight comparison for the newest generation that fired
  /// onGeneration. Null only before any post-crash handshake completed.
  comparison: null as Promise<void> | null,
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

/** Longest single string mirrored from structuredContent into details.
 * Envelope metadata (paths, ids, checksums) is short; a huge string in a
 * structured result is a payload projection that belongs in the bounded
 * text surface, not a second unbounded copy inside details. */
const kMaxMirroredStringChars = 4_096;

/** Bounded tail scan for a paged-artifact envelope (R30-T05). The server's
 * text JSON serializes QVariantMap keys alphabetically, so a paged
 * artifact_read envelope puts its giant "content" FIRST and the actionable
 * metadata (path/sha256/next_offset/…) LAST — exactly the part a
 * tail-keeping truncation preserves as text but a failed whole-parse used
 * to drop from details. Inside a JSON string every quote is backslash
 * escaped, so the unescaped `"key":` adjacency can only occur at envelope
 * level — embedded JSON payloads cannot forge it. */
function artifactEnvelopeFromTruncated(text: string): Record<string, any> | null {
  const tail = text.length > 8_192 ? text.slice(-8_192) : text;
  const numberField = (key: string): number | undefined => {
    const m = tail.match(new RegExp(`"${key}"\\s*:\\s*(-?\\d+)`));
    return m ? Number(m[1]) : undefined;
  };
  const stringField = (key: string): string | undefined => {
    const m = tail.match(new RegExp(`"${key}"\\s*:\\s*"((?:[^"\\\\]|\\\\.)*)"`));
    if (!m) return undefined;
    try {
      return JSON.parse(`"${m[1]}"`);
    } catch {
      return m[1];
    }
  };
  const path = stringField("path");
  const sha256 = stringField("sha256");
  const sizeBytes = numberField("size_bytes");
  // Require the identifying triple before claiming an artifact envelope: a
  // path alone could be any tool's output field, and a fabricated ref is
  // worse than an honest "no continuation".
  if (typeof path !== "string" || !path || typeof sha256 !== "string" ||
      !/^[0-9a-f]{64}$/.test(sha256) || typeof sizeBytes !== "number") {
    return null;
  }
  const envelope: Record<string, any> = { path, sha256, size_bytes: sizeBytes };
  const offset = numberField("offset");
  const length = numberField("length");
  const nextOffset = numberField("next_offset");
  const truncated = tail.match(/"truncated"\s*:\s*(true|false)/);
  if (typeof offset === "number") envelope.offset = offset;
  if (typeof length === "number") envelope.length = length;
  if (typeof nextOffset === "number") envelope.next_offset = nextOffset;
  if (truncated) envelope.truncated = truncated[1] === "true";
  return envelope;
}

/** Machine-readable details for a tool result (R29-T03 + R30-T05). The
 * exp-rs server currently answers tools/call with a single text content
 * holding ONE JSON document; mirror it into `details` when it parses WHOLE
 * so Pi consumers get structured fields instead of re-parsing prose. A
 * document truncated by truncateTail no longer parses, so the mirror falls
 * back to a bounded tail scan for a paged-artifact envelope (path, cursor,
 * whole-file checksum) — the continuation must be EXECUTABLE, not a vague
 * suggestion. Native MCP `structuredContent` is authoritative over the
 * text projection, but mirrored strings obey a per-field cap so a
 * structured payload cannot smuggle an unbounded copy past the 50k text
 * budget. */
function structuredDetails(
  result: any,
  text: string,
  toolName?: string,
): Record<string, any> {
  const details: Record<string, any> = {};
  const structured = result?.structuredContent;
  if (structured !== null && structured !== undefined && typeof structured === "object") {
    if (Array.isArray(structured)) details.items = structured;
    else {
      for (const [key, value] of Object.entries(structured)) {
        if (typeof value === "string" && value.length > kMaxMirroredStringChars) {
          details[key] =
            value.slice(0, kMaxMirroredStringChars) +
            `…[truncated: ${value.length - kMaxMirroredStringChars} chars dropped]`;
        } else {
          details[key] = value;
        }
      }
    }
  } else if (text.length <= MAX_RESULT_CHARS) {
    // R2-N2: only an UNTRUNCATED document is mirrored — parsing the
    // original text for an oversized result would carry its full body
    // into details and defeat the 50k budget (the text projection is the
    // bounded surface; a truncated result exposes only the envelope below
    // and the artifact_read continuation).
    const trimmed = text.trim();
    if (trimmed.startsWith("{") || trimmed.startsWith("[")) {
      try {
        const parsed = JSON.parse(trimmed);
        if (parsed !== null && typeof parsed === "object") {
          if (Array.isArray(parsed)) details.items = parsed;
          else Object.assign(details, parsed);
        }
      } catch {
        // not whole-JSON: details stay empty — no fabricated structure
      }
    }
  }
  // Truncation transparency (never a silent cut): when the text exceeded
  // the budget, details say so — and the continuation must match what can
  // actually be recovered. A paged-artifact envelope in the tail yields
  // the real path/cursor/checksum; anything else gets the honest
  // narrow-the-query advice (the server elides oversized outputs without
  // spilling them to files — a generic artifact_read hint would point at
  // a file that does not exist).
  if (text.length > MAX_RESULT_CHARS) {
    details.resultTruncated = true;
    details.resultDroppedChars = text.length - MAX_RESULT_CHARS;
    const envelope = artifactEnvelopeFromTruncated(text);
    if (envelope) {
      details.artifact = envelope;
      const resumeOffset =
        typeof envelope.next_offset === "number" ? envelope.next_offset : envelope.offset ?? 0;
      details.resultContinuation =
        `page the underlying file with the ${piToolName("artifact_read")} tool: ` +
        `{"path": ${JSON.stringify(envelope.path)}, "offset": ${resumeOffset}, "length": 40000} ` +
        `— 40k-char pages stay under the ${MAX_RESULT_CHARS}-char result budget; ` +
        `sha256 ${envelope.sha256} covers the WHOLE file (verify after the last page)`;
    } else {
      details.resultContinuation =
        `no paged artifact in this result; the server elides outputs beyond its ` +
        `512 KiB tool budget instead of spilling them to files — re-call ${toolName ?? "the tool"} ` +
        `with a narrower filter/pagination instead of retrying the same query`;
    }
  }
  return details;
}

/** One text projection of a tools/call content array: text items verbatim,
 * resource items by their embedded text or URI placeholder (R29-T03: a
 * resource-only answer used to project to the empty string). */
function contentToText(result: any): string {
  const parts = (result?.content ?? []).map((c: any) => {
    if (typeof c?.text === "string") return c.text;
    if (c?.type === "resource") {
      const r = c.resource ?? {};
      return typeof r.text === "string" ? r.text : `<resource ${r.uri ?? "?"}>`;
    }
    return "";
  });
  return parts.length ? parts.join("\n") : "(no content)";
}

export default async function (pi: ExtensionAPI) {
  const categories = wantedCategories();
  const extraArgs = (process.env.EXP_RS_MCP_ARGS ?? "").split(" ").filter(Boolean);
  const bin = detectBinary();
  /** MCP tool names registered with Pi for the CURRENT session (R25-C drift
   * detection — see catalogDrift above). */
  const registeredToolNames = new Set<string>();
  /** Canonical inputSchema per registered MCP tool name (R29-T03 schema
   * drift): the registration-time fingerprint the post-respawn re-list
   * compares against — name-equal but schema-different is `changed`. */
  const registeredSchemas = new Map<string, string>();
  /** Bridged tools REJECTED because their sanitized Pi name was already
   * taken (R29-T03): explicit refusal surfaced through exprs_status —
   * never a silent second registerTool over the first registration. */
  const nameCollisions: string[] = [];
  const bridge = new McpBridge(bin ?? "sicnu_geo_rs", extraArgs, {
    // R25-C: every completed handshake (re)checks the catalog so a respawn
    // onto a drifted server binary is REPORTED, not silently absorbed.
    // R30-T05: the comparison result now also CONSTRAINS dispatch —
    // see gateDispatch below.
    onGeneration: (info) => {
      if (info.generation <= 1 || registeredToolNames.size === 0) return;
      runComparison(info.generation);
    },
  });

  /** Post-respawn schema comparison for `generation` (R29-T03 detection +
   * R30-T05 gate authority). Fire-and-forget at handshake time; the dispatch
   * gate awaits catalogDrift.comparison so calls never race past an
   * unsettled verdict. A comparison that lands for a SUPERSEDED generation
   * still resolves its promise (waiters re-check comparedGeneration), but
   * never overwrites the newer generation's drift state.
   * Returns the settle promise so the gate can also re-run a FAILED
   * comparison once per call. */
  const runComparison = (generationAtRequest: number): Promise<void> => {
    catalogDrift.reListInFlight = true;
    let settleComparison: () => void = () => {};
    const comparison = new Promise<void>((resolve) => {
      settleComparison = resolve;
    });
    catalogDrift.comparison = comparison;
    listAllTools(bridge, { includeSchemas: true })
      .then((tools: any[]) => {
        const stale = bridge.generations !== generationAtRequest;
        if (!stale) {
          const current = new Map<string, string>();
          for (const t of tools ?? []) {
            if (!categories.has(toolCategory(t.name))) continue;
            current.set(t.name, stableStringify(t.inputSchema ?? null));
          }
          const added: string[] = [];
          const removed: string[] = [];
          const changed: string[] = [];
          for (const [name, schema] of current) {
            if (!registeredToolNames.has(name)) added.push(name);
            else if (registeredSchemas.get(name) !== schema) changed.push(name);
          }
          for (const name of registeredToolNames) {
            if (!current.has(name)) removed.push(name);
          }
          catalogDrift.added = added;
          catalogDrift.removed = removed;
          catalogDrift.changed = changed;
          catalogDrift.lastError = null;
          catalogDrift.comparedGeneration = generationAtRequest;
          catalogDrift.reListInFlight = false;
          if (added.length || removed.length || changed.length) {
            console.warn(
              `[exp-rs-spatial] server respawned (generation ${generationAtRequest}) and the tool catalog drifted: ` +
                `+[${added.join(", ")}] ~[${changed.join(", ")}] -[${removed.join(", ")}]. ` +
                `Calls to changed/removed tools are refused until the session restarts; unchanged tools keep working.`,
            );
          }
        }
        settleComparison();
      })
      .catch((err: any) => {
        if (bridge.generations !== generationAtRequest) {
          settleComparison();
          return;
        }
        catalogDrift.lastError = String(err?.message ?? err);
        catalogDrift.reListInFlight = false;
        settleComparison();
      });
    return comparison;
  };

  /// Bounded wait for the drift gate: how long a dispatch may hold for the
  /// live generation's catalog comparison to settle before refusing with a
  /// retryable SCHEMA_UNVERIFIED (a local handshake answers a re-list in
  /// tens of milliseconds; five seconds tolerates a slow disk or a busy
  /// server without letting a call hang indefinitely).
  const kDriftGateWaitMs = 5_000;

  /** Dispatch gate (R30-T05): a bridged call may only reach the server when
   * the catalog comparison of the CURRENT generation has landed AND the
   * tool's live schema still equals the schema registered with Pi.
   * - changed/removed tool → typed non-retryable SCHEMA_DRIFT refusal;
   * - comparison pending → bounded wait (the generation that will serve the
   *   call must be verified first);
   * - comparison failed → one retry of the comparison per call, then a
   *   retryable SCHEMA_UNVERIFIED refusal carrying the failure reason.
   * Generation 1 dispatches without a gate: the registration listing IS the
   * live catalog of that generation. */
  const gateDispatch = async (toolName: string, signal?: AbortSignal): Promise<void> => {
    const deadline = Date.now() + kDriftGateWaitMs;
    let retriedFailedComparison = false;
    let awaited: Promise<void> | null = null;
    for (;;) {
      const generation = bridge.generations;
      if (generation <= 1) return; // registration-time catalog is live
      if (catalogDrift.comparedGeneration === generation) {
        if (catalogDrift.changed.includes(toolName)) {
          throw bridgeFailure(
            `tool '${toolName}' changed its input schema since this Pi session ` +
              `registered it (generation ${generation}); dispatching with the stale ` +
              `Pi-side schema could silently mis-execute. Restart the Pi session to ` +
              `re-register (exprs_status lists the drift); unchanged tools keep working.`,
            BRIDGE_ERRORS.SCHEMA_DRIFT,
            false,
          );
        }
        if (catalogDrift.removed.includes(toolName)) {
          throw bridgeFailure(
            `tool '${toolName}' was removed from the server catalog since this Pi ` +
              `session registered it (generation ${generation}); the registration is ` +
              `stale. Restart the Pi session to re-register (exprs_status lists the drift).`,
            BRIDGE_ERRORS.SCHEMA_DRIFT,
            false,
          );
        }
        return; // unchanged: the registered schema still matches the live one
      }
      // The live generation is not verified yet: pending, failed, or a
      // settled-but-stale comparison promise. A failed comparison gets ONE
      // on-demand retry per gated call (transient re-list failures recover
      // without a session restart); anything still unsettled at the deadline
      // refuses retryable instead of dispatching blind.
      if (
        catalogDrift.lastError &&
        !catalogDrift.reListInFlight &&
        !retriedFailedComparison
      ) {
        retriedFailedComparison = true;
        runComparison(generation);
      }
      const comparison = catalogDrift.comparison;
      if (
        !comparison ||
        comparison === awaited || // already settled without a verdict: no progress left
        Date.now() >= deadline
      ) {
        const why = catalogDrift.lastError
          ? `the post-respawn catalog comparison failed (${catalogDrift.lastError})`
          : "the post-respawn catalog comparison has not settled";
        throw bridgeFailure(
          `cannot verify tool '${toolName}' against generation ${generation}: ${why}; ` +
            `retry the call, or restart the Pi session (exprs_status shows drift state)`,
          BRIDGE_ERRORS.SCHEMA_UNVERIFIED,
          true,
        );
      }
      awaited = comparison;
      const remaining = Math.max(0, deadline - Date.now());
      await Promise.race([
        comparison,
        new Promise((resolve) => setTimeout(resolve, remaining)),
        new Promise((resolve) => {
          signal?.addEventListener("abort", () => resolve(undefined), { once: true });
        }),
      ]);
      if (signal?.aborted) throw new Error("Aborted before dispatch");
      // Loop: re-read the generation — a crash during the wait moves the
      // goalpoint to the newer generation's own comparison.
    }
  };

  const guidelines = [
    "exp-rs spatial tools: call exprs_search_algorithms or exprs_list_algorithms to discover capabilities before planning a remote-sensing workflow.",
    "exprs_spatial_raster_inspect reveals CRS, resolution, band roles, and radiometric state — inspect inputs before choosing algorithms.",
    "exprs_preflight_algorithm validates parameters and estimates RAM without executing; prefer it over blind execution.",
    "exprs_execute_algorithm returns an execution_id immediately; poll exprs_get_execution_status or await it with exprs_wait_for_execution.",
    "Multi-step pipelines (preprocess → index → classify → vectorize) belong in exprs_run_workflow, not in chained single calls.",
    "exprs_status reports bridge health, server generation, catalog drift, and which tool categories this session bridged — call it when tools misbehave or to see opt-in families such as scientific: (agent sessions, bridged via EXP_RS_TOOL_CATEGORIES).",
  ];

  const registerWaitTool = () => {
    pi.registerTool({
      name: "exprs_wait_for_execution",
      label: "Wait for exp-rs execution",
      description:
        "Poll an exp-rs execution (execution_id from exprs_execute_algorithm or exprs_run_workflow steps) until it completes, fails, or is canceled. Returns the final status including the committed asset id when available. " +
        "timeout_seconds bounds the WHOLE wait (status requests and sleeps alike) and only stops waiting — it does NOT cancel the execution; aborting the tool call DOES request a server-side cancel.",
      // Plain JSON Schema (identical wire shape to Type.Object — the
      // bridged tools already pass plain objects): keeps the shell
      // dependency-free (README contract) and executable under node's
      // type-stripping in tests, without the host-provided `typebox`
      // package (R29-T03).
      parameters: {
        type: "object",
        properties: {
          execution_id: { type: "string", description: "Execution id (task-<id>) to wait for" },
          timeout_seconds: { type: "number", description: "Give up after this many seconds (default 300; must be finite and positive)" },
          poll_interval_ms: { type: "number", description: "Poll interval in milliseconds (default 1000)" },
        },
        required: ["execution_id"],
      } as any,
      async execute(_toolCallId, params, signal) {
        // R30-T05: a non-finite or non-positive budget used to reach the
        // deadline arithmetic as NaN (never fires) or 0 (instant timeout) —
        // refuse the call instead of polling unbounded.
        const timeoutSeconds = params.timeout_seconds ?? 300;
        if (
          typeof timeoutSeconds !== "number" || !Number.isFinite(timeoutSeconds) ||
          timeoutSeconds <= 0
        ) {
          throw new Error(
            `timeout_seconds must be a finite positive number of seconds (got ${String(params.timeout_seconds)})`,
          );
        }
        const deadline = Date.now() + timeoutSeconds * 1000;
        // Model-controlled interval: clamp (R21-C F4) — 0/negative values
        // used to busy-loop the poll at event-loop speed for the whole
        // timeout window (the deadline check cannot fire early either).
        const interval = clampPollIntervalMs(params.poll_interval_ms);
        // Abort must also stop SERVER-side work: fire-and-forget a cancel so
        // an aborted 30-minute classification does not keep burning CPU
        // while Pi believes it stopped (#623). "Cancel REQUESTED" is not
        // "canceled": the acknowledgement is the server's eventual
        // canceled/terminal status, observable via get_execution_status.
        const cancelServerSide = () => {
          bridge
            .request("tools/call", {
              name: "cancel_execution",
              arguments: { execution_id: params.execution_id },
            })
            .catch(() => {});
        };
        signal?.addEventListener("abort", cancelServerSide, { once: true });
        /** Sleep that abort and the deadline can both cut short (R30-T05:
         * the old plain setTimeout slept past both). The abort listener is
         * removed on every settle path — a long wait polling every 200 ms
         * used to stack one closure per iteration onto a long-lived caller
         * signal. */
        const sleepBounded = async (ms: number) => {
          await new Promise<void>((resolve) => {
            const onAbort = () => {
              clearTimeout(timer);
              resolve();
            };
            const timer = setTimeout(() => {
              signal?.removeEventListener("abort", onAbort);
              resolve();
            }, ms);
            signal?.addEventListener("abort", onAbort, { once: true });
          });
        };
        const knownStatus = (status: string | null): status is string =>
          status !== null &&
          ["running", "queued", "waiting_resource", "cancelling",
           "completed", "failed", "canceled"].includes(status);
        const terminal = (status: string) =>
          status === "completed" || status === "failed" || status === "canceled";
        let lastStatus = "unknown";
        let badStatusStreak = 0;
        try {
          for (;;) {
            if (signal?.aborted) throw new Error("Aborted while waiting for execution");
            const remaining = deadline - Date.now();
            if (remaining <= 0) {
              throw new Error(
                `Timed out waiting for execution '${params.execution_id}' (still '${lastStatus}')`,
              );
            }
            // The poll itself is bounded by the remaining budget: a slow
            // status response must not blow through the total wait (the
            // deadline fires here, not after the response lands). Aborting
            // a read-only status poll cancels nothing server-side (the
            // server blacklists get_execution_status from its cancel map).
            const pollSignal = signal
              ? AbortSignal.any([signal, AbortSignal.timeout(remaining)])
              : AbortSignal.timeout(remaining);
            let result: any;
            try {
              await gateDispatch("get_execution_status", signal);
              result = await bridge.request(
                "tools/call",
                {
                  name: "get_execution_status",
                  arguments: { execution_id: params.execution_id },
                },
                pollSignal,
              );
            } catch (err: any) {
              if (signal?.aborted)
                throw new Error("Aborted while waiting for execution");
              if (Date.now() >= deadline)
                throw new Error(
                  `Timed out waiting for execution '${params.execution_id}' (still '${lastStatus}')`,
                );
              throw err;
            }
            // Typed poll failures surface immediately (R30-T05): isError
            // with errorCode/errorCategory used to degrade into "unknown
            // status" and poll until the timeout hid the real cause.
            if (result?.isError) {
              const text = contentToText(result);
              const err = new Error(truncateTail(text));
              (err as any).errorCode = result?.errorCode;
              (err as any).errorCategory = result?.errorCategory;
              if (result?.retryable !== undefined) {
                (err as any).retryable = result.retryable === true;
              }
              throw err;
            }
            const text: string = contentToText(result);
            let status: string | null = null;
            try {
              const parsed = JSON.parse(text);
              if (typeof parsed?.status === "string") status = parsed.status;
            } catch {
              // malformed payload: handled by the streak below
            }
            if (!knownStatus(status)) {
              // Three consecutive unparseable/unknown statuses are a broken
              // poll contract, not a transient blip — keep polling "blind"
              // for the whole timeout used to hide exactly that.
              if (++badStatusStreak >= 3) {
                throw new Error(
                  `get_execution_status returned ${badStatusStreak} consecutive malformed or ` +
                    `unknown statuses (last: ${JSON.stringify(text.slice(0, 120))}); aborting the wait — ` +
                    `poll exprs_get_execution_status directly to investigate`,
                );
              }
            } else {
              badStatusStreak = 0;
              lastStatus = status;
              if (terminal(status)) {
                // Abort may have fired while this terminal response was in
                // flight: the waiter already ended at abort, and a late
                // terminal must not resurrect it as a success (R30-T05).
                if (signal?.aborted) {
                  throw new Error(
                    `Aborted while waiting for execution (a terminal '${status}' arrived after the abort; ` +
                      `confirm via exprs_get_execution_status)`,
                  );
                }
                return {
                  content: [{ type: "text" as const, text: truncateTail(text) }],
                  details: structuredDetails(result, text),
                };
              }
            }
            await sleepBounded(Math.min(interval, Math.max(0, deadline - Date.now())));
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
    // get_tool_schema instead). listAllTools consumes the server's opt-in
    // pagination when a page lands split (R29-T03).
    const tools = await listAllTools(bridge, { includeSchemas: true });
    // The shell's own tools claim their Pi names FIRST (registered before
    // the bridged slice): a bridged tool whose sanitized name collides is
    // rejected explicitly and reported — a second registerTool would either
    // throw in the host or silently shadow the first registration (R29-T03).
    const claimedPiNames = new Set<string>(["exprs_status", "exprs_wait_for_execution"]);
    let count = 0;
    for (const tool of tools ?? []) {
      const category = toolCategory(tool.name);
      if (!categories.has(category)) continue;
      const piName = piToolName(tool.name);
      if (claimedPiNames.has(piName)) {
        nameCollisions.push(`${tool.name} → ${piName}`);
        console.warn(
          `[exp-rs-spatial] rejecting '${tool.name}': sanitized Pi tool name ` +
            `'${piName}' collides with an already-registered tool; ` +
            `see exprs_status (nameCollisions)`,
        );
        continue;
      }
      claimedPiNames.add(piName);
      registeredToolNames.add(tool.name);
      registeredSchemas.set(tool.name, stableStringify(tool.inputSchema ?? null));
      const schema =
        tool.inputSchema && tool.inputSchema.type === "object"
          ? tool.inputSchema
          : { type: "object", properties: {} };
      pi.registerTool({
        name: piName,
        label: `exp-rs ${tool.name}`,
        description: `[exp-rs ${tool.name}] ${tool.description ?? ""}`.slice(0, 8_000),
        parameters: schema as any,
        promptGuidelines: count === 0 ? guidelines : undefined,
        async execute(_toolCallId, params, signal, onUpdate) {
          onUpdate?.({ content: [{ type: "text", text: `exp-rs ${tool.name} …` }] });
          if (signal?.aborted) throw new Error("Aborted before dispatch");
          // R30-T05 dispatch gate: the generation that serves this call must
          // have a LANDED catalog comparison, and the tool must be unchanged
          // in it. A call that crossed a crash boundary (the lazy respawn
          // served it on a NEWER generation than the one the gate verified)
          // re-verifies: a crash-class failure re-gates and re-dispatches
          // (bounded retries); a SUCCESS from an unverified generation is
          // checked post-hoc and refused if that generation drifted — the
          // response would otherwise be the product of a stale-schema call.
          for (let attempt = 0; ; attempt++) {
            await gateDispatch(tool.name, signal);
            const generationBefore = bridge.generations;
            let result: any;
            try {
              result = await requestOrAbort(
                bridge,
                "tools/call",
                { name: tool.name, arguments: params },
                signal,
              );
            } catch (err: any) {
              const crashCrossed =
                generationBefore !== bridge.generations && !signal?.aborted;
              const retryableCrash =
                err?.bridgeError === BRIDGE_ERRORS.CHILD_EXITED ||
                err?.bridgeError === BRIDGE_ERRORS.RESTART_FAILED;
              if (crashCrossed && retryableCrash && attempt < 2) continue;
              throw err;
            }
            if (generationBefore !== bridge.generations && !signal?.aborted) {
              // The answer came from a generation the gate never verified.
              await gateDispatch(tool.name, signal);
            }
            const text = contentToText(result);
            if (result?.isError) {
              const err = new Error(truncateTail(text));
              // Preserve the structured taxonomy (INVALID_PARAMETER,
              // PATH_OUTSIDE_WORKSPACE, ...) so Pi can classify instead of
              // parsing prose - protocol errors already carried code/data;
              // tool execution errors dropped them (#645). `retryable` rides
              // the same result object (the server always emits it) and is
              // the retry/re-plan signal of docs/agent/pi-adapter.md (R29-T03).
              (err as any).errorCode = result?.errorCode;
              (err as any).errorCategory = result?.errorCategory;
              if (result?.retryable !== undefined) {
                (err as any).retryable = result.retryable === true;
              }
              throw err;
            }
            return {
              content: [{ type: "text" as const, text: truncateTail(text) }],
              details: structuredDetails(result, text, tool.name),
            };
          }
        },
      });
      count++;
    }
    return count;
  };

  /** Mutable startup note read by exprs_status at CALL time (not captured at
   * registration): the tool is registered exactly once per extension load,
   * on EVERY path, and reports the CURRENT state — the startup snapshot is
   * only the first line of its history (R29-T03: healthy sessions used to
   * have no status tool at all; only failures did). */
  const bridgeNote = { text: "" as string };
  const registerStatusTool = () => {
    pi.registerTool({
      name: "exprs_status",
      label: "exp-rs bridge status",
      description:
        "Reports whether the exp-rs spatial bridge is connected (live liveness, not the startup snapshot): server identity/protocol/generation, bridged categories and tool counts, catalog drift, unmet dependencies, and the next action to fix each. Always available — healthy, crashed, or never-started.",
      parameters: { type: "object", properties: {} } as any,
      async execute() {
        const live = bridge.alive
          ? "bridge: CONNECTED (child process alive)"
          : `bridge: DISCONNECTED${bridge.lastFailure ? ` (${bridge.lastFailure})` : ""}`;
        // R25-C generation honesty: how many server generations this session
        // burned (>=2 means a crash happened), which version answered the
        // last handshake, and whether the tool catalog drifted on respawn.
        const identity =
          `server: ${bridge.serverInfo?.name ?? "unknown"} v${bridge.serverInfo?.version ?? "?"}` +
          ` (protocol ${bridge.protocolVersion ?? "?"}), generation ${bridge.generations}`;
        let drift = "";
        if (bridge.generations > 1 || catalogDrift.reListInFlight) {
          const parts: string[] = [];
          if (catalogDrift.added.length) parts.push(`added: ${catalogDrift.added.join(", ")}`);
          if (catalogDrift.changed.length)
            parts.push(`schema changed: ${catalogDrift.changed.join(", ")}`);
          if (catalogDrift.removed.length) parts.push(`removed: ${catalogDrift.removed.join(", ")}`);
          if (catalogDrift.lastError) parts.push(`re-list failed: ${catalogDrift.lastError}`);
          // An unfinished re-list has made NO comparison yet: saying "none
          // detected" would be a claim this session has not established.
          if (!parts.length && catalogDrift.reListInFlight)
            parts.push("comparison still in flight (no drift observed yet)");
          drift =
            ` catalog drift since registration: ${parts.length ? parts.join("; ") : "none detected"}` +
            ` (Pi registrations are from generation 1; restart the session after a crash to re-register)`;
        }
        // Capabilities + unmet dependencies + next actions (R29-T03): what
        // THIS session bridged, what it could not, and what to do next.
        const enabledCategories = [...categories].join(",");
        const capabilities =
          `bridge capabilities: ${registeredToolNames.size} tools in [${enabledCategories}]` +
          (nameCollisions.length
            ? ` (${nameCollisions.length} name collision(s) rejected, see details)`
            : "");
        const unmet: string[] = [];
        const nextActions: string[] = [];
        if (!bin) {
          unmet.push("exp-rs binary not found");
          nextActions.push(
            "build the project (build-dev/sicnu_geo_rs) or set EXP_RS_MCP_BIN to the desktop binary; it is launched with --mcp",
          );
        }
        if (bin && !bridge.alive) {
          if (bridge.fastCrashes >= 5) {
            unmet.push(`server crash-looping (${bridge.fastCrashes} fast crashes; lazy respawn disabled)`);
            nextActions.push("fix/rebuild the binary, then restart the Pi session");
          } else if (bridge.generations === 0) {
            unmet.push(`server failed to start: ${bridge.lastFailure ?? "unknown"}`);
            nextActions.push("set EXP_RS_MCP_BIN or rebuild the project, then retry");
          } else {
            unmet.push(`server disconnected (generation ${bridge.generations} ended)`);
            nextActions.push("the next tool call lazily respawns the server; restart the Pi session if it keeps crashing");
          }
        }
        if (catalogDrift.added.length || catalogDrift.changed.length || catalogDrift.removed.length) {
          unmet.push("tool catalog drifted since Pi registration");
          nextActions.push("restart the Pi session so registrations match the live server catalog");
        }
        // Discoverability of the scientific session surface (R29-T03): the
        // scientific: family is a DELIBERATE default-off opt-in (surface
        // snapshot pins it intentionally unbridged); the status tool is the
        // place an agent learns the opt-in exists.
        if (!categories.has("scientific")) {
          nextActions.push(
            "scientific sessions are opt-in: add 'scientific' to EXP_RS_TOOL_CATEGORIES to bridge scientific:agent_session / scientific:context / scientific:capabilities",
          );
        }
        const lines = [live, identity + drift, capabilities];
        if (unmet.length) lines.push(`unmet dependencies: ${unmet.join("; ")}`);
        if (nextActions.length) lines.push(`next actions: ${nextActions.join("; ")}`);
        if (bridgeNote.text) lines.push(bridgeNote.text);
        return {
          content: [{ type: "text" as const, text: lines.join("\n") }],
          details: {
            connected: bridge.alive,
            binary: bin,
            generation: bridge.generations,
            protocolVersion: bridge.protocolVersion,
            server: bridge.serverInfo ?? null,
            categories: [...categories],
            registeredTools: registeredToolNames.size,
            nameCollisions,
            drift: {
              added: catalogDrift.added,
              changed: catalogDrift.changed,
              removed: catalogDrift.removed,
              lastError: catalogDrift.lastError,
              comparisonInFlight: catalogDrift.reListInFlight,
              // The generation whose comparison last LANDED: when this is
              // behind `generation`, calls wait (bounded) or refuse
              // retryable — drift is enforced at dispatch, not just
              // reported (R30-T05).
              comparisonGeneration: catalogDrift.comparedGeneration,
            },
            unmetDependencies: unmet,
            nextActions,
          },
        };
      },
    });
  };

  // ONE registration source, EVERY path (R29-T03): the status tool exists
  // before anything can fail, so a half-registered session is still
  // observable — "child alive" is never the only signal.
  registerStatusTool();
  if (!bin) {
    bridgeNote.text =
      "exp-rs bridge: no binary found. Build the project (build/sicnu_geo_rs) or set EXP_RS_MCP_BIN to the desktop binary; it is launched with --mcp.";
    return;
  }
  try {
    await bridge.start();
    const count = await registerBridgedTools();
    registerWaitTool();
    bridgeNote.text = `startup: bridged ${count} tools from ${bin}`;
    console.log(`[exp-rs-spatial] bridged ${count} tools from ${bin}`);
  } catch (err: any) {
    // start() may have spawned a healthy child that then failed the tool
    // registration - tear it down so nothing leaks (#623).
    bridge.stop();
    bridgeNote.text = `exp-rs bridge failed to start (${bin}): ${err?.message ?? err}. Set EXP_RS_MCP_BIN or rebuild the project.`;
    console.error(`[exp-rs-spatial] ${bridgeNote.text}`);
  }
}
