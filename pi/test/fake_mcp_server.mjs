#!/usr/bin/env node
/**
 * Fake exp-rs MCP server for bridge lifecycle tests (#669).
 *
 * Spawned by McpBridge as: <this file> --mcp <counter-file>
 * - appends one line ("1") to the counter file on every spawn, so tests can
 *   assert exactly how many children were created.
 * - answers `initialize` and `tools/call` normally.
 * - a tools/call with arguments {crash: true} makes it exit(1) WITHOUT
 *   replying (simulates a child crash mid-run, the trigger from #669).
 *
 * Track 9 (mcp-surface-r4) additive modes, all opt-in via arguments so the
 * original triggers keep their exact shapes:
 * - --notify-log <path> (argv): every received notification is appended to
 *   the file as one JSON line (cancellation-count assertions).
 * - --hang-initialize (argv, no value): never answer `initialize` — the
 *   child spawns fine (counter still ticks) but the handshake hangs, so the
 *   bridge's startup deadline must fire and tear it down. The flag carries
 *   no value, so the last-argument counter detection below is unaffected.
 * - --hang-tool-calls (argv, no value): answer `initialize` normally but
 *   never answer `tools/call` — the per-request timeout must fire
 *   client-side, and (R18-C C05) the bridge must then send
 *   notifications/cancelled so server-side work stops too. Other methods
 *   still answer, so post-timeout bridge health is observable via ping.
 * - arguments {id_null_error: true}: instead of answering, emit an id:null
 *   JSON-RPC error frame (-32700, the server's unattributable parse-fault
 *   shape). The bridge must reject ALL pending requests on it and stay
 *   usable afterwards (R18-C C09).
 * - arguments {delay_ms}: reply late (abort-mid-flight races).
 * - arguments {error_shape: true}: reply with a structured tool error
 *   (isError + errorCode + errorCategory, the #645 passthrough contract).
 * - arguments {garbage_line: true}: emit a non-JSON stdout line before the
 *   valid response (framing-tolerance contract).
 * - arguments {huge: N}: reply with an N-char text (transport fidelity:
 *   the bridge must NOT truncate; MAX_RESULT_CHARS is the shell's budget).
 *
 * R25-C additive modes (generation-drift contract), opt-in via argv:
 * - --version-file <path> (argv): serverInfo.version is READ FROM the file
 *   at every `initialize`, so a test can rewrite it between a crash and the
 *   lazy respawn and assert the bridge observes the version drift.
 * - --tools-file <path> (argv): `tools/list` reads a JSON array of
 *   {name, description?} from the file, so the catalog can change across
 *   generations the same way. Absent/unreadable file → empty tool list.
 * - --die-if-exists <path> (argv): if the file EXISTS at spawn time, exit(1)
 *   immediately (before answering anything) — lets a test make the NEXT
 *   generation unstartable so the bridge's restart-failure path settles.
 */
import { appendFileSync, readFileSync } from "node:fs";

const args = process.argv.slice(2);
function argAfter(flag) {
  const i = args.indexOf(flag);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : undefined;
}
// The counter is the LAST argument that is neither a flag nor the notify
// log's value: it may ride directly after --mcp (legacy shape
// ["--mcp", counter]) or after other flag/value pairs
// (["--mcp", "--notify-log", path, counter]). The notify log rides its own
// "--notify-log <path>" pair.
const notifyLog = argAfter("--notify-log");
const versionFile = argAfter("--version-file");
const toolsFile = argAfter("--tools-file");
const dieIfExists = argAfter("--die-if-exists");
const hangInitialize = args.includes("--hang-initialize");
const hangToolCalls = args.includes("--hang-tool-calls");
// The counter must not collide with the VALUE of any flag/value pair.
const pairValues = new Set([notifyLog, versionFile, toolsFile, dieIfExists]);
const counterPath = [...args]
  .reverse()
  .find((a) => !a.startsWith("--") && !pairValues.has(a));
appendFileSync(counterPath, "1\n");
// --die-if-exists: a poisoned generation dies before the handshake so the
// bridge's restart-failure path settles (R25-C typed-error contract).
if (dieIfExists) {
  try {
    readFileSync(dieIfExists);
    process.exit(1);
  } catch {
    // file absent → healthy generation
  }
}

let buffer = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => {
  buffer += chunk;
  let newline = buffer.indexOf("\n");
  while (newline >= 0) {
    const line = buffer.slice(0, newline).trim();
    buffer = buffer.slice(newline + 1);
    if (line) handle(line);
    newline = buffer.indexOf("\n");
  }
});

function send(msg) {
  process.stdout.write(JSON.stringify(msg) + "\n");
}

function handle(line) {
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;
  }
  if (msg.id === undefined || msg.id === null) {
    // Notification: record it when a notify log was requested, so tests
    // can assert exactly how many notifications/cancelled arrived (#645).
    if (notifyLog) {
      try {
        appendFileSync(notifyLog, JSON.stringify(msg) + "\n");
      } catch {
        // best-effort only
      }
    }
    return;
  }
  if (msg.method === "initialize") {
    // --hang-initialize: spawn + counter still work; the handshake reply is
    // withheld forever, so the bridge-side startup deadline is what settles.
    if (hangInitialize) return;
    // --version-file: the version is read per-handshake so a test can
    // rewrite it between generations (R25-C version-drift contract).
    let version = "0";
    if (versionFile) {
      try {
        version = readFileSync(versionFile, "utf8").trim() || "0";
      } catch {
        // unreadable/absent → keep the default
      }
    }
    send({ jsonrpc: "2.0", id: msg.id, result: { protocolVersion: "2024-11-05", capabilities: {}, serverInfo: { name: "fake", version } } });
    return;
  }
  if (msg.method === "tools/list") {
    // --tools-file: JSON array of {name, description?} read per-request so
    // the catalog can drift across generations (R25-C catalog-drift
    // contract). Absent/unreadable → empty list (never a fake catalog).
    let tools = [];
    if (toolsFile) {
      try {
        tools = JSON.parse(readFileSync(toolsFile, "utf8"));
      } catch {
        tools = [];
      }
    }
    send({ jsonrpc: "2.0", id: msg.id, result: { tools } });
    return;
  }
  if (msg.method === "tools/call") {
    // --hang-tool-calls: the handshake completed, so the bridge is healthy;
    // every tools/call is then withheld forever — the client-side request
    // timeout is the only way the call settles (R18-C C05 trigger).
    if (hangToolCalls) return;
    const a = msg.params ?? {};
    const args = a.arguments ?? {};
    if (args.id_null_error) {
      // Unattributable protocol fault: id:null errors cannot be matched to
      // a request, so the bridge must fail EVERY pending call (and must not
      // treat the frame as a notification to silently drop).
      send({ jsonrpc: "2.0", id: null, error: { code: -32700, message: "Parse error" } });
      return;
    }
    if (a.crash || args.crash) {
      // Crash mid-run without replying — the exact #669 trigger shape.
      process.exit(1);
    }
    if (a.flood || args.flood) {
      // F-PI-1 trigger shape: >32 MiB of bytes with NO terminating newline,
      // then normal operation. A bridge that only fails fast stays zombied —
      // every later response glues onto the runaway line.
      process.stdout.write("x".repeat(33 * 1024 * 1024));
      send({ jsonrpc: "2.0", id: msg.id, result: { content: [{ type: "text", text: "ok" }] } });
      return;
    }
    if (Number.isFinite(args.delay_ms) && args.delay_ms > 0) {
      setTimeout(() => {
        send({ jsonrpc: "2.0", id: msg.id, result: { content: [{ type: "text", text: "late" }] } });
      }, args.delay_ms);
      return;
    }
    if (args.error_shape) {
      send({
        jsonrpc: "2.0",
        id: msg.id,
        result: {
          content: [{ type: "text", text: "boom: invalid parameters" }],
          isError: true,
          errorCode: "E_TEST_SHAPE",
          errorCategory: "test",
          retryable: false,
        },
      });
      return;
    }
    if (args.garbage_line) {
      process.stdout.write("}\x00not json at all {{\n");
      send({ jsonrpc: "2.0", id: msg.id, result: { content: [{ type: "text", text: "ok" }] } });
      return;
    }
    if (Number.isFinite(args.huge) && args.huge > 0) {
      send({ jsonrpc: "2.0", id: msg.id, result: { content: [{ type: "text", text: "y".repeat(args.huge) }] } });
      return;
    }
    send({ jsonrpc: "2.0", id: msg.id, result: { content: [{ type: "text", text: "ok" }] } });
    return;
  }
  send({ jsonrpc: "2.0", id: msg.id, result: {} });
}
