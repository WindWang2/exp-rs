# DECISIONS & ARCHITECTURAL TRADEOFFS — Direction B

**Date**: 2026-10-13  
**Autonomous Mode**: Full Unattended Execution (AGY / Goal-Loop)  

---

## Decision 1: Protocol Handling for `/teamwork-preview` and `/goal-loop`
- **Context**: The prompt requests activating `/teamwork-preview` and `/goal-loop`. In this agent session environment, slash commands are UI shortcuts not directly dispatched to the shell.
- **Options Considered**:
  1. Emit slash commands to shell (Rejected: shell does not recognize them).
  2. Pretend to execute without actual capability (Rejected: violates honesty and audit guidelines).
  3. Formally mark `NOT_AVAILABLE` in ledger and establish native Antigravity role separation (Scout, Implementer, Reviewer) and strict Oracle loop (`Oracle → Red → Minimum Fix → Green × 2 → Reverse Review`). (Chosen Default).
- **Default Chosen**: Option 3.

---

## Decision 2: Resolving the 2 Unresolved PR #1475 P1 Review Threads Without Collision
- **Context**: PR #1475 has two unresolved P1 review threads:
  1. `CatalogService` 提前限制 `query.maxItems` 时 `truncated` 假阴性；
  2. `fileExists()` 仅正则化存在性检查而 direct `GDALOpen` 仍使用原 URL。
  PR #1475 modified `src/geospatial/fabric/query_planner.cpp` and `src/operators/framework/rs_json_params.cpp`. We must NOT modify #1475's files directly to prevent upstream cherry-pick/merge conflicts.
- **Options Considered**:
  1. Edit #1475's branch directly (Rejected: violates git isolation and non-interference rules).
  2. Edit #1475's modified files in our worktree (Rejected: causes severe merge conflicts when #1475 is merged).
  3. Solve both problems at the domain authority layers owned by Direction B:
     - Solve Catalog truncation and infinite pagination in `src/geospatial/fabric/catalog_service.cpp` (owned by Direction B);
     - Solve `file://` and URI decoding inside `src/geospatial/util/resource_uri.cpp` and `src/geospatial/raster/raster_reader.cpp` / `canonical_metadata.cpp` (owned by Direction B).
- **Default Chosen**: Option 3. This resolves the true root causes cleanly while ensuring 0 file conflicts with PR #1475.

---

## Decision 3: Host Compilation Resource Governance
- **Context**: Multiple agents and worktrees (`exp-rs-r11-a-science`, `exp-rs-r11-c-agent`, `exp-rs-r11-b-geospatial`) share a single Linux machine. Heavy concurrent builds risk compiler contention and out-of-memory crashes.
- **Options Considered**:
  1. Build everything with unconstrained parallelism `-j$(nproc)` (Rejected: violates hard constraint and risks crashing the host).
  2. Rely only on `-j2` per process without inter-process coordination (Risky: 3 worktrees × 2 jobs = 6 concurrent jobs, exceeding host budget).
  3. Use an inter-process advisory lock `flock /home/kevin/projects/rs-studio/.build_lease.lock` combined with Ninja `-j2` and targeted single-test executable builds (e.g. `test_io_fabric_catalog`).
- **Default Chosen**: Option 3. Guarantees global host build safety and lightning-fast builds (< 30s per target).
