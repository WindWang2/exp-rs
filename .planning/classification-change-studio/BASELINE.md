# BASELINE — D15 Classification & Change Detection Studio

## Git Baseline

- **Branch**: `zcode/classification-change-studio` (created from `origin/master`)
- **Baseline commit**: `007e70cff6f43151aef6cf7e501c14bcb94a5090`
  ("fix(docs): arbitrate ADR 0146 collisions and complete .planning whitelist (#985)")
- **Baseline date**: 2026-09-14 11:22:46 +0800
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-classification-change-studio`
  (isolated; `master` checkout at `/home/kevin/projects/rs-studio/main` is never
  modified or built in)
- **Working tree state at branch point**: clean (no uncommitted changes)

## Environment Baseline

| Item | Value |
|---|---|
| OS | Linux 6.18.49-2-lts x64 |
| CPUs / RAM | 16 logical / 62 GiB physical |
| Compiler | `/usr/bin/c++` (gcc), system toolchain |
| CMake | `/usr/bin/cmake` (⚠ `~/.local/bin/cmake` is a broken shim that launches an unrelated app — always use the absolute path) |
| Build generator | Ninja (`/usr/bin/ninja`), preset `dev-default` (Debug, ENABLE_TESTS=ON) |
| Qt | Qt6 system (`/usr/lib/cmake/Qt6`), offscreen platform for tests |
| Deps | GDAL (`/usr/lib/cmake/gdal`), OpenCV5 (`/usr/lib/cmake/opencv5`), Catch2 via FetchContent |
| Test harness | `sicnu_add_test()` helper → Catch2 + `catch_discover_tests(PRE_TEST)`; `QT_QPA_PLATFORM=offscreen ctest -j1` |

## Build & Test Envelope (Hardware Guardrails)

- `CMAKE_BUILD_PARALLEL_LEVEL=2`, build with `ninja -j2` (hard cap; **no** `-j$(nproc)`)
- Downgrade to `-j1` when host RSS > 70%
- `CTEST_PARALLEL_LEVEL=1`, ctest `-j1 --output-on-failure`
- `QT_QPA_PLATFORM=offscreen` for every test invocation
- Remote CI: **none** (`ci=none`). No push, no GitHub Actions. Local green is the only gate.

## Pre-existing Prior Art (Read-Only Reference — do not modify)

The repo already contains adjacent capabilities from earlier tracks. D15 builds
**new, self-contained public seams** beside them (no edits to existing algorithm
files), so refactoring risk to other tracks stays zero:

- `src/analysis/classification/*` — Phase-10A classification stack (OpenCV
  backends: RF/SVM/KMeans/ISODATA/NormalBayes; `RsAccuracyAssessment`,
  `RsJmSeparability`, `RsFloodFill`, `RsClassificationSplit`).
- `src/processing/algorithms/change_detection.{h,cpp}` — `namespace ChangeDetection`
  kernel-level diff / ND / ratio / mask / stats.
- `src/processing/algorithms/post_classification.{h,cpp}` — `namespace TransitionMatrix`.
- `tests/synthetic_raster_builder.h` — `sicnu::testing::RsSyntheticRasterBuilder`
  fluent GDAL raster factory (used by Package I).
- `src/agent/output_verifier.h` — teaching grader `gradeForTeaching()` (ADR 0150),
  rules under `data/labs/grading/*.rules.json` (`landcover_classify`, `change_detect`).
- Lab specs `data/labs/lab03_classification.lab.json`, `lab04_change_detection.lab.json`,
  `lab11_obia_classification.lab.json` already exist — Package I validates their
  grading contracts end-to-end rather than re-authoring them from scratch.
- `.gitignore` whitelists `.planning/<track>/*.md` per track; this track's whitelist
  entry is added with the Phase-0 commit.

## Verification Commands (canonical)

```bash
cd /home/kevin/projects/rs-studio/exp-rs-classification-change-studio
ninja -C build-dev -j2          # or -j1 under memory pressure
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -j1 --output-on-failure \
  -R "test_spatial_block|test_glcm|test_classifier|test_classification_postprocess|test_change_detector|test_confusion_matrix|test_magic_wand|test_feature_scatter|test_classification_studio|test_d15|test_classification_agent"
```
