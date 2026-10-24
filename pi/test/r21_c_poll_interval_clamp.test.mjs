/**
 * R21-C F4: the wait tool's poll_interval_ms is MODEL-controlled, and the
 * shell used to hand it to setTimeout unclamped — 0 or negative values
 * polled get_execution_status at event-loop speed for the whole timeout
 * window (and the `Date.now() + interval > deadline` early-exit can never
 * fire with interval <= 0). The clamp lives in mcp_bridge.ts beside
 * piToolName/toolCategory so the shell imports it and this test pins it
 * (single source, no regex shadow).
 *
 * Run: node --test "pi/test/r21_c_poll_interval_clamp.test.mjs"
 */
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import {
  clampPollIntervalMs,
  POLL_INTERVAL_CEILING_MS,
  POLL_INTERVAL_FLOOR_MS,
} from "../mcp_bridge.ts";

const here = dirname(fileURLToPath(import.meta.url));
const shellSource = readFileSync(join(here, "..", "exp-rs-spatial.ts"), "utf8");

test("the clamp floors, ceilings and defaults every model-supplied interval", () => {
  assert.equal(clampPollIntervalMs(undefined), 1_000);
  assert.equal(clampPollIntervalMs(0), POLL_INTERVAL_FLOOR_MS);
  assert.equal(clampPollIntervalMs(-5), POLL_INTERVAL_FLOOR_MS);
  assert.equal(clampPollIntervalMs(Number.NaN), 1_000);
  assert.equal(clampPollIntervalMs(Number.POSITIVE_INFINITY), 1_000);
  assert.equal(clampPollIntervalMs(50), POLL_INTERVAL_FLOOR_MS);
  assert.equal(clampPollIntervalMs(250), 250);
  assert.equal(clampPollIntervalMs(2_000), 2_000);
  assert.equal(clampPollIntervalMs(120_000), POLL_INTERVAL_CEILING_MS);
  assert.equal(clampPollIntervalMs(123.9), 123);
});

test("the shell routes poll_interval_ms through the clamp, never the raw value", () => {
  assert.match(
    shellSource,
    /clampPollIntervalMs\(\s*params\.poll_interval_ms\s*\)/,
    "exp-rs-spatial.ts must clamp the model-supplied interval",
  );
  assert.doesNotMatch(
    shellSource,
    /const\s+interval\s*=\s*params\.poll_interval_ms\s*\?\?/,
    "the unclamped `params.poll_interval_ms ?? …` spelling must not return",
  );
});
