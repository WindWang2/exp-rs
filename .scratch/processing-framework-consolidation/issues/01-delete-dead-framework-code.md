# 01 — Delete Dead Framework Code

**What to build:** Remove uncalled framework utility classes (`ErrorReporter`, `ProcessingCache`, `ProgressCallback` virtual base class, and `SimpleProgressCallback`) and their unit tests from `src/processing/framework/` and `tests/`.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Remove `error_reporter.h` and `error_reporter.cpp` from `src/processing/framework/` and `CMakeLists.txt`.
- [ ] Remove `processing_cache.h` and `processing_cache.cpp` from `src/processing/framework/` and `CMakeLists.txt`.
- [ ] Remove `progress_callback.h` and `progress_callback.cpp` from `src/processing/framework/` and `CMakeLists.txt`.
- [ ] Remove corresponding unit tests for `ProcessingCache` and `ErrorReporter` from `tests/`.
- [ ] Project builds cleanly and full test suite passes.
