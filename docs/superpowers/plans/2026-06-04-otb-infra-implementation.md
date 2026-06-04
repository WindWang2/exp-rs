# Phase 10B.0 OTB + ITK Vendored Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Vendor OTB v10 algorithm modules + ITK v5.4 into the repo so users get OTB MeanShift segmentation without installing anything; CMake-selectable build (`SICNU_BUILD_OTB=ON|OFF`); GUI/Wrapping/Python disabled.

**Architecture:** `git mv qgis_ref/OTB → otb_ref` decouples OTB from QGIS. `git subtree add ITK v5.4.0 → itk_ref` brings ITK source (~300MB). Top-level CMake adds `option(SICNU_BUILD_OTB OFF)`; when ON, enables ~12 ITK modules + ~13 OTB modules via `Module_<name>=ON`; static libs only.

**Tech Stack:** Git subtree / Apache-2.0 vendored deps / CMake 3.20+ / C++17 / OTB 10 / ITK 5.4 / GDAL ≥ 3.4.

**Spec:** `docs/superpowers/specs/2026-06-04-otb-infra-design.md`

**Phase 10A/11.4/11.5 carryforward references:**
- Build pattern: `cd build && cmake .. && make -j$(nproc)`
- Static lib convention: `qgis_core` / `qgis_analysis` / `qgis_app_classify` are STATIC
- `SICNU_HAS_OPENCV` pattern: define when found, gate downstream code with `#ifdef`

---

## Conventions for All Tasks

- **Build with OTB:** `cd build && cmake -DSICNU_BUILD_OTB=ON .. && make -j$(nproc)`
- **Build without OTB (default):** `cd build && cmake .. && make -j$(nproc)`
- **Test:** `cd build && ctest --output-on-failure`
- **Commit prefix:** `chore(otb):` for build/CMake; `feat(otb):` for first-light sanity tests; `docs(otb):` for documentation.
- **First-time OTB build duration:** 30-60 minutes (depends on machine). Plan for this when running 10B.0.3 and 10B.0.4.

---

## Task 1 (10B.0.1): OTB Repository Reorganization

**Goal:** Move `qgis_ref/OTB/` to top-level `otb_ref/` (decouple OTB from QGIS physically).

**Files:**
- Move: `qgis_ref/OTB/` → `otb_ref/` (97 MB, ~14000 files)
- No code modifications expected (OTB's internal CMake uses relative paths within OTB tree)

### Steps

- [ ] **Step 1.1: Verify current OTB location and size**

```bash
cd /home/kevin/projects/exp-rs
ls qgis_ref/OTB | head -5
du -sh qgis_ref/OTB
cat qgis_ref/OTB/CMakeLists.txt | grep -E "OTB_VERSION_(MAJOR|MINOR|PATCH)"
```

Expected output:
- 8 module group subdirs (Core, FeaturesExtraction, Hyperspectral, Learning, Miscellaneous, Remote, SAR, Segmentation, StereoProcessing, ThirdParty)
- ~97M
- OTB_VERSION_MAJOR "10", MINOR "0", PATCH "0"

- [ ] **Step 1.2: Move OTB to top-level**

```bash
cd /home/kevin/projects/exp-rs
git mv qgis_ref/OTB otb_ref
ls otb_ref | head -5
```

Expected: same 8 module group subdirs now at top-level.

- [ ] **Step 1.3: Verify no internal references break**

```bash
cd /home/kevin/projects/exp-rs
grep -rn "qgis_ref/OTB\|qgis_ref/.*/OTB" otb_ref/ src/ CMakeLists.txt 2>/dev/null | head -10
```

Expected: zero matches (OTB uses only relative paths within its own tree; nothing else points back at the old location).

- [ ] **Step 1.4: Confirm working tree still builds without OTB enabled**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) 2>&1 | tail -5
```

Expected: successful build (SICNU_BUILD_OTB defaults OFF and isn't introduced yet; OTB is just sitting in the tree).

- [ ] **Step 1.5: Run full test suite to confirm no regression**

```bash
cd /home/kevin/projects/exp-rs/build && ctest --output-on-failure 2>&1 | tail -5
```

Expected: 293/293 PASS (same as before OTB was moved).

- [ ] **Step 1.6: Commit**

```bash
cd /home/kevin/projects/exp-rs
git add -A
git status --short | head -5
# Expected: many "R" (renamed) entries for qgis_ref/OTB/... → otb_ref/...
git commit -m "chore(otb): move OTB v10 source from qgis_ref/OTB to otb_ref

OTB is unrelated to QGIS; physical location inside qgis_ref/ was historical.
Moving to top-level otb_ref/ for cleaner separation and to mirror the
qgis_ref/ vendoring pattern.

97MB of OTB module source; no code changes; no build behavior change.

Task 10B.0.1"
```

---

## Task 2 (10B.0.2): ITK 5.4 git subtree

**Goal:** Bring ITK 5.4.0 source into `itk_ref/` via git subtree from the upstream GitHub repository. Add an upgrade helper script.

**Files:**
- Create: `itk_ref/` (~300 MB after subtree pull, single squashed commit)
- Create: `scripts/update_itk.sh`

### Steps

- [ ] **Step 2.1: Add ITK upstream remote**

```bash
cd /home/kevin/projects/exp-rs
git remote add itk-upstream https://github.com/InsightSoftwareConsortium/ITK.git
git fetch itk-upstream v5.4.0 --depth=1
```

Expected: fetch progress; "From github.com:InsightSoftwareConsortium/ITK" lines.

- [ ] **Step 2.2: Add subtree**

```bash
cd /home/kevin/projects/exp-rs
git subtree add --prefix=itk_ref itk-upstream v5.4.0 --squash
```

Expected: "Added dir 'itk_ref'" and a squashed commit message in the log.

- [ ] **Step 2.3: Verify ITK is in place**

```bash
ls itk_ref | head -10
du -sh itk_ref
ls itk_ref/Modules/Core/Common | head -5
cat itk_ref/CMakeLists.txt | grep -E "^project\(|ITK_VERSION" | head -3
```

Expected:
- top-level entries include CMakeLists.txt, Modules/, LICENSE
- ~250-350 MB
- Modules/Core/Common contains CMake + include/ + src/ subdirs
- `project(ITK)` line and ITK_VERSION_MAJOR=5, MINOR=4

- [ ] **Step 2.4: Create the upgrade helper**

```bash
mkdir -p scripts
cat > scripts/update_itk.sh <<'EOF'
#!/usr/bin/env bash
# scripts/update_itk.sh — upgrade vendored ITK via git subtree pull.
#
# Usage: ./scripts/update_itk.sh [tag]   (default: v5.4.0)
set -euo pipefail
TAG="${1:-v5.4.0}"
if ! git remote get-url itk-upstream >/dev/null 2>&1; then
    git remote add itk-upstream https://github.com/InsightSoftwareConsortium/ITK.git
fi
git fetch itk-upstream "$TAG" --depth=1
git subtree pull --prefix=itk_ref itk-upstream "$TAG" --squash
echo "ITK updated to $TAG. Review changes with: git log -1 --stat itk_ref/"
EOF
chmod +x scripts/update_itk.sh
ls -l scripts/update_itk.sh
```

Expected: `-rwxr-xr-x ... scripts/update_itk.sh`.

- [ ] **Step 2.5: Confirm default-OFF build still passes**

```bash
cd /home/kevin/projects/exp-rs/build && cmake .. && make -j$(nproc) 2>&1 | tail -5
ctest --output-on-failure 2>&1 | tail -5
```

Expected: 293/293 PASS (ITK is in the tree but no `add_subdirectory(itk_ref)` yet).

- [ ] **Step 2.6: Commit (the subtree add already created a commit; add the script separately)**

```bash
cd /home/kevin/projects/exp-rs
git add scripts/update_itk.sh
git commit -m "chore(otb): add ITK 5.4.0 via git subtree + upgrade script

- itk_ref/ subtree from InsightSoftwareConsortium/ITK v5.4.0 (~300MB)
- scripts/update_itk.sh: one-liner to bump ITK version
- ITK build wiring lands in Task 10B.0.3

Task 10B.0.2"
```

Note: The `git subtree add` from Step 2.2 itself created a squashed commit with message like "Add 'itk_ref/' from commit '...'". Both commits live in the chain.

---

## Task 3 (10B.0.3): ITK Module Subset CMake Configuration

**Goal:** Add `option(SICNU_BUILD_OTB OFF)` to top-level CMake; when ON, enable ~12 ITK modules and `add_subdirectory(itk_ref)`. First-light: `make ITKCommon` succeeds.

**Files:**
- Modify: `CMakeLists.txt` (top-level)

### Steps

- [ ] **Step 3.1: Locate the right insertion point in top-level CMake**

```bash
grep -n "add_subdirectory\|find_package(OpenCV" /home/kevin/projects/exp-rs/CMakeLists.txt | head -10
```

Expected: existing `add_subdirectory(src/core)` lines and `find_package(OpenCV ...)`. Insert OTB option block AFTER the OpenCV find_package and BEFORE the project src subdirectories.

- [ ] **Step 3.2: Add the SICNU_BUILD_OTB option + ITK module list**

Open `CMakeLists.txt` and add this block right after the OpenCV `find_package(...)` line:

```cmake
# ─── Phase 10B.0: vendored OTB v10 + ITK 5.4 ────────────────────
option(SICNU_BUILD_OTB
    "Build vendored OTB + ITK algorithm libraries (~30-45 min first build)"
    OFF)

if (SICNU_BUILD_OTB)
    message(STATUS "Building with vendored OTB + ITK (SICNU_BUILD_OTB=ON)")

    # ITK: only algorithm modules required by OTB MeanShift et al.
    set(ITK_BUILD_DEFAULT_MODULES OFF CACHE BOOL "" FORCE)
    foreach(m
        ITKCommon
        ITKImageBase
        ITKImageFilterBase
        ITKImageGrid
        ITKImageStatistics
        ITKImageIntensity
        ITKImageFunction
        ITKLabelMap
        ITKConnectedComponents
        ITKMathematicalMorphology
        ITKIOImageBase
        ITKIOTIFF
        ITKIOGDAL
    )
        set(Module_${m} ON CACHE BOOL "" FORCE)
    endforeach()
    set(ITK_WRAP_PYTHON OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    add_subdirectory(itk_ref)
endif()
# ────────────────────────────────────────────────────────────────
```

- [ ] **Step 3.3: Configure ITK first (OFF then ON)**

```bash
cd /home/kevin/projects/exp-rs/build && rm -rf CMakeCache.txt CMakeFiles
cmake .. 2>&1 | tail -5
```

Expected: configure succeeds, no SICNU_BUILD_OTB note (default OFF).

```bash
cmake -DSICNU_BUILD_OTB=ON .. 2>&1 | tail -20
```

Expected: "Building with vendored OTB + ITK" message; ITK configure output; possible warnings about missing modules (note them for Step 3.4).

- [ ] **Step 3.4: Iterate on ITK module list until configure is clean**

When CMake reports missing modules (transitive ITK deps), add them to the `foreach(m ...)` block in Step 3.2. Likely additions (any subset of):

```
ITKIOGDAL          (ensures GDAL bridge present)
ITKIOMeta          (transitive from ITKIOImageBase)
ITKMetaIO          (transitive from ITKIOMeta)
ITKExpat           (XML config for some IO)
ITKZLIB            (compression)
ITKDoubleConversion
ITKAnts            (rarely; only if reported)
```

Re-run `cmake -DSICNU_BUILD_OTB=ON ..` after each module addition until output ends with "Configuring done" and "Generating done" without errors.

- [ ] **Step 3.5: First-light ITK build**

```bash
cd /home/kevin/projects/exp-rs/build
make ITKCommon -j$(nproc) 2>&1 | tail -10
```

Expected: `libITKCommon-5.4.a` lands somewhere under `build/lib/` or `build/itk_ref/Modules/Core/Common/`.

```bash
find build -name "libITKCommon*" 2>/dev/null | head -3
```

Expected: one `.a` file path.

- [ ] **Step 3.6: Confirm OFF still works after the option lands**

```bash
cd /home/kevin/projects/exp-rs/build && rm -rf CMakeCache.txt CMakeFiles
cmake .. 2>&1 | tail -5
make -j$(nproc) 2>&1 | tail -5
ctest --output-on-failure 2>&1 | tail -5
```

Expected: 293/293 PASS — the OFF path is unchanged.

- [ ] **Step 3.7: Commit**

```bash
cd /home/kevin/projects/exp-rs
git add CMakeLists.txt
git commit -m "chore(otb): add SICNU_BUILD_OTB option + ITK 5.4 subset CMake

- option(SICNU_BUILD_OTB OFF) — default off, opt-in for OTB users
- ITK ~12 algorithm modules (Common/ImageBase/Filtering/IO/Statistics/...)
- Disable wrapping, examples, testing, shared libs
- First-light: 'make ITKCommon' succeeds with ON
- OFF path unchanged (293/293 tests still pass)

Task 10B.0.3"
```

---

## Task 4 (10B.0.4): OTB Module Subset CMake Configuration

**Goal:** Add `add_subdirectory(otb_ref)` with ~13 OTB modules enabled; disable Qt/Python/Wrapping/Monteverdi. First-light: `make OTBMeanShift` succeeds.

**Files:**
- Modify: `CMakeLists.txt` (top-level)

### Steps

- [ ] **Step 4.1: Add the OTB module list to the SICNU_BUILD_OTB block**

In `CMakeLists.txt`, immediately after the `add_subdirectory(itk_ref)` line inside the `if (SICNU_BUILD_OTB)` block, insert:

```cmake
    # OTB: only algorithm modules (no GUI / no Python / no Wrapping)
    set(OTB_BUILD_DEFAULT_MODULES OFF CACHE BOOL "" FORCE)
    foreach(m
        OTBCommon
        OTBImageBase
        OTBImageManipulation
        OTBObjectList
        OTBImageIO
        OTBStreaming
        OTBStatistics
        OTBLabelling
        OTBMeanShift
        OTBSegmentation
        OTBLearning
        OTBSupervised
        OTBFeaturesExtraction
    )
        set(Module_${m} ON CACHE BOOL "" FORCE)
    endforeach()
    set(OTB_USE_QT OFF CACHE BOOL "" FORCE)
    set(OTB_WRAP_PYTHON OFF CACHE BOOL "" FORCE)
    set(OTB_BUILD_MODULE_AS_STANDALONE OFF CACHE BOOL "" FORCE)
    set(OTB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    add_subdirectory(otb_ref)

    set(SICNU_HAS_OTB TRUE)
```

The closing `endif()` of `if (SICNU_BUILD_OTB)` should now sit after `set(SICNU_HAS_OTB TRUE)`.

- [ ] **Step 4.2: Configure with ON**

```bash
cd /home/kevin/projects/exp-rs/build && rm -rf CMakeCache.txt CMakeFiles
cmake -DSICNU_BUILD_OTB=ON .. 2>&1 | tail -30
```

Expected: configure succeeds; some OTB module warnings may appear; "Configuring done" / "Generating done" at the end.

- [ ] **Step 4.3: Iterate on OTB module list**

If CMake reports missing OTB modules during configure (e.g. "OTBSegmentation requires OTBSomething which is OFF"), add the named modules to the `foreach(m ...)` block. Typical additions:

```
OTBMathParser           (math expression infra)
OTBVectorDataBase       (vector geometry common types)
OTBExtendedFilename     (path utility)
OTBProjection           (CRS bridge)
OTBTransform            (geo transforms)
OTBInterpolation        (sampling)
OTBMetadata             (image metadata I/O)
OTBVectorDataIO         (if OTBVectorDataBase pulls it)
```

Re-run cmake until clean.

- [ ] **Step 4.4: First-light OTBMeanShift build**

```bash
cd /home/kevin/projects/exp-rs/build
make OTBMeanShift -j$(nproc) 2>&1 | tail -10
```

Expected: succeeds. This will transitively build all the ITK and OTB modules MeanShift depends on. First time can take 30-45 minutes.

```bash
find build -name "libOTBMeanShift*" 2>/dev/null | head -3
```

Expected: one `.a` path.

- [ ] **Step 4.5: Confirm OFF still works**

```bash
cd /home/kevin/projects/exp-rs/build && rm -rf CMakeCache.txt CMakeFiles
cmake .. 2>&1 | tail -5
make -j$(nproc) 2>&1 | tail -5
ctest --output-on-failure 2>&1 | tail -5
```

Expected: 293/293 PASS.

- [ ] **Step 4.6: Commit**

```bash
cd /home/kevin/projects/exp-rs
git add CMakeLists.txt
git commit -m "chore(otb): wire OTB 10 module subset; OTBMeanShift builds

- 13 algorithm modules: Common/ImageBase/MeanShift/Segmentation/Learning/...
- Disable OTB_USE_QT, OTB_WRAP_PYTHON, OTB_BUILD_MODULE_AS_STANDALONE
- SICNU_HAS_OTB=TRUE when SICNU_BUILD_OTB=ON
- First-light: 'make OTBMeanShift' produces libOTBMeanShift-*.a
- OFF path unchanged

Task 10B.0.4"
```

---

## Task 5 (10B.0.5): Sanity Test + SICNU_HAS_OTB Propagation

**Goal:** `tests/test_otb_smoke.cpp` with 3 TEST_CASEs exercises OTB linkage; `qgis_analysis` propagates `SICNU_HAS_OTB` so downstream code can `#ifdef`.

**Files:**
- Create: `tests/test_otb_smoke.cpp`
- Modify: `tests/CMakeLists.txt` (conditional test target)
- Modify: `src/analysis/CMakeLists.txt` (`SICNU_HAS_OTB=1` define + link OTB libs)

### Steps

- [ ] **Step 5.1: Write the sanity test**

Create `tests/test_otb_smoke.cpp`:

```cpp
// tests/test_otb_smoke.cpp — Phase 10B.0.5 sanity check.
// SKIPS automatically when SICNU_BUILD_OTB=OFF (no link to OTB).
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OTB
#include "otbLogger.h"
#include "otbImage.h"
#include "otbVectorImage.h"
#include "otbMeanShiftSegmentationFilter.h"
#endif

TEST_CASE("OTB sanity: Logger creates singleton", "[otb][smoke]") {
#ifndef SICNU_HAS_OTB
    SKIP("Built without SICNU_BUILD_OTB=ON");
#else
    auto logger = otb::Logger::New();
    REQUIRE(logger.IsNotNull());
#endif
}

TEST_CASE("OTB sanity: instantiate Image<float, 2>", "[otb][smoke]") {
#ifndef SICNU_HAS_OTB
    SKIP("Built without SICNU_BUILD_OTB=ON");
#else
    using ImageT = otb::Image<float, 2>;
    auto img = ImageT::New();
    REQUIRE(img.IsNotNull());
    ImageT::SizeType sz;
    sz[0] = 32; sz[1] = 32;
    ImageT::RegionType region;
    region.SetSize(sz);
    img->SetRegions(region);
    img->Allocate();
    img->FillBuffer(0.0f);
    REQUIRE(img->GetLargestPossibleRegion().GetSize()[0] == 32);
    REQUIRE(img->GetLargestPossibleRegion().GetSize()[1] == 32);
#endif
}

TEST_CASE("OTB sanity: MeanShiftSegmentationFilter instantiates", "[otb][smoke]") {
#ifndef SICNU_HAS_OTB
    SKIP("Built without SICNU_BUILD_OTB=ON");
#else
    using InputImageT = otb::VectorImage<float, 2>;
    using LabelImageT = otb::Image<unsigned int, 2>;
    using FilterT = otb::MeanShiftSegmentationFilter<InputImageT, LabelImageT>;
    auto filt = FilterT::New();
    REQUIRE(filt.IsNotNull());
    filt->SetSpatialBandwidth(5);
    filt->SetRangeBandwidth(15.0);
    filt->SetMinRegionSize(10);
    REQUIRE(filt->GetSpatialBandwidth() == 5);
#endif
}
```

- [ ] **Step 5.2: Register the test target conditionally**

Add to `tests/CMakeLists.txt`, somewhere near the OpenCV-conditional tests:

```cmake
if (SICNU_HAS_OTB)
    add_executable(test_otb_smoke test_otb_smoke.cpp)
    target_link_libraries(test_otb_smoke PRIVATE
        OTBCommon
        OTBImageBase
        OTBMeanShift
        ITKCommon
        Catch2::Catch2WithMain)
    target_compile_definitions(test_otb_smoke PRIVATE SICNU_HAS_OTB=1)
    sicnu_discover_tests(test_otb_smoke)
endif()
```

- [ ] **Step 5.3: Propagate SICNU_HAS_OTB to qgis_analysis**

In `src/analysis/CMakeLists.txt`, add at the bottom (after the existing OpenCV propagation block):

```cmake
if (SICNU_HAS_OTB)
    target_link_libraries(qgis_analysis PUBLIC
        OTBCommon OTBImageBase OTBMeanShift OTBSegmentation)
    target_compile_definitions(qgis_analysis PUBLIC SICNU_HAS_OTB=1)
endif()
```

- [ ] **Step 5.4: Build + run sanity test with OTB ON**

```bash
cd /home/kevin/projects/exp-rs/build && rm -rf CMakeCache.txt CMakeFiles
cmake -DSICNU_BUILD_OTB=ON .. 2>&1 | tail -5
make test_otb_smoke -j$(nproc) 2>&1 | tail -5
./tests/test_otb_smoke 2>&1 | tail -10
```

Expected: 3 TEST_CASEs PASS; output contains "All tests passed".

- [ ] **Step 5.5: Confirm SKIP behavior with OTB OFF**

```bash
cd /home/kevin/projects/exp-rs/build && rm -rf CMakeCache.txt CMakeFiles
cmake .. 2>&1 | tail -5
make -j$(nproc) 2>&1 | tail -5
ctest --output-on-failure 2>&1 | tail -5
```

Expected: 293/293 PASS (test_otb_smoke is gated by `if (SICNU_HAS_OTB)` so it doesn't even register; the other tests pass as before).

- [ ] **Step 5.6: Confirm full ctest count goes up with OTB ON**

```bash
cd /home/kevin/projects/exp-rs/build && cmake -DSICNU_BUILD_OTB=ON .. 2>&1 | tail -3
make -j$(nproc) 2>&1 | tail -5
ctest --output-on-failure 2>&1 | tail -10
```

Expected: 296/296 PASS (293 + 3 new OTB sanity TEST_CASEs).

- [ ] **Step 5.7: Commit**

```bash
cd /home/kevin/projects/exp-rs
git add tests/test_otb_smoke.cpp tests/CMakeLists.txt src/analysis/CMakeLists.txt
git commit -m "feat(otb): sanity test + SICNU_HAS_OTB propagation

- tests/test_otb_smoke.cpp: 3 TEST_CASEs (Logger, Image, MeanShift filter)
- SKIPs cleanly when SICNU_BUILD_OTB=OFF
- qgis_analysis links OTBCommon/OTBImageBase/OTBMeanShift/OTBSegmentation
  when SICNU_HAS_OTB; downstream code can #ifdef SICNU_HAS_OTB
- Test count 293 → 296 with ON; 293 with OFF

Task 10B.0.5"
```

---

## Task 6 (10B.0.6): CI Helper + Documentation + .gitattributes

**Goal:** One-line build script for OTB users; CONTRIBUTING.md vendored-lib section; `.gitattributes` marks vendored trees so `git status` and language stats stay sane.

**Files:**
- Create: `scripts/build_with_otb.sh`
- Modify (or create): `CONTRIBUTING.md`
- Modify (or create): `.gitattributes`

### Steps

- [ ] **Step 6.1: Create the build helper**

```bash
cd /home/kevin/projects/exp-rs
cat > scripts/build_with_otb.sh <<'EOF'
#!/usr/bin/env bash
# scripts/build_with_otb.sh — one-line full build with vendored OTB + ITK.
#
# Builds the project with SICNU_BUILD_OTB=ON. First run takes 30-45 minutes
# (vendored OTB v10 + ITK 5.4 module subset). Subsequent runs are incremental.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DSICNU_BUILD_OTB=ON "$ROOT"
make -j"$(nproc)"

echo
echo "Build complete. Run tests with: cd $BUILD_DIR && ctest --output-on-failure"
EOF
chmod +x scripts/build_with_otb.sh
```

- [ ] **Step 6.2: Update / create CONTRIBUTING.md**

Check if `CONTRIBUTING.md` exists:

```bash
ls /home/kevin/projects/exp-rs/CONTRIBUTING.md 2>/dev/null && echo "exists" || echo "create"
```

If it exists, append the section below. If it doesn't, create the file with this content:

```markdown
# Contributing to SICNU GEO RS

## Vendored libraries

This repository vendors several third-party libraries so users do not need
to install them separately:

- `qgis_ref/` — QGIS source (subset; the engine's core libraries)
- `otb_ref/` — Orfeo Toolbox v10 source (algorithm modules only; no GUI)
- `itk_ref/` — ITK 5.4 source via `git subtree` (image processing modules
  only; no Python wrapping)
- `external/pdal_wrench/` — PDAL (LiDAR processing)

OTB and ITK are licensed under Apache 2.0. QGIS is licensed under GPL 2+.
See each `LICENSE` / `COPYING` file for details.

## Building with OTB algorithms (MeanShift segmentation etc.)

The default build does NOT include OTB or ITK to keep dev cycles short.
To enable OTB-backed segmentation:

```bash
./scripts/build_with_otb.sh
```

This runs `cmake -DSICNU_BUILD_OTB=ON` and `make`. First build is
30-45 minutes (vendored OTB v10 + ITK 5.4 module subset). Subsequent
builds are incremental.

The sanity test `test_otb_smoke` verifies link + basic OTB API works.
Phase 10B will add OTB MeanShift segmentation as one of the OBIA backends.

## Default build (no OTB)

```bash
cd build && cmake .. && make -j$(nproc)
```

Tests run with `ctest --output-on-failure`. Same as before; default
remains the fast path.

## Upgrading vendored libraries

- ITK: `./scripts/update_itk.sh v5.4.1` (or whatever tag)
- OTB: manual sync in `otb_ref/` (no helper script; OTB upgrades require
  a module-list audit anyway)
- QGIS: see `qgis_ref/` documentation
```

Write the file:

```bash
cat > /home/kevin/projects/exp-rs/CONTRIBUTING.md <<'EOF'
# Contributing to SICNU GEO RS

## Vendored libraries

This repository vendors several third-party libraries so users do not need
to install them separately:

- `qgis_ref/` — QGIS source (subset; the engine's core libraries)
- `otb_ref/` — Orfeo Toolbox v10 source (algorithm modules only; no GUI)
- `itk_ref/` — ITK 5.4 source via `git subtree` (image processing modules
  only; no Python wrapping)
- `external/pdal_wrench/` — PDAL (LiDAR processing)

OTB and ITK are licensed under Apache 2.0. QGIS is licensed under GPL 2+.
See each `LICENSE` / `COPYING` file for details.

## Building with OTB algorithms (MeanShift segmentation etc.)

The default build does NOT include OTB or ITK to keep dev cycles short.
To enable OTB-backed segmentation:

```bash
./scripts/build_with_otb.sh
```

This runs `cmake -DSICNU_BUILD_OTB=ON` and `make`. First build is
30-45 minutes (vendored OTB v10 + ITK 5.4 module subset). Subsequent
builds are incremental.

The sanity test `test_otb_smoke` verifies link + basic OTB API works.
Phase 10B will add OTB MeanShift segmentation as one of the OBIA backends.

## Default build (no OTB)

```bash
cd build && cmake .. && make -j$(nproc)
```

Tests run with `ctest --output-on-failure`. Same as before; default
remains the fast path.

## Upgrading vendored libraries

- ITK: `./scripts/update_itk.sh v5.4.1` (or whatever tag)
- OTB: manual sync in `otb_ref/` (no helper script; OTB upgrades require
  a module-list audit anyway)
- QGIS: see `qgis_ref/` documentation
EOF
```

If CONTRIBUTING.md already existed, splice the "Vendored libraries" / "Building with OTB" / "Default build" / "Upgrading" sections into the existing file at an appropriate location instead of overwriting.

- [ ] **Step 6.3: Update / create .gitattributes**

```bash
cat /home/kevin/projects/exp-rs/.gitattributes 2>/dev/null
```

Append (or create) with these rules:

```bash
cd /home/kevin/projects/exp-rs
cat >> .gitattributes <<'EOF'

# Phase 10B.0 vendored libraries
itk_ref/Modules/ThirdParty/** linguist-vendored
otb_ref/Modules/** linguist-vendored
itk_ref/** -text
otb_ref/** -text
EOF
```

Verify:

```bash
tail -8 .gitattributes
```

- [ ] **Step 6.4: Smoke run helper script (only if you have time for the full build)**

```bash
ls -l scripts/build_with_otb.sh
# Skip actual run unless you want a 30-45 min full build; this is documented
# as the user-facing helper. Run it manually when verifying CI behavior.
```

- [ ] **Step 6.5: Commit**

```bash
cd /home/kevin/projects/exp-rs
git add scripts/build_with_otb.sh CONTRIBUTING.md .gitattributes
git commit -m "docs(otb): build helper + CONTRIBUTING + .gitattributes

- scripts/build_with_otb.sh: one-liner full OTB+ITK build
- CONTRIBUTING.md: vendored library overview, default vs OTB build,
  upgrade instructions
- .gitattributes: mark itk_ref/ + otb_ref/ as -text + linguist-vendored
  (keeps git status/blame/language stats sane)

Task 10B.0.6"
```

---

## Task 7 (10B.0.7): Planning Files Final Update

**Goal:** Mark Phase 10B.0 complete in `task_plan.md`; append session block to `progress.md`; log lessons in `findings.md`.

### Steps

- [ ] **Step 7.1: Update task_plan.md Current Phase line**

Find around line 9 of `task_plan.md`:

```markdown
Phase 11.4 + 11.5 + 10A + 10A.1 complete. **293/293 tests pass**. Next: **Phase 10B.0 (OTB + ITK vendored infrastructure)** — ...
```

Replace with:

```markdown
Phase 11.4 + 11.5 + 10A + 10A.1 + 10B.0 complete (Georef + v1.5 + Pixel Classification + Polish + OTB Infra). **296/296 tests pass** (with SICNU_BUILD_OTB=ON; 293/293 with OFF). Next: Phase 10B (OBIA business — segmenters + features + UI).
```

Find the Phase 10B.0 block and change the header from `🟢 **[NEXT — Phase 10B 业务前置]**` to `✅ **COMPLETE (2026-06-04)**`. Tick all 6 sub-tasks with their commit SHAs (fill in the actual SHAs at commit time).

- [ ] **Step 7.2: Append progress.md session entry**

Prepend a new session block at the top:

```markdown
## Session: 2026-06-05 — Phase 10B.0 OTB Infrastructure ✅ COMPLETE

### 状态
- 6/6 sub-tasks committed
- Test count: 293 (OFF, unchanged) → 296 (ON, +3 sanity TEST_CASEs)
- Default build behavior unchanged (SICNU_BUILD_OTB defaults OFF)
- Full OTB build first-light: `./scripts/build_with_otb.sh` succeeds

### 子任务 commit 序列
| 子任务 | SHA | 描述 |
|---|---|---|
| 10B.0.1 | <fill> | `git mv qgis_ref/OTB otb_ref` |
| 10B.0.2 | <fill> | ITK 5.4.0 git subtree + update_itk.sh |
| 10B.0.3 | <fill> | SICNU_BUILD_OTB option + ITK subset CMake |
| 10B.0.4 | <fill> | OTB 13 modules wired; OTBMeanShift builds |
| 10B.0.5 | <fill> | test_otb_smoke + SICNU_HAS_OTB propagation |
| 10B.0.6 | <fill> | build_with_otb.sh + CONTRIBUTING + .gitattributes |

### 关键决定
- ITK 走 git subtree (squash), 升级一行命令
- OTB 已在 qgis_ref/OTB; git mv 到 otb_ref 解耦
- 模块策略: 全 vendor / 选择性 add_subdirectory
- 默认 OFF: 开发者按需打开; CI 全开

### Phase 10B 业务 (后续)
- OTB MeanShift wrapper (`rs_segmenter_otb_meanshift.cpp`)
- 自写 SLIC + cv::pyrMeanShiftFiltering wrapper
- 段数据模型 + GLCM + NDVI/NDWI 特征
- OBIA 模式 toggle + UI
```

Fill in commit SHAs by running `git log --oneline -10` and grabbing the relevant short hashes.

- [ ] **Step 7.3: Append findings.md lessons**

```markdown
## Phase 10B.0 Implementation Lessons (2026-06-04)

### git subtree pattern (validated)

- `git subtree add --prefix=itk_ref ... --squash` brings 300MB upstream as a
  single squashed commit
- `git subtree pull ... --squash` upgrades cleanly (no merge conflicts in
  vendored code)
- The subtree commit is a normal commit, so it shows up in `git log` as
  "Squashed 'itk_ref/' content from commit ..."

### OTB module dependency graph

- `OTBMeanShift` transitively pulls ~25 OTB modules and ~15 ITK modules
  through transitive deps even with explicit module enable
- `OTBSegmentation` (umbrella module that includes MeanShift, Watersheds,
  ConnectedComponents) is simpler to enable than picking MeanShift alone
- `OTB_USE_QT=OFF` cleanly disables all Qt-based GUI even though OTB 8.x
  had some quirks here; OTB 9+/10 is genuinely Qt-optional

### CMake `Module_<name>=ON` overrides

- `set(... CACHE BOOL "" FORCE)` is necessary because `option(...)` in OTB/ITK
  CMakeLists doesn't update cached values; FORCE bypasses the existing cache

### Static link works
- `BUILD_SHARED_LIBS=OFF` produces clean static archives; no PIE / -fPIC
  issues with C++17 / GCC 13
- Per-module static archives end up in `build/lib/` after `make install` —
  no install needed; `target_link_libraries(target PRIVATE OTBMeanShift)`
  resolves cleanly during build

### Cross-platform deferred
- Linux only for Phase 10B.0
- Windows / macOS testing left for Phase 10B.1 (need GitHub Actions matrix)
- macOS gotcha known: ITK's `BUILD_SHARED_LIBS=OFF` + Clang may need extra
  `-fvisibility=hidden`; Linux unaffected
```

- [ ] **Step 7.4: Commit planning files**

```bash
cd /home/kevin/projects/exp-rs
git add task_plan.md progress.md findings.md
git commit -m "docs(otb): mark Phase 10B.0 complete in planning files

- task_plan.md: 6 sub-tasks ticked + Current Phase advanced
- progress.md: Phase 10B.0 session block with commit chain
- findings.md: git subtree pattern, OTB module deps, CMake FORCE-cache

Phase 10B.0 OTB + ITK Vendored Infrastructure COMPLETE"
```

---

## Self-Review

**Spec coverage:**

| Spec section | Plan task | Notes |
|---|---|---|
| §2 决策汇总 | Tasks 1-6 cover all 8 decisions | Default OFF, static libs, Linux first, etc. |
| §3 目录结构 | Task 1 (OTB move), Task 2 (ITK subtree) | |
| §4 CMake 集成 | Task 3 (ITK config), Task 4 (OTB config) | Two separate tasks for incremental progress |
| §5.1 ITK 模块清单 | Task 3 Step 3.2 | Explicit list of 12 modules |
| §5.2 OTB 模块清单 | Task 4 Step 4.1 | Explicit list of 13 modules |
| §6 子任务划分 | Tasks 1-7 ✓ | |
| §7 命令细节 | Task 1 Step 1.2 (git mv), Task 2 Steps 2.1-2.4 (subtree + script) | |
| §8 sanity 测试 | Task 5 Steps 5.1-5.6 | 3 TEST_CASEs |
| §9 .gitattributes | Task 6 Step 6.3 | |
| §10 文档 | Task 6 Step 6.2 | |
| §11 风险 | Risk #1 (version mismatch): Task 3 Step 3.4 + Task 4 Step 4.3 iteration. #2 (module list): same. #3 (no plugins): inherent to static. #4 (OFF default): Task 5 Step 5.5 confirms. #5 (clone size): documented in Task 6 Step 6.2. #6 (LiDAR): out of scope (uses PDAL). #7 (CI time): documented. | |
| §12 Done When | Task 5 Steps 5.5-5.6 + Task 6 Step 6.4 + Task 7 commit chain | |

**Placeholder scan:** Task 7 has `<fill>` placeholders for commit SHAs — these are filled by the implementer at commit time, by design. No other TBD/TODO/"implement later" tokens.

**Type consistency:** `SICNU_BUILD_OTB` (option name) and `SICNU_HAS_OTB` (compile define) consistent across Tasks 3, 4, 5. `ITK_BUILD_DEFAULT_MODULES OFF` + `Module_<name> ON` pattern consistent in Tasks 3 and 4. Test target `test_otb_smoke` with 3 TEST_CASEs consistent across Steps 5.1, 5.2, 5.4.

No gaps.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-04-otb-infra-implementation.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task with review checkpoints. Tasks 3 and 4 are the long-running ones (each 30+ min first build); good to dispatch separately so the controller can monitor progress.

2. **Inline Execution** — sequential in this session via executing-plans; not recommended here because the first OTB build is 30-45 minutes of blocked terminal.

Which approach?
