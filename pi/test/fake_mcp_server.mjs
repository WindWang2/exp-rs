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
 */
import { appendFileSync } from "node:fs";

const args = process.argv.slice(2);
const counterPath = args[0] === "--mcp" ? args[1] : args[0];
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
  if (msg.id === undefined || msg.id === null) return; // notification
  if (msg.method === "initialize") {
    send({ jsonrpc: "2.0", id: msg.id, result: { protocolVersion: "2024-11-05", capabilities: {}, serverInfo: { name: "fake", version: "0" } } });
    return;
  }
  if (msg.method === "tools/call") {
    const a = msg.params ?? {};
    if (a.crash || (a.arguments && a.arguments.crash)) {
      // Crash mid-run without replying — the exact #669 trigger shape.
      process.exit(1);
    }
    send({ jsonrpc: "2.0", id: msg.id, result: { content: [{ type: "text", text: "ok" }] } });
    return;
  }
  send({ jsonrpc: "2.0", id: msg.id, result: {} });
}
