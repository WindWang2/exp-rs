# R14-E Risk Register

| Risk | Likelihood | Impact | Mitigation / status |
|---|---|---|---|
| Qt filter semantics drift across versions changes allEntries() visibility | Low | Medium | Filter pinned by QDIR_FILTER_PROBE.txt + behavior tests (symlink/hidden/nested cases); per-entry guards make correctness filter-independent for hidden/dir/file classes |
| NFC normalization invalidates resume for NFD-keyed checkpoints | Certain (one-time) | Low | Fail-safe direction (fresh re-grade, never wrong grade); release-noted in LEDGER |
| Preflight unknown-broadening changes agent-visible outcomes (ok→requires_ack) for unresolvable inputs | Certain for affected inputs | Medium | Intended fail-closed contract change, pinned in the golden matrix (conscious update per that file's doctrine) |
| Router race needs TSan to prove fixed | Medium | Medium | Smoke test + single-lock design review; race detection belongs to the sanitizer lane (documented) |
| Pair-rule temporal asymmetry (policy gate before unresolved emission) misread as a gap | Low | Low | In-test comment + REVIEW.md explain the deliberate ordering |
| Shared-host build contention with tracks A–D | Realized once | Low | resource_guard + cross-track lock honored; light ctest-only runs documented in TEST_RESULTS.md |
