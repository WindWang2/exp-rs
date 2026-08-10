# Autonomous Platform Hardening Plan

## Goal
Full autonomous hardening of `exp-rs` platform architecture, CI, cross-platform build, runtime contracts, and release packaging quality convergence on branch `agent/20260809-rs-platform-hardening`.

## Phases & Deliverables

### Phase 1: Production Readiness Audit
- Audit CMakeLists.txt, cmake/, packaging/, scripts/, tests/
- Check GitHub CI status and workflow existence/gaps
- Audit TaskCenter, ProcessingRegistry, RSOperator, Python worker, Agent/MCP
- Audit cross-platform compatibility, static analysis, sanitizers
- Construct complete matrix in `ci-matrix.md` and report findings in `findings.md` and `architecture-findings.md`

### Phase 2: Layered CI Pipeline Implementation & Reinforcement
- Establish or reinforce GitHub Actions workflows (.github/workflows/)
- Tier 1 — fast required: configure, compile core/tests, unit tests, contract tests, static checks
- Tier 2 — full Linux integration: Qt, GDAL, QGIS dependencies, full ctest, Agent tests, probes
- Tier 3 — platform verification: Windows & macOS build verification lanes
- Ensure NO `continue-on-error` or silent test swallows on required lanes

### Phase 3: CMake Profiles & Option Toggles Clean-up
- Standardize build profiles / presets (developer, ci-fast, ci-full, sanitizer, release)
- Audit feature toggles (`ENABLE_PYTHON_WORKER`, `ENABLE_AGENT_TESTS`, `ENABLE_PROBES`, `ENABLE_SANITIZERS`, `ENABLE_STATIC_ANALYSIS`)
- Fix option combinations so disabled features do not link or register dead modules

### Phase 4: Python Worker / Optional Runtime Hardening
- Audit Python worker detection and process isolation
- Verify worker crash/timeout/error propagation and cleanup (process, shared memory, temp files)
- Add automated Python worker capability detection and smoke test lane in CI/test baseline

### Phase 5: Runtime Contracts & Concurrency Hardening
- TaskCenter: cancellation timing, detached thread cleanup, worker thread lifecycle
- GUI Bridge & Callback lifetimes: parent-child UI ownership, signal/slot thread affinity, race guards
- Processing Registry & RSOperator: error propagation, provenance integrity, resource cleanup

### Phase 6: Architecture Hardening (Deep Modules / Narrow Interfaces)
- De-duplicate orchestration logic across processing / UI boundaries
- Fix hidden global state, unbounded queues, and sync I/O on main UI thread
- Lock contracts with automated regression tests

### Phase 7: Cross-Platform Compatibility Fixes
- Fix Windows `min`/`max` header macro pollution (`NOMINMAX`)
- Path separators, UTF-8 path handling, temp directory abstraction
- Replace hardcoded POSIX APIs (`unistd.h`, `dlopen`, signals) with Qt/C++ std portable equivalents
- MSVC compiler flags and warning fixes

### Phase 8: Static Analysis & Sanitizer Integration
- Implement ASan / UBSan build profile
- Strict compiler warning flags (-Wall -Wextra -Werror=return-type or MSVC equivalents)
- Selected high-value clang-tidy checks

### Phase 9: Release & Packaging Verification
- Audit CMake `install()` rules, packaging scripts, runtime resource bundles
- Verify out-of-tree / installed package execution (startup smoke test)
- Sync version metadata and CHANGELOG

### Phase 10: Independent Swarm Review & Remediation
- Multi-perspective review (Architecture, C++/Qt Lifetime, CI/Build, Cross-platform, Security)
- Resolve all Critical/High findings

### Phase 11: Final Verification & PR Creation
- Clean build & 100% test pass verification
- `git diff --check`
- Push branch `agent/20260809-rs-platform-hardening` to `origin`
- Create PR to `master` with full PR body summary
