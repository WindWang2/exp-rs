/**
 * Bridge anti-drift parity test (whole-repo review F-PI-1/F-PI-2).
 *
 * History: pi/exp-rs-spatial.ts used to carry a second, structurally near
 * identical copy of the JSON-RPC child lifecycle, and fixes drifted between
 * the copies (the startup-deadline cleanup only ever reached
 * mcp_bridge.ts). The Compiler 10.0 consolidation removed the second
 * transport: mcp_bridge.ts is now the ONE implementation (guarded by
 * no_drift.test.mjs, which asserts the shell only imports it), and these
 * tests pin the load-bearing lifecycle constructs on that single
 * implementation. mcp_bridge.ts also has functional coverage in
 * mcp_bridge.test.mjs.
 *
 * Run: node --test pi/test/
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const piDir = join(here, "..");
const bridgeSource = readFileSync(join(piDir, "mcp_bridge.ts"), "utf8");

test("the shared bridge tears the child down on stream desync (F-PI-1)", () => {
  // The overflow branch must route through the shared kill teardown, and
  // the teardown must actually kill the child (not just clear the buffer).
  assert.match(
    bridgeSource,
    /failDesyncedStream\(/,
    "overflow branch must call failDesyncedStream",
  );
  // The kill assertion is scoped to the failDesyncedStream BODY (from the
  // signature to the next method boundary) — an unbounded wildcard would
  // false-pass by matching stop()'s child.kill() instead (review R-B1).
  const teardown = bridgeSource.match(
    /failDesyncedStream\(err: Error\): void \{[^]*?\n  \}\n/,
  );
  assert.ok(teardown, "failDesyncedStream body not found");
  assert.match(
    teardown[0],
    /child\.kill\(\)/,
    "failDesyncedStream must kill the child so lazy respawn replaces it",
  );
  assert.match(
    teardown[0],
    /kill\("SIGKILL"\)/,
    "failDesyncedStream must escalate to SIGKILL (review R-B4)",
  );
  // Exactly one overflow rejection path: one call site plus the shared
  // teardown definition, and no leftover inline buffer-clear-and-return.
  assert.equal(
    (bridgeSource.match(/failDesyncedStream\(/g) ?? []).length,
    2,
    "expected one desync call site plus one definition",
  );
});

test("the shared bridge cancels the startup deadline on every settle path (F-PI-2)", () => {
  assert.match(
    bridgeSource,
    /let cancelStartupDeadline: \(\) => void = \(\) => \{\};/,
    "startup deadline needs a cancellable handle",
  );
  // try { await Promise.race([... startupDeadline]) } finally { cancel } —
  // the success path used to leave the 30s timer armed, pinning the event
  // loop after every spawn/respawn.
  assert.match(
    bridgeSource,
    /try \{[^}]*await Promise\.race\(\[[^]*?startupDeadline,?[^]*?\]\);[^]*?\} finally \{[^]*?cancelStartupDeadline\(\);/,
    "startup deadline must be cancelled in a finally around the init race",
  );
});

test("the shared bridge rejects all pending calls when the child exits", () => {
  assert.match(
    bridgeSource,
    /on\("exit"[^]*?for \(const p of this\.pending\.values\(\)\) \{[^]*?p\.reject\(err\);/,
    "child exit must reject pending calls",
  );
});
