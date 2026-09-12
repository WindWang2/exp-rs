# Plugin Platform 9.0 — Review log (adversarial)

Review process per milestone: self-review after each suite goes green; two
read-only subagent reviews (architecture/correctness/concurrency/scientific
validity/security; tests/performance/portability/resource-bounds/docs-vs-
code) before final PR. Findings get P0–P3, dispositions, and re-verification
notes. This file is appended to as reviews complete.

## Cross-lane baseline fixes carried on this branch

- `src/geospatial/metadata/canonical_metadata.cpp` (NOT plugin ownership):
  master failed to compile on GDAL 3.13.3 — `GDALMDArrayRead` takes
  `arrayStartIdx` as `const GUInt64*` but `count` as `const size_t*`; the
  code passed a `const GUInt64` for count. One-token fix (`size_t oneCount`)
  + comment. Same class as merged CI fix #834 ("bridge range_cache VSI APIs
  across GDAL 3.8–3.13"). Blocks every fat test lane on this host, hence
  carried here; minimal diff, no behavior change on GDAL versions where the
  old code compiled (size_t == GUInt64-width arithmetic is unaffected at
  the value 1).

## Self-review findings (running log)

- Fixed during development: statusCode() collapsed E6003 into E6002 (now
  reports the real code); the recv-cap/send-cap tests initially raced the
  reader (poison frame written before the request registered) — made
  deterministic by holding the request in flight; jsoncpp value-semantics
  copy trap in the a11y schema test; fixture event vocabulary had to match
  the host renderer (clicked/changed/command), not a guessed whitelist.

## Subagent review A — architecture/correctness/concurrency/security

Verdict: P0 x1, P1 x2, P2 x4, P3 x7. Dispositions:

- **A-P0-1 (P0) directional frame caps defeated by the shared fallback.**
  FIXED: loadPlugin now builds limits AFTER the handshake and only for the
  negotiated peer shape (1.2 peers get ONLY maxRequestBytes/maxResponseBytes;
  1.1 peers get ONLY the shared maxFrameBytes); the worker treats
  maxFrameBytes as fallback-only when any directional bound is present; the
  limits builder is a shared helper re-applied on respawn against the fresh
  worker's own features. End-to-end evidence: new suite
  "protocol 1.2 directional caps survive the negotiated path end-to-end"
  (small request quota + legal response quota; ~1 MiB response flows; the
  old code E6003-killed the channel here).
- **A-P1-1 (P1) provider scheme gate bypassed by authority-less/case
  variants.** FIXED: RFC 3986 scheme extraction (ALPHA *(ALPHA/DIGIT/+/-/.)
  ":"), case-insensitive comparison; only scheme-less paths stay ungated
  (documented boundary).
- **A-P1-2 (P1) redaction gaps.** FIXED: ANY scalar under a secret-like key
  is redacted (not just strings); vocabulary extended with
  authorization/bearer/cookie (signature deliberately excluded — the
  package.signature integrity metadata is not a secret); the honest
  boundary (no value-level scanning under innocent keys) is documented in
  the header.
- **A-P2-1 (P2) lock order mMutex -> registry in registerModelRuntime.**
  FIXED: the registry lookup (via the new copy accessor) happens BEFORE
  mMutex.
- **A-P2-2 (P2) raw PluginRecord* held across dereferences.** FIXED: new
  PluginRegistry::accessDeclarationFor()/pluginDirectoryFor() copy
  accessors (taken under the registry lock); all five gate sites use them.
- **A-P2-3 (P2) send-side cap refusal reported E6002.** FIXED:
  sendEnvelope propagates the frame writer's typed code; an oversized
  local send now fails with E6003.
- **A-P2-4 (P2) matrix row missing for the provider scheme gate.** FIXED:
  "dataProviders[].schemes" enforced-worker row added (with discover/
  plain-path caveats).
- A-P3 items: uri type-check before cast (FIXED, typed E6002 refusal);
  loadPlugin rollback comment (FIXED — documents the registry caller's
  revoke-on-failure); runBounded detach-on-timeout (ACCEPTED, pre-existing
  8.0 pattern, bounded by the target's own ladder); applyPins hostile
  entry guard (FIXED — non-string id/version entries are skipped);
  mRetiredGroups unbounded growth (FIXED — capped at the 64 most recent);
  boundedNumber dead helper (REMOVED); doc double blank lines (FIXED).

## Subagent review B — tests/performance/portability/docs

Verdict: P1 x3, P2 x9, P3 x12. Dispositions:

- **B1/A-P0-1 (P1, same finding).** FIXED — see above.
- **C1 (P1) ::getpid() breaks MSVC.** FIXED: _WIN32 guard with
  GetCurrentProcessId (the documented repo idiom).
- **D1 (P1) "load gate enforces dependencies" claim false.** FIXED
  honestly: the diagnostic and packaging.md now say ADVISORY verbatim
  ("NOTHING enforces this at load time"); implementing a full load-time
  resolver was rejected as a behavior change beyond 9.0's probe design.
- **A1 (P2) round-trip test could not see projection drops.** FIXED: the
  test now walks the ORIGINAL hand-written document and requires every
  leaf path to survive into the projection — and immediately caught TWO
  real issues: the test's own wrong key names (determinism,
  working_directory_param) and a GENUINE toJson defect (the python section
  was dropped whenever entrypoint_kind != python although the parser reads
  it unconditionally — fixed in plugin_manifest.cpp). The jsoncpp
  insertion-order comment was corrected (members are key-sorted).
- **A2 (P2) staging sweep unverified.** FIXED: the test plants a DEAD
  process's leftover (pid+1, 48 h) and asserts removal.
- **A3 (P2) no e2e refusal coverage for the capability gates.** FIXED:
  three new integration tests — ui:false refuses describe AND invoke
  (E5005, worker stays alive); a model framework outside the declared list
  fails the whole load typed E5005 with nothing half-registered; plus the
  P0 negotiated-caps evidence test.
- **B2 (P2) PERFORMANCE.md validateUiEvent bound false.** FIXED both ways:
  string fast path rejects oversized values before serialization; the doc
  now states the real bound (work is O(value size); the cap bounds what is
  accepted).
- **C2 (P2) env byte quotas truncate on MSVC (fail-open).** FIXED:
  clampedBytes() clamps env values into [1 KiB, 1 GiB] before the long
  cast, for both byte quotas.
- **C3 (P2) crafted range can throw std::out_of_range out of install().**
  FIXED: parseVersion parts are length-capped (<= 9 digits) and fail
  closed.
- **C4 (P3) caret 0.0.x semantics.** FIXED to npm semantics (^0.0.3 admits
  only 0.0.3) with test coverage.
- **C5 (P3) scheme comparison case-sensitive.** FIXED (folded into the
  A-P1-1 rewrite).
- **D2 (P2) ARCHITECTURE D5 wrong whitelist + error class.** FIXED (record
  states the shipped vocabulary and E6010).
- **D3 (P2) ARCHITECTURE D6 describes unbuilt behaviors.** FIXED (record
  states what shipped: sweep-verified interrupted installs; advisory
  dependency probe).
- **D4 (P2) doctor health surface.** PARTIALLY DELIVERED + record
  corrected: doctor now quotes the enforcement matrix and the live health
  snapshot; the snapshot gained workerPid and droppedEvents; the full
  support surface (retired trail, redacted diagnostics) is documented as
  living in debug-bundle.
- **D5/D6 (P3) kitchen-sink name, rendezvous wording.** FIXED (records
  state what shipped).
- **D7 (P3) TEST_MATRIX inconsistencies.** FIXED: R0 recorded honestly as
  NOT RUN with the reasoning; final regression table added.
- **D8 (P3) matrix "generated FROM" wording.** PARTIALLY ACCEPTED: the
  matrix remains the single machine-readable source the CLI surfaces quote;
  the human doc paraphrases it — wording softened in capabilities.md 9.0
  section ("one source of truth quoted by the CLI surfaces").
- **D9 (P3)** FIXED with A1.
- **D10 (P3) directional-caps zero comment.** FIXED ("leave unchanged").
- **D11 (P3) CLI pin-syntax untested/--json accepted.** PARTIALLY ACCEPTED:
  --json is consumed by the CLI framework contract; usage string documents
  the --pins=id=version form; CLI-level index tests deferred (the SDK
  index suite covers the logic; noted as follow-up).

## Re-verification after remediation (2026-09-12)

All 11 suites re-run green post-fixes: ipc 96/20, capabilities 112/13,
ui_schema 25/8, manifest 277/7, plugin_system 96/11, loader 26/4,
host_process 218/23, runtime_host 48/5, barrier 23/6, ui_schema_host 18/2,
cli_json 33/6 — plus the conformance kit end-to-end 19 checks (18 pass /
1 skipped, verdict ok) and 3x repeated ipc runs stable. The two
poison-frame tests were made deterministic (request in flight before the
poison is written) after the remediation shifted reader timing.
