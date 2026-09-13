# GOAL — cn-eo-products-sensor-physics-10 · Chinese EO Product, Sensor Physics & Standardized Ingestion Platform 10.0

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/cn-eo-products-sensor-physics-10` (worktree `../exp-rs-cn-eo-products-sensor-physics-10`, off `origin/master` @ `7d78059d1a`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block on, trigger, or cite online CI.
> **Write scope:** `src/geospatial/products/`, `data/products/`, `src/operators/rs/rs_*import*`, `data/processing/algorithm_meta/`, `docs/products/`, `tests/test_cn_products.cpp` + new tests, `.planning/cn-eo-products-sensor-physics-10/`
> **Read-only:** `src/operators/rs/` SAR/temporal kernels (other tracks' authority), `src/processing/algorithms/` kernels (call, do not modify except narrow seams), `master` branch

## Mission

PR #956 (merged, `7d78059d1a`) added GF-1/2/6 PMS/WFV, ZY-3 TLC/NAD/FWD/BWD and HJ-1A/1B CCD
as fixed-set adapters with diagnosable refusal for other CN names
(`src/geospatial/products/cn_product_metadata.cpp:344-427`). Sensor truth is split across
three unlinked stores: `data/products/band_roles/gaofen.json` (range-midpoint wavelengths),
`data/spectral/sensors.json` `gf-pms` (nominal centers + FWHM, no cross-reference) and
compiled-in identity patterns. The ingestion pipeline exists only as three per-family
operators sharing header machinery (`src/operators/rs/rs_cn_import_operator.h`); there is no
unified identify→inspect→validate→plan service, no declared-generation reporting, no
optional calibration application, and no capability metadata for the CN operators
(`data/processing/algorithm_meta/capability/` has `rs-landsat-import.json` only).

This track turns the CN product support into a platform: one **sensor profile registry** as
single authority (platform/instrument/mode/band/role/wavelength/FWHM/GSD/polarization/
calibration rules/expected constituents), **versioned sidecar generation detection** with
explicit dispatch and unknown-element diagnostics, **typed refusals** that carry what a
product family would need instead of a static string, **new families** (HJ-2 CCD, GF-4
PMS, GF-7, ZY-1 02C/02D, CBERS-04, GF-3 SAR declared-metadata) behind the same
architecture, and one **standardized ImportPlan service** with provenance, numeric-domain
declaration and optional declared-coefficient calibration shared by every surface.

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended. No clarifying questions, no option menus. Take the
  default in Autonomy defaults; record every taken decision in DECISIONS.md.
- **Precedence**: this GOAL overrides `.agents/AGENTS.md` / `CLAUDE.md` on conflict.
- **Agent**: `zcode`. **Token budget: 300,000,000** (allocation in PLAN.md). Phase over
  1.5× envelope → budget line in EVIDENCE.md, continue.
- **Subagents: at most 2**, both read-only. #1 = architecture + science-correctness
  review; #2 = adversarial test/contract review. Main agent owns 100% of implementation.
- **No CI**: local evidence only → EVIDENCE.md.
- **Branching**: `master` read-only; all work in the worktree.
- **Build resources (hard)**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`;
  Ninja `-j2`, drop to `-j1` on RSS/load pressure; `-j$(nproc)` forbidden.
- **Tests**: `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` first.
- **Exit**: PR created, not merged.

## Autonomy defaults (do not ask — apply these)

1. **格式/来源**: sensor truth converges on a new versioned registry under
   `data/products/sensor_profiles/` with provenance strings; spectral resampling grids
   stay in `data/spectral/sensors.json` but gain derivation/consistency links; existing
   `band_roles/*.json` keep loading (back-compat) while the registry becomes the authority.
2. **失败项处置**: a product constituent that cannot be parsed is a typed diagnostic
   (completeness verdict + missing list), never skipped silently, never defaulted.
3. **命名/编号**: new sensor keys `<sat>_<sensor>[_variant]` lowercase; new ADR files
   take the next free number in `docs/adr/`; new tests extend `test_cn_products.cpp`
   (or new files `tests/test_cn_*.cpp`).
4. **资源与超时**: single command ≤ 20 min (build target) / ≤ 10 min (test family);
   on timeout drop to `-j1` and retry once; record in EVIDENCE.md.
5. **对外动作**: `git fetch`/`git push` to origin + `gh pr create` allowed; nothing else.
6. **范围外发现**: EVIDENCE.md `OUT_OF_SCOPE` section; P0 flagged in PR_BODY.md top.
7. **依赖新增**: none. Expat, jsoncpp, GDAL, Qt are already available.

## Work packages

| ID | Package | Key deliverables |
| --- | --- | --- |
| A | Sensor profile registry + generation dispatch | `data/products/sensor_profiles/*.json`, loader API, generation detection, diagnostics |
| B | New product families | HJ-2 CCD, GF-4, GF-7, ZY-1 02C/02D, CBERS-04, GF-3 declared-metadata + typed refusals upgraded |
| C | ImportPlan service + calibration application | shared identify→…→provenance service, optional DN→radiance, operators rebased on it |
| D | Integration + capability metadata | agent capability JSON, docs, GUI dialog coverage |
| E | Adversarial validation + review + PR | negative-case matrix, review fixes, PR |

## Completion gate

1. `ctest -R cn_products -j1` → exit 0 with new families covered.
2. `ctest -R io_product -j1` → exit 0 (registry adapters).
3. Band-truth check: one loader, zero duplicated wavelength tables (grep audit → EVIDENCE.md).
4. Targeted build of touched targets exit 0 with `-j2`.
5. PR created to `master`, not merged.
