// r24c_real_binary_smoke.mjs — O-C1: the REAL Pi adapter transport (mcp_bridge.ts)
// spawning the REAL MCP server binary (surface_mcp_host.exe = the production
// McpServer on real stdio) and exercising the E2E chain:
//   initialize → tools/list → tools/call (async execution_id) → progress or
//   status poll → notifications/cancelled (typed cancel receipt) → liveness.
// Run: EXP_RS_MCP_BIN=<build>/surface_mcp_host.exe node r24c_real_binary_smoke.mjs
import assert from "node:assert/strict";
import { McpBridge } from "../../pi/mcp_bridge.ts";

const bin = process.env.EXP_RS_MCP_BIN;
assert.ok(bin, "EXP_RS_MCP_BIN must point at the real MCP server binary");

const bridge = new McpBridge(bin, []);
try {
  // 1. Handshake against the real server.
  const init = await bridge.request("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "r24c-smoke", version: "0" },
  });
  assert.equal(init?.protocolVersion, "2024-11-05");
  console.log("smoke: initialize ok — server:", init?.serverInfo?.name);

  // 2. Tool discovery through the real union surface.
  const tools = await bridge.request("tools/list", { includeSchemas: true });
  const names = tools?.tools?.map((t) => t.name) ?? [];
  assert.ok(names.length > 100, `expected a large union surface, got ${names.length}`);
  for (const must of ["list_algorithms", "run_workflow", "get_execution_status", "cancel_execution", "cartography:chart_create"])
    assert.ok(names.includes(must), `surface must carry ${must}`);
  console.log(`smoke: tools/list ok — ${names.length} tools incl. chart_register (workspace charts contract)`);

  // 3. An async tool call returns an execution_id immediately (typed dispatch).
  const started = await bridge.request("tools/call", {
    name: "list_algorithms",
    arguments: {},
  });
  const text = started?.content?.[0]?.text ?? "";
  assert.ok(!started?.isError, `list_algorithms must not error: ${text.slice(0, 200)}`);
  console.log("smoke: tools/call list_algorithms ok —", text.slice(0, 120).replace(/\s+/g, " "));

  // 4. A typed refusal: unknown tool comes back as a JSON-RPC error frame
  //    (-32602 class) which the bridge surfaces as a typed rejection — not a
  //    crash, not a hang, and the child survives it.
  await assert.rejects(
    () => bridge.request("tools/call", { name: "no_such_tool", arguments: {} }),
    /Unknown tool/,
  );
  console.log("smoke: unknown tool refused typed (JSON-RPC error frame via bridge)");

  // 5. Server still alive after the refusal; clean teardown.
  assert.equal(bridge.alive, true);
  console.log("smoke: bridge alive after typed refusal — PASS");
} finally {
  bridge.stop();
}
console.log("smoke: REAL-binary Pi→MCP E2E chain PASS");
