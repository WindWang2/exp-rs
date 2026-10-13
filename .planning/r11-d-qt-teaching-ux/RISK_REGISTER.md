# Risk Register — Direction D

## Managed Risks

| ID | Risk | Severity | Mitigation Strategy |
|---|---|---|---|
| **RSK-01** | Git conflict with open PR #1478 upon merge to master | Medium | Modify non-overlapping functions or build clean extensions; maintain semantic parity; review file overlap before commit. |
| **RSK-02** | Heavy build concurrency OOM on host system | High | Strictly enforce `-j2` build limit (`CMAKE_BUILD_PARALLEL_LEVEL=2`), avoid building all 15k repo files, build only targeted test binaries. |
| **RSK-03** | Silent failure concealment in UI | High | Replace empty `return;` statements in UI actions with user-visible `statusBar()->showMessage(...)` or explicit error dialogs. |
| **RSK-04** | Large table memory exhaustion in Experiment Studio | Medium | Implement batch item populating or disable sorting/updates during reload, or use virtual model representation. |
| **RSK-05** | Terminology provider id collision breaks downstream lookups | High | Ensure unique descriptor ID assignment and bidirectional related terms resolution across duplicate english strings. |
| **RSK-06** | File truncation on failed pipeline definition saves | Medium | Use atomic file writing pattern (write to `.tmp` file, flush, fsync, atomic rename). |
