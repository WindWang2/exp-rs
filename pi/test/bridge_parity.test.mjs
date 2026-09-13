/**
 * Bridge anti-drift parity test (whole-repo review F-PI-1/F-PI-2).
 *
 * pi/mcp_bridge.ts and pi/exp-rs-spatial.ts carry two structurally near
 * identical copies of the JSON-RPC child lifecycle. History shows a fix
 * landing on one copy and not the other (the startup-deadline cleanup only
 * ever reached mcp_bridge.ts), so this test pins the load-bearing lifecycle
 * constructs in BOTH files. exp-rs-spatial.ts cannot be imported outside the
 * Pi extension host (it imports @earendil-works/pi-coding-agent), so its
 * assertions are structural by necessity; mcp_bridge.ts additionally has
 * functional coverage in mcp_bridge.test.mjs.
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
const sources = {
  "mcp_bridge.ts": readFileSync(join(piDir, "mcp_bridge.ts"), "utf8"),
  "exp-rs-spatial.ts": readFileSync(join(piDir, "exp-rs-spatial.ts"), "utf8"),
};

test("both bridges tear the child down on stream desync (F-PI-1)", () => {
  for (const [file, src] of Object.entries(sources)) {
    // The overflow branch must route through the shared kill teardown, and
    // the teardown must actually kill the child (not just clear the buffer).
    assert.match(
      src,
      /failDesyncedStream\(/,
      `${file}: overflow branch must call failDesyncedStream`,
    );
    // The kill assertion is scoped to the failDesyncedStream BODY (from the
    // signature to the next method boundary) — an unbounded wildcard would
    // false-pass by matching stop()'s child.kill() instead (review R-B1).
    const teardown = src.match(
      /failDesyncedStream\(err: Error\): void \{[^]*?\n  \}\n/,
    );
    assert.ok(teardown, `${file}: failDesyncedStream body not found`);
    assert.match(
      teardown[0],
      /child\.kill\(\)/,
      `${file}: failDesyncedStream must kill the child so lazy respawn replaces it`,
    );
    assert.match(
      teardown[0],
      /kill\("SIGKILL"\)/,
      `${file}: failDesyncedStream must escalate to SIGKILL (review R-B4)`,
    );
    // Exactly one overflow rejection path: one call site plus the shared
    // teardown definition, and no leftover inline buffer-clear-and-return.
    assert.equal(
      (src.match(/failDesyncedStream\(/g) ?? []).length,
      2,
      `${file}: expected one desync call site plus one definition`,
    );

  }
});

test("both bridges cancel the startup deadline on every settle path (F-PI-2)", () => {
  for (const [file, src] of Object.entries(sources)) {
    assert.match(
      src,
      /let cancelStartupDeadline: \(\) => void = \(\) => \{\};/,
      `${file}: startup deadline needs a cancellable handle`,
    );
    // try { await Promise.race([... startupDeadline]) } finally { cancel } —
    // the success path used to leave the 30s timer armed, pinning the event
    // loop after every spawn/respawn.
    assert.match(
      src,
      /try \{[^}]*await Promise\.race\(\[[^]*?startupDeadline,?[^]*?\]\);[^]*?\} finally \{[^]*?cancelStartupDeadline\(\);/,
      `${file}: startup deadline must be cancelled in a finally around the init race`,
    );
  }
});

test("both bridges reject all pending calls when the child exits", () => {
  for (const [file, src] of Object.entries(sources)) {
    assert.match(
      src,
      /on\("exit"[^]*?for \(const p of this\.pending\.values\(\)\) \{[^]*?p\.reject\(err\);/,
      `${file}: child exit must reject pending calls`,
    );
  }
});
