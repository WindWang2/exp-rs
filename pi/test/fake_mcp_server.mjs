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
 * - arguments {delay_ms}: reply late (abort-mid-flight races).
 * - arguments {error_shape: true}: reply with a structured tool error
 *   (isError + errorCode + errorCategory, the #645 passthrough contract).
 * - arguments {garbage_line: true}: emit a non-JSON stdout line before the
 *   valid response (framing-tolerance contract).
 * - arguments {huge: N}: reply with an N-char text (transport fidelity:
 *   the bridge must NOT truncate; MAX_RESULT_CHARS is the shell's budget).
 */
import { appendFileSync } from "node:fs";

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
const counterPath = [...args].reverse().find((a) => !a.startsWith("--") && a !== notifyLog);
appendFileSync(counterPath, "1\n");

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
    send({ jsonrpc: "2.0", id: msg.id, result: { protocolVersion: "2024-11-05", capabilities: {}, serverInfo: { name: "fake", version: "0" } } });
    return;
  }
  if (msg.method === "tools/call") {
    const a = msg.params ?? {};
    const args = a.arguments ?? {};
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
