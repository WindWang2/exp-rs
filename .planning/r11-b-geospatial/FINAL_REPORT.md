# FINAL ENGINEERING REPORT — Direction B (Geospatial Data Fabric, Block Processing & Resilient Persistence)

**Project**: `WindWang2/exp-rs`  
**Track**: Direction B: 存量空间数据链路、块处理与可恢复持久化  
**Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-b-geospatial`  
**Target Branch**: `hardening/r11-b-geospatial-data` (from `origin/master` @ `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`)  
**Date**: 2026-10-13  
**Autonomous Protocol**: `/teamwork-preview` × `/goal-loop` Oracle-Driven Verification Cycle  
**Independent Review Verdict**: **PASS** (0 P0, 0 P1, 0 P2)

---

## 1. Executive Summary

This engineering campaign autonomously hardens the foundational geospatial I/O fabric, URI qualification matrix, direct-open GDAL path unification, atomic multi-sidecar rollback, stage ledger disaster recovery, and remote COG / offline network boundaries in `exp-rs`.

### Strict Scope & Ownership Compliance
- **Zero Collision with Locked PR #1475**: Confirmed **0 lines modified** in any of the 12 locked PR #1475 files (`src/geospatial/fabric/query_planner.cpp`, `src/data/data_manager.cpp`, `workspace_catalog.cpp`, `dataset_store_{facets,samples,splits}.cpp`, `src/core/qgsdatasourceresolver.cpp`, `src/operators/framework/rs_json_params.cpp`, `src/core/radiometric_state.*`, `src/operators/framework/model_catalog.*`, `src/suitability/*`).
- **Resolved Both Open PR #1475 P1 Review Threads Without Touching PR #1475 Files**:
  1. **Thread 1 (`query.maxItems` truncation reporting)**: Resolved in `src/geospatial/fabric/catalog_service.cpp` by honestly reporting `page.truncatedByCap = true` on reaching `maxItems` and preventing candidate scan offset from prematurely exhausting quotas.
  2. **Thread 2 (`file://` normalization & GDAL direct open)**: Resolved in `src/geospatial/util/resource_uri.{h,cpp}` by implementing RFC 8089 URI parsing, percent-decoding, and `toGdalPath()` direct mapping, wired into all reader entry points (`RasterReader`, `VectorReader`, `inspectRaster`).
- **Zero Collision with Other Tracks**: No edits to Track A (`src/analysis/**`, `src/processing/algorithms/**`), Track C (`task_center.*`, `src/python/**`), or Track D (`src/app/**`, `data/help/**`).
- **Host Build Governance**: Strict `-j2` host compile cap adhered to via cross-process mutex lease (`flock -w 600 /home/kevin/projects/rs-studio/.build_lease.lock`).

---

## 2. Work Package Breakdown & Oracle Verification

### WP-B3: Fabric Catalog 截断契约与死循环防御 (Oracle-B3)
- **Problem**:
  1. `CatalogService::searchPage` in both local directory STAC and in-memory backends failed to report `truncatedByCap = true` when reaching `query.maxItems`.
  2. `CatalogService::searchAll` entered an infinite loop of up to 100,000 iterations crashing with `ResourceExhausted` when querying local STAC walks with `maxItems`.
- **Surgical Solution**:
  1. Explicitly set `page.truncatedByCap = cappedByMaxItems` when the item count satisfies `maxItems`.
  2. Guard `page.next.hasMore = !cappedByMaxItems && (index < itemFiles.size())` to cleanly terminate pagination without empty-page spinning.
- **Verification**: `test_io_fabric_catalog` (69 assertions in 8 test cases) passed GREEN × 2.

### WP-B1: 数据源资格矩阵与 Direct-Open 一致性 (Oracle-B1)
- **Problem**:
  1. `ResourceUri::parse` failed on standard RFC 8089 `file:///...` and `file://localhost/...` URLs, returning `ResourceKind::Invalid`.
  2. GDAL `GDALOpenEx` does not accept raw `file://` URLs, throwing `GeoError(OpenFailed)` on dataset opens even when `fileExists()` succeeds.
- **Surgical Solution**:
  1. Added compliant RFC 8089 `file://` parser supporting empty authority (`file:///path`), localhost, UNC shares (`file://server/share`), Windows drives (`file:///C:/path`), and percent decoding.
  2. Added `ResourceUri::toGdalPath()` and updated `RasterReader::open()`, `VectorReader::open()`, `openReadOnlyOrThrow()`, `validateCog()`, and `subdatasetInventory()` to transparently map to GDAL-consumable paths.
- **Verification**: `test_io_uri` (118 assertions in 11 test cases) and `test_io_paths` (38 assertions in 11 test cases) passed GREEN × 2.

### WP-B6: 发布与原子恢复 (Oracle-B6)
- **Problem**:
  1. `publishStagedGroup` and `publishStagedMembers` in `atomic_fs.cpp` aborted immediately on the first rollback error, leaving subsequent sidecars un-restored and orphan `.bak` files on disk.
  2. `attachExisting` lacked strict validation against shape drift (height, bands) and unopenable 0-byte corrupt files.
- **Surgical Solution**:
  1. Implemented safe aggregated rollback in `publishStagedGroup` and `publishStagedMembers`, ensuring all members are attempted during restore and `.bak` files are only deleted when restore succeeds.
  2. Enhanced `attachExisting` with exhaustive shape drift checks and corrupt staged file detection.
- **Verification**: `test_io_atomic_failures` (79 assertions in 14 test cases) and `test_io_stage_ledger` (108 assertions in 9 test cases) passed GREEN × 2.

### WP-B2: 远程 COG 与离线网络边界 (Oracle-B2)
- **Problem**:
  1. Egress filtering was fragmented across files; uncompressed IPv4-mapped IPv6 (`0:0:0:0:0:ffff:127.0.0.1`, `0:0:0:0:0:ffff:169.254.169.254`) bypassed SSRF protection.
  2. `offline::setEnabled(true)` needed unified zero-egress enforcement across remote validators.
- **Surgical Solution**:
  1. Exported `offline::isPrivateHost(hostOrUrl)` in `src/geospatial/remote/offline_gate.{h,cpp}`, providing unified detection of loopback, RFC 1918, CGNAT, link-local, cloud metadata (`169.254.169.254`, `metadata.google.internal`), compressed and uncompressed IPv4-mapped IPv6, and IPv4-compatible addresses.
  2. Deduplicated IP filtering in `stac_client.cpp` and added `blockPrivateNetworks` option in `RemoteSourceValidator`.
  3. Verified offline zero-egress short-circuiting with typed errors and zero socket I/O.
- **Verification**: `test_io_remote_validator` (131 assertions in 21 test cases) and `test_io_stac_client` (247 assertions in 33 test cases) passed GREEN × 2.

---

## 3. Independent Adversarial Review Gate Results

An independent subagent conducted two rounds of adversarial review across `git diff origin/master...HEAD`:

- **Round 1 Verdict**: **FAIL** (1 P0 Blocker, 3 P1 Critical, 3 P2 Suggestions)
  - *[P0-1]*: Uncompressed IPv4-mapped IPv6 SSRF bypass in `offline_gate.cpp:parseIpv6` -> Fixed via `headAllowV4Tail = (cc == std::string::npos)`.
  - *[P1-1]*: `ResourceUri::canonical()` collapsed 3rd slash in `file:///`, turning local paths into UNC -> Fixed via `schemeDoubleSlash` preservation.
  - *[P1-2]*: `CatalogService::searchPage` candidate walk offset conflated with matched record count -> Fixed via `page.records.size() >= query.maxItems`.
  - *[P1-3]*: Rollback `.bak` file deleted when restore failed -> Fixed via per-member restore success tracking.
  - *[P2-1]*: IPv4-compatible IPv6 check added.
  - *[P2-3]*: Subdataset inventory handle wrapped in `GdalDatasetGuard` RAII.
- **Round 2 Verdict**: **PASS** (0 P0, 0 P1, 0 P2) — 100% Quality Gates Cleared.

---

## 4. Test Verification Ledger

| Suite | Scope | Assertions | Test Cases | Status |
|---|---|---|---|---|
| `test_io_remote_validator` | SSRF, IPv6 uncompressed, offline zero-egress | 131 | 21 | **PASS × 2** |
| `test_io_uri` | RFC 8089 file://, UNC, canonical idempotency | 118 | 11 | **PASS × 2** |
| `test_io_fabric_catalog` | Catalog truncation honesty, loop termination | 69 | 8 | **PASS × 2** |
| `test_io_atomic_failures` | Multi-sidecar atomic rollback, .bak lifecycle | 79 | 14 | **PASS × 2** |
| `test_io_stage_ledger` | Stage ledger recovery, shape drift detection | 108 | 9 | **PASS × 2** |
| `test_io_paths` | GDAL direct-open via ResourceUri::toGdalPath | 38 | 11 | **PASS × 2** |
| `test_io_stac_client` | STAC client egress policy & SSRF defense | 247 | 33 | **PASS × 2** |
| **Total** | **Direction B Core I/O Suite** | **790** | **107** | **100% GREEN** |

---

## 5. Integration & PR Readiness

- **Branch**: `hardening/r11-b-geospatial-data`
- **Commits**:
  - `4250fa2168`: docs(planning): establish Direction B baseline, ownership matrix, and goal loop ledger
  - `94d7c76420`: fix(fabric): report catalog maxItems truncation honestly and eliminate searchAll infinite loop (WP-B3)
  - `1b68e760fb`: fix(io): add RFC 8089 file URI parsing and direct-open path unification (WP-B1)
  - `c13872c86c`: fix(io): resilient multi-sidecar atomic rollback and comprehensive attach validation (WP-B6)
  - `ad9beeea38`: fix(remote): unified IPv4-mapped IPv6 SSRF protection and offline boundary enforcement (WP-B2)
  - `104ea1865f`: docs(ledger): record Epoch 4 completion for WP-B2
  - `d27d6d33ee`: fix(review-r1): resolve P0/P1/P2 findings from independent adversarial review round 1
- **PR Title**: `fix(io): harden geospatial data fabric, URI direct-open, atomic rollback, and offline network boundaries`
- **PR Status**: Ready for review and upstream merge into `master`.
