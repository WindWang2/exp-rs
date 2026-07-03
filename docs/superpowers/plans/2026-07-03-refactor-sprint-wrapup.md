# P0–P5 Refactor Sprint Wrap-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the completed P0–P5 refactor (dialog async unification + `main_window` modular split) as a single verified atomic commit with updated developer docs.

**Architecture:** All implementation code already exists in the working tree. This plan covers verification, doc updates, selective staging (excluding unrelated vendor/UI assets), and one atomic commit per `docs/superpowers/specs/2026-07-03-refactor-sprint-design.md`.

**Tech Stack:** C++20, Qt6, CMake, Catch2, QGIS vendored core/gui, offscreen `ctest`

## Global Constraints

- Test gate: **449/449** pass with `QT_QPA_PLATFORM=offscreen ctest`
- Commit strategy: **single atomic commit** for all refactor source + doc updates
- Do **not** stage: `otb_ref/`, `boost_ref/`, `vendor/`, unrelated `UI/svg-icons/` digitize icons, `.claude/worktrees/`
- Spec reference: `docs/superpowers/specs/2026-07-03-refactor-sprint-design.md` (already committed as `cc7ce8e13`)

---

## File Inventory (already implemented)

### New files

| Path | Purpose |
|------|---------|
| `src/agent/stac_client.{h,cpp}` | STAC HTTP client |
| `src/app/main_window_menus.cpp` | Menu / toolbar / status bar |
| `src/app/main_window_docks.cpp` | Dock widgets |
| `src/app/main_window_connections.cpp` | Signal wiring + canvas callbacks |
| `src/app/main_window_vector.cpp` | Vector editing |
| `src/app/main_window_project.cpp` | Project I/O |
| `src/app/main_window_layers.cpp` | Layer management + identify |
| `src/app/main_window_view.cpp` | Map navigation |
| `src/app/main_window_misc.cpp` | Settings / help / layout persistence |
| `tests/test_raster_processing_dialog_base.cpp` | Dialog base lifecycle tests |

### Modified (core)

- `src/app/dialogs/raster_processing_dialog_base.{h,cpp}` — `runGdalTask`, `runAlgorithmTask`, `cleanupRunResources`
- `src/app/dialogs/*_dialog.{h,cpp}` — migrated to base async (see spec §3.2)
- `src/app/main_window.cpp` — 238 lines (bootstrap only)
- `src/app/CMakeLists.txt` — new translation units
- `src/processing/algorithms/{band_math,atmospheric_correction,image_enhancement,image_fusion}.{h,cpp}`
- `src/agent/mcp_server.{h,cpp}` — cooperative stop
- `tests/CMakeLists.txt`, `tests/test_{band_math,atmospheric,pca,image_fusion,stac_client}.cpp`
- `CMakeLists.txt`, `README.md` (partial — test count still needs update)

---

### Task 1: Verify build and test gate

**Files:**
- Verify: entire tree (no edits)

**Interfaces:**
- Produces: confirmed 449/449 test pass before commit

- [ ] **Step 1: Clean rebuild**

```bash
cd /home/kevin/projects/exp-rs/build
cmake .. -DENABLE_TESTS=ON
make -j$(nproc)
```

Expected: `sicnu_geo_rs` and all test targets link without error.

- [ ] **Step 2: Run full test suite**

```bash
cd /home/kevin/projects/exp-rs/build
QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 449`

- [ ] **Step 3: Record evidence**

Save last 3 lines of ctest output in PR description or commit body footnote.

---

### Task 2: Update README test count

**Files:**
- Modify: `README.md:76`

**Interfaces:**
- Consumes: Task 1 verified count (449)

- [ ] **Step 1: Edit README**

Replace:

```markdown
**443 tests** covering core algorithms, GDAL utilities, dialog UI, and processing framework.
```

With:

```markdown
**449 tests** covering core algorithms, GDAL utilities, dialog UI, and processing framework.
```

- [ ] **Step 2: Grep for stale counts**

```bash
rg "443 tests" README.md CLAUDE.md
```

Expected: no matches after edit.

---

### Task 3: Update CLAUDE.md architecture section

**Files:**
- Modify: `CLAUDE.md` (after `## Codebase Architecture` bullet list)

**Interfaces:**
- Produces: module routing table for `src/app/`

- [ ] **Step 1: Add `src/app/` module table**

Insert after the existing architecture bullets (before `## Language`):

```markdown
*   `src/app/`: Application shell — see [P0–P5 refactor spec](docs/superpowers/specs/2026-07-03-refactor-sprint-design.md) for module map:
    *   `main_window.cpp` — constructor, `setupUi`, `setupMapCanvas`
    *   `main_window_menus.cpp` — menu / toolbar / status bar
    *   `main_window_docks.cpp` — dock panels
    *   `main_window_connections.cpp` — signals, canvas state, layer tree events
    *   `main_window_view.cpp` — zoom, pan, measure, Georeferencer
    *   `main_window_vector.cpp` — vector editing
    *   `main_window_project.cpp` — project I/O, STAC browser
    *   `main_window_layers.cpp` — layers, identify results
    *   `main_window_misc.cpp` — preferences, help, panel layout
    *   `main_window_processing.cpp` — RS processing dialog slots
    *   `dialogs/raster_processing_dialog_base.{h,cpp}` — shared async dialog lifecycle
```

- [ ] **Step 2: Update agent bullet**

Change:

```markdown
*   `src/agent/`: AI Agent infrastructure — MCP server, STAC client.
```

(Ensure STAC client is mentioned — already present; no change if already correct.)

---

### Task 4: Stage refactor files only

**Files:**
- Stage: see explicit list below
- Exclude: `otb_ref/`, `boost_ref/`, `vendor/`, `UI/svg-icons/mAction*.svg`, `docs/superpowers/plans/2026-06-06-phase-10b-obia-plan.md`, unrelated cmake vendor scripts unless part of `SICNU_EMBED_PYTHON` switch

**Interfaces:**
- Consumes: Tasks 1–3 complete

- [ ] **Step 1: Stage source and tests**

```bash
cd /home/kevin/projects/exp-rs

git add \
  CMakeLists.txt \
  README.md \
  CLAUDE.md \
  src/agent/mcp_server.cpp \
  src/agent/mcp_server.h \
  src/agent/stac_client.cpp \
  src/agent/stac_client.h \
  src/app/CMakeLists.txt \
  src/app/main_window.cpp \
  src/app/main_window.h \
  src/app/main_window_connections.cpp \
  src/app/main_window_docks.cpp \
  src/app/main_window_layers.cpp \
  src/app/main_window_menus.cpp \
  src/app/main_window_misc.cpp \
  src/app/main_window_project.cpp \
  src/app/main_window_vector.cpp \
  src/app/main_window_view.cpp \
  src/app/dialogs/ \
  src/processing/algorithms/atmospheric_correction.cpp \
  src/processing/algorithms/atmospheric_correction.h \
  src/processing/algorithms/band_math.cpp \
  src/processing/algorithms/band_math.h \
  src/processing/algorithms/image_enhancement.cpp \
  src/processing/algorithms/image_enhancement.h \
  src/processing/algorithms/image_fusion.cpp \
  src/processing/algorithms/image_fusion.h \
  tests/CMakeLists.txt \
  tests/test_atmospheric.cpp \
  tests/test_band_math.cpp \
  tests/test_image_fusion.cpp \
  tests/test_pca.cpp \
  tests/test_stac_client.cpp \
  tests/test_raster_processing_dialog_base.cpp
```

- [ ] **Step 2: Verify staged diff excludes vendor trees**

```bash
git diff --cached --stat | rg "otb_ref|boost_ref|vendor/"
```

Expected: no output.

- [ ] **Step 3: Review staged file count**

```bash
git diff --cached --stat | tail -1
```

Expected: ~50 files changed (dialogs + main_window modules + algorithms + tests).

---

### Task 5: Atomic commit

**Files:**
- Commit: staged changes from Task 4

**Interfaces:**
- Consumes: Task 1 test evidence, Tasks 2–4 doc/staging

- [ ] **Step 1: Commit with spec message**

```bash
git commit -m "$(cat <<'EOF'
refactor: unify dialog async lifecycle and modularize main_window

- RasterProcessingDialogBase: runGdalTask, runAlgorithmTask, run lifecycle
- Migrate all RS dialogs to base-owned async runners
- Extract StacClient; add ImageFusion::processNativeFusion
- Split main_window into 10 focused translation units (~238-line core)
- Fix spectral_index temp band lifetime bug
- 449/449 tests pass (offscreen)

Spec: docs/superpowers/specs/2026-07-03-refactor-sprint-design.md
EOF
)"
```

- [ ] **Step 2: Verify clean refactor staging**

```bash
git log -1 --stat
git status --short
```

Expected: one commit on `master`; remaining unstaged files are explicitly out-of-scope per spec §1.3.

---

## Self-Review (plan vs spec)

| Spec section | Task |
|--------------|------|
| §1 Success criteria (449 tests) | Task 1 |
| §1 README/CLAUDE pointers | Tasks 2–3 |
| §2 P0–P5 retrospective (code) | Task 4 staging list |
| §5 Wrap-up checklist | Tasks 1–5 |
| §1.3 Non-goals (exclude vendor) | Task 4 Step 2 |

No placeholders. All commands are exact.

---

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-07-03-refactor-sprint-wrapup.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks
2. **Inline Execution** — run tasks in this session via executing-plans with checkpoints

Which approach?