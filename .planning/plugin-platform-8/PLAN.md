# Plugin Platform 8.0 — Plan

Milestones (implementation order; each ends with a targeted build + targeted tests):

- M0 Baseline repairs: jsoncpp Int64 ambiguity in test_plugin_capabilities (master build break on
  jsoncpp without long-long overloads), `.exe`-suffixed worker path in tests/CMakeLists.txt
  (Linux/macOS test breakage), model-runtime proxy constructor locking, docs/plugins/capabilities.md
  drift fix.
- M1 Protocol v1.1 concurrency (A+B): worker dispatch pool, per-request cancel routing, host-side
  FIFO concurrency gate = maxRequestConcurrency (exact), poison-on-timeout with deferred kill,
  frame-cap negotiation, progress coalescing + event queue bounds, proxies unlocked-request fix.
- M2 Isolation hardening (C): POSIX process-group kill + pre-exec RLIMIT_AS, enforcement matrix
  docs, orphan-grandchild test (Linux).
- M3 Capability 2.0 (D): worker-side workDir containment vs declared roots, consistency
  validation warnings, default-deny tests.
- M4 Declarative UI (E): plugin_ui_schema.{h,cpp} (Qt-free model + validator), worker probe +
  ui.describe/ui.invoke, host renderer + registration seams, revocation; fixture provider + tests.
- M5 Lifecycle + conformance kit (F+H): PT_CANCEL/PT_CONCURRENCY/PT_UI_SCHEMA/PT_RESTART,
  reload-while-executing tests.
- M6 Packaging (G): package metadata (checksums/sbom/signature), staged install with rollback.
- M7 Docs/DX (I) + adversarial review + PR.

Verification per milestone: build affected targets only (bounded -j3/-j4 under host contention),
run targeted test binaries with -j1, record evidence in TEST_MATRIX.md / PERFORMANCE.md.
