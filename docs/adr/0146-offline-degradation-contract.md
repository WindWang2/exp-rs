# ADR 0146: Offline Degradation Contract for Transport-Dependent Tests

Status: accepted · Branch `zcode/verification-baseline-green` · Baseline `origin/master@27b9aa0a63`

## Context

The READINESS verification vocabulary deliberately keeps `not-built`, `skipped`,
`timeout`, `failed` and `passed` distinct, but the system had no answer to "what
should a transport-dependent test do when its transport is unavailable?". The
observed failure mode was the worst possible one: a test binary hanging inside
GDAL/curl until the verification ladder's hard wall budget killed it, so the
READINESS recorded `timeout` — indistinguishable, as evidence, from a code
defect, and useless on an offline teaching machine (the machine-room scenario
this platform must serve).

D10's baseline audit sharpened the picture:

* The three "remote" I/O suites (`test_io_range_cache`, `test_io_remote_range`,
  `test_io_remote_validator`) never touch the internet. They prove remote-transport
  contracts against a **loopback HTTP fixture** (`tests/support/http_range_server.cpp`,
  numeric `http://127.0.0.1:<port>` URLs).
* Their `timeout` verdicts were **environmental**: a poisoned proxy environment
  (`http_proxy`/`ALL_PROXY` set without a loopback `no_proxy` exemption) makes
  libcurl route even `127.0.0.1` fixture reads through a proxy — a hang (firewall
  DROP) or typed failures unrelated to the code under test.
* The ladder mapped direct test-binary runs to only three verdicts
  (`passed`/`failed`/`timeout`). There was **no producer of `skipped`** — the
  vocabulary existed but could not be exercised, so a skip could not be
  distinguished from a pass except by convention.

## Decisions

1. **SKIP contract (machine-readable)**: a transport-dependent test binary that
   finds its prerequisites unavailable prints

       sicnu-skip: <reason-code>

   on **stdout** (the machine-readable channel; human detail goes to stderr as
   `sicnu-skip-detail: ...`) and exits with status **77** (the GNU automake
   "skipped test" convention). The verification ladder maps exit 77 — with or
   without a sentinel line, recording whichever reason exists — to the distinct
   `skipped` verdict with the reason carried in its `detail` field. `collect_readiness.py`
   refuses to print a reason-less skip: a skip without a reason is a reporting
   defect, not a verdict.
2. **Frozen reason vocabulary** (extend, never reword):
   * `forced-offline` — `SICNU_FORCE_OFFLINE=1` is set in the environment;
     exercises the skip path itself and makes the offline behaviour of every
     transport-dependent suite locally testable.
   * `loopback-unavailable` — the bounded loopback TCP probe
     (bind → listen → connect → accept, every stage under a `poll()` deadline)
     failed. Sub-reasons are recorded in the detail line (`:bind`, `:connect-timeout`, …).
3. **Proxy hygiene is a fix, not a skip**: the guard appends
   `127.0.0.1,localhost` to `no_proxy`/`NO_PROXY` before any test runs. Loopback
   fixtures never need a proxy; a proxy env that intercepts them is an
   environment defect the platform now neutralises instead of failing on.
   Explicit (non-loopback) traffic keeps its configured proxy.
4. **The probe itself can never be the hang**: every stage of the loopback
   probe is deadline-bounded (2 s poll budgets); the guard fires once per test
   run, at Catch2's `testRunStarting`, so test discovery (`--list-tests`) and
   ctest registration keep working on hosts where the transport is missing.
5. **Local substitute evidence**: because the three "remote" suites already
   exercise the production remote-transport code paths (probe, range cache,
   validators) against a loopback fixture, they ARE the offline-verifiable
   substitute. No second code path is added; what changes is that the suites
   now (a) survive hostile proxy environments, (b) SKIP with a reason when
   loopback TCP itself is impossible, and (c) demonstrate both behaviours on
   every host via `SICNU_FORCE_OFFLINE=1`.
6. **The verdict vocabulary is unchanged.** `not-built`, `skipped`, `timeout`,
   `failed` and `passed` stay distinct. This ADR introduces a *producer* for the
   existing `skipped` verdict and a reason-code channel; it adds no verdict.
7. **Routine benchmarks get a smoke tier**: `--bench-quick` runs both benchmark
   binaries with fixed, small, non-env-overridable iteration counts (declared
   budget: well under 120 s single-threaded) and labels the JSON artifact
   `"tier": "quick"`. Full-tier runs stay opt-in evidence. Benchmarks remain
   evidence, never gates.

## Consequences

* An offline machine room can no longer produce a `timeout` from an
  environmental transport problem: either the transport exists (tests run, with
  proxy hygiene applied), or the tests report `skipped (forced-offline)` /
  `skipped (loopback-unavailable)` and the READINESS explains why.
* Every transport-dependent suite must embed the guard (`SICNU_OFFLINE_GUARD()`
  from `tests/support/offline_probe.h`); a suite that hangs instead of
  skipping is a defect against this ADR.
* `scripts/verification_ladder.py` and `scripts/collect_readiness.py` carry the
  contract on the reporting side; both remain local-only tooling (no CI).
