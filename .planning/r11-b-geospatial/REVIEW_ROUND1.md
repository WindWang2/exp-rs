# Independent Adversarial Review Report (Round 1) — Direction B

**Track**: Direction B: Geospatial Data Fabric, Block Processing & Resilient Persistence  
**Branch**: `hardening/r11-b-geospatial-data`  
**Base Revision**: `origin/master...HEAD` (`8a08642760e2e8ffd2c4b965fc4683c01d3e4758`)  
**Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-b-geospatial`  
**Date**: 2026-10-13  
**Reviewer Role**: Independent Adversarial Reviewer (Zero prior participation in implementation)  
**Overall Verdict**: **FAIL** (1 P0 Blocker, 3 P1 Critical, 3 P2 Suggestions)

---

## 1. Gate Audit Summary

| Review Gate | Status | Findings |
|---|---|---|
| **1. Strict Scope & Ownership** | **PASS** | 0 edits to any of the 12 files locked by open PR #1475. Zero collision with Track A, C, or D. |
| **2. Lifecycle & Memory Safety** | **PASS (with P2)** | Dataset handles use RAII guards (`GdalDatasetGuard`) in `cog_validator.cpp` and `format_profiles.cpp`. Noted non-RAII site in `subdataset_inventory.cpp` (P2-3). |
| **3. Concurrency, Locks & Deadlocks** | **PASS** | `g_offline` atomic sequential consistency, `stagingCounter` atomic, lock acquisition orders clean. |
| **4. Correctness & Error Boundaries** | **FAIL** | 1 P0 Blocker (uncompressed IPv4-mapped IPv6 SSRF bypass), 3 P1 Critical issues (canonical URI slash collapse to UNC, searchPage candidate vs match count truncation drift, rollback .bak deletion on restore failure). |
| **5. Cross-Platform Portability** | **PASS (with P1)** | Windows drive letters (`C:/`) and UNC (`//server/share`) supported in parser, but URI canonicalization slash collapse impacts path round-tripping. |

---

## 2. Findings Detail

### [P0-1] Security Blocker: SSRF Protection Bypassed by Uncompressed IPv4-Mapped IPv6
- **Location**: `src/geospatial/remote/offline_gate.cpp:86-130`, `parseIpv6()`
- **Impact**: Security vulnerability (SSRF filter bypass). Allows attackers to reach internal services (AWS IMDS `169.254.169.254`, loopback `127.0.0.1`, RFC 1918 subnets) despite `blockPrivateNetworks` or STAC SSRF checks.
- **Root Cause**:
  `parseIpv6` splits the input into `head` (before `::`) and `tail` (after `::`).
  `allowV4Tail` was hardcoded to `false` for `head`.
  When an address has no `::` compression, such as `http://[0:0:0:0:0:ffff:127.0.0.1]/` or `http://[0000:0000:0000:0000:0000:ffff:169.254.169.254]/`, `cc == std::string::npos`. The entire address is treated as `head` with `allowV4Tail = false`.
  `parseSection` rejects `"127.0.0.1"` as an invalid hex group, causing `parseIpv6` to fail. `isPrivateHost` falls through and treats it as an allowed non-IP hostname!
- **Required Fix**: When `cc == std::string::npos`, `allowV4Tail` must be `true` for `head`.

### [P1-1] Contract Violation: `ResourceUri::canonical()` Collapses Slashes in `file:///`, Corrupting Local Paths into UNC Shares
- **Location**: `src/geospatial/util/resource_uri.cpp:197-217` (`normalizeSeparators`) & `641-650` (`canonical`)
- **Impact**: Non-idempotent URI parsing; local file paths turn into network UNC shares upon round-trip through `canonical()`.
- **Root Cause**:
  `normalizeSeparators` strips consecutive slashes unless `out.size() == 1 || schemeColon`. For `"file:///path/to/raster.tif"`, the third slash (`/`) is preceded by `file://`, where `out[5] != ':'`, so the root slash is dropped, resulting in `"file://path/to/raster.tif"`.
  When reparsed, `path` is treated as the authority/host, creating UNC path `"//path/to/raster.tif"`.
- **Required Fix**: In `ResourceUri::canonical()`, format `LocalFile` with `scheme == "file"` preserving the authority and path structure: `"file:///" + (path.front() == '/' ? path.substr(1) : path)` or preserving authority if UNC.

### [P1-2] Logic & Contract Violation: `CatalogService::searchPage` Conflates Walk Candidate Offset with Matched Record Count
- **Location**: `src/geospatial/fabric/catalog_service.cpp:739-755` & `843-875`
- **Impact**: Premature search truncation and missing records when client filters are applied.
- **Root Cause**:
  In `CatalogService::searchPage`, `page.offset + page.records.size() >= query.maxItems` used `page.offset` (which is `continuation.localOffset`, the scan index of candidates visited so far) instead of the count of matched records. Unmatched candidates advanced `page.offset`, falsely exhausting the `maxItems` quota.
- **Required Fix**: In `searchPage`, cap records when `page.records.size() >= query.maxItems`.

### [P1-3] Data Loss Edge Case: `publishStagedGroup` and `publishStagedMembers` Delete `.bak` Even When Restore Fails
- **Location**: `src/geospatial/util/atomic_fs.cpp:400-407` & `514-520`
- **Impact**: Loss of pre-existing good backup files during rollback failure.
- **Root Cause**:
  In `cleanup`, `if ( fileExists( targetMainPath ) ) removeFileQuiet( mainBackup );` checked only whether the target file exists, not whether the restore succeeded. If `renameReplaceQuiet` failed while the target file existed in a corrupted or locked state, the `.bak` was deleted.
- **Required Fix**: Track per-member restore success boolean flags and only delete `.bak` files when the restore actually succeeded.

### [P2] Minor Suggestions
- **[P2-1]**: IPv4-Compatible IPv6 (`::127.0.0.1`): Recommend recognizing IPv4-compatible addresses where `v6[0..11] == 0`.
- **[P2-2]**: Document non-dotted quad IPv4 integer formats as accepted limitation.
- **[P2-3]**: RAII Guard Missing in `subdataset_inventory.cpp`: Wrap `GDALDatasetH handle` in `GdalDatasetGuard` to prevent leaks on exception.

---

## 3. Review Gate Decision
**Status**: REJECTED pending resolution of P0-1, P1-1, P1-2, P1-3. Implementation team must fix all 4 issues, verify GREEN x 2 with dedicated regression tests, and submit for Round 2 review.
