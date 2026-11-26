/**
 * Current-architecture floor for the vendored in-product Pi agent.
 *
 * Current usage docs must not repeat the historical “Pi ships no MCP”
 * claim as a present-tense interface rule, must describe the in-product
 * sidecar (not only the external --mcp extension), and the controlled
 * session must keep the no-bypass tool denylist. Historical ADR 0122 may
 * retain the original wording if it also points at the product path.
 *
 * Run with any Node 18+: node --test pi/product-runtime/test/current-architecture-docs.test.mjs
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const read = (relative) => readFileSync(join(root, relative), "utf8");

const currentDocs = [
  "README.md",
  "CONTEXT.md",
  "pi/README.md",
  "pi/exp-rs-spatial.ts",
  "pi/product-runtime/README.md",
  "docs/agent/pi-adapter.md",
  "docs/development/pi-product-agent/README.md",
];

const outdatedCurrentClaims = [
  /ships no MCP/i,
  /intentionally ships no MCP/i,
  /deliberately ships no MCP/i,
  /Pi ships no MCP client/i,
  /Pi 不支持 MCP/,
];

test("current usage docs do not treat “Pi ships no MCP” as a present-tense rule", () => {
  for (const relative of currentDocs) {
    const text = read(relative);
    for (const pattern of outdatedCurrentClaims) {
      assert.equal(
        pattern.test(text),
        false,
        `${relative} still asserts ${pattern} as current fact`,
      );
    }
  }
});

test("current usage docs name the vendored in-product runtime and host proxy", () => {
  const required = {
    "README.md": ["product-runtime", "AgentToolHost"],
    "CONTEXT.md": ["product-runtime", "AgentToolHost"],
    "pi/README.md": ["product-runtime", "AgentToolHost", "sicnu.pi/1"],
    "docs/agent/pi-adapter.md": ["product-runtime", "AgentToolHost"],
    "docs/development/pi-product-agent/README.md": [
      "product-runtime",
      "AgentToolHost",
      "scientific_agent_session",
      "EXCLUDED_TOOLS",
    ],
    "pi/product-runtime/README.md": ["AgentToolHost", "exp-rs --mcp"],
    "pi/exp-rs-spatial.ts": ["product-runtime", "not the GUI tool host"],
  };
  for (const [relative, needles] of Object.entries(required)) {
    const text = read(relative);
    for (const needle of needles) {
      assert.ok(text.includes(needle), `${relative} must mention ${needle}`);
    }
  }
});

test("ADR 0122 keeps the historical MCP claim and points at the product path", () => {
  const text = read("docs/adr/0122-pi-spatial-intelligence-layer.md");
  assert.match(text, /no MCP client support/);
  assert.match(text, /## Subsequent decision/);
  assert.match(text, /historical/);
  assert.match(text, /pi\/product-runtime/);
  assert.match(text, /2026-11-15-vendored-pi-product-agent\.md/);
  assert.match(text, /docs\/development\/pi-product-agent\//);
});

test("the controlled session keeps a no-bypass host tool loadout", () => {
  const source = read("pi/product-runtime/controlled-session.mjs");
  assert.match(source, /export const EXCLUDED_TOOLS = Object\.freeze\(\[/);
  for (const name of [
    "mcp__*", "codemode", "tool_search", "read", "bash", "powershell",
    "edit", "write", "grep", "find", "ls",
  ]) {
    assert.ok(source.includes(`"${name}"`), `EXCLUDED_TOOLS must contain ${name}`);
  }
  assert.match(source, /noTools:\s*"all"/);
  assert.match(source, /excludeTools:\s*\[\.\.\.EXCLUDED_TOOLS\]/);
  assert.match(source, /Pi exposed an unexpected tool loadout/);
  assert.doesNotMatch(source, /exp-rs --mcp/);
  assert.doesNotMatch(source, /createExtensionRuntime\(\)[\s\S]{0,80}extensions:\s*\[[^\]\n]+,/);
});
