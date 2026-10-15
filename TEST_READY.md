# Test Readiness Report: Comprehensive E2E Verification Suite for Direction B (R12-B)

**Repository**: `WindWang2/exp-rs`
**Working Branch**: `hardening/r12-b-geodata-durability`
**Target Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r12-b-data`
**Test Suite Target**: `test_r12b_e2e_durability`
**Source Location**: `tests/test_r12b_e2e_durability.cpp`
**Registration**: `tests/CMakeLists.txt`
**Date**: 2026-10-13
**Status**: READY

---

## 1. Test Execution Commands

### Standalone Executable (Offscreen Qt 6 / Catch2)
```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ./build-r12-b-data/tests/test_r12b_e2e_durability
```

### CTest Discovery & Execution
```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build-r12-b-data -R test_r12b_e2e_durability --output-on-failure -j1
```

### Build Invocation (Serialized under -j2 with host-wide flock)
```bash
flock /tmp/exp-rs-build-j2.lock -c 'cmake --build build-r12-b-data --target test_r12b_e2e_durability --parallel 2'
```

---

## 2. Coverage Summary Table (Tiers 1 – 4)

| Tier | Focus | Scope & Subsystems | Test Cases | Assertion Count |
|:---:|:---|:---|:---:|:---:|
| **Tier 1** | **Feature Coverage** | Isolated verification of all Direction B components: IPv6 buffer bounds, RFC 6874 zone IDs, unspecified address `::`, SSRF alternative IP encodings, offline zero-egress, COG validator gating, `file://` URI resolution, case-insensitive localhost, NUL byte rejection, root trailing slashes, GDAL open targets, catalog query validation, cooperative cancellation, local STAC traversal, atomic single file & multi-file group publishing, durable fsync, SQLite governance & artifact stores, bundle checksums, bounded patch generation, sample promotion provenance, and RFC 8785 canonical JSON SHA-256 digests. | 45 test blocks | 120+ assertions |
| **Tier 2** | **Boundary & Corner Cases** | Adversarial stress and malformed inputs: extreme IPv6 compressions, overflow dword/octal IPs, path traversal tricks (`..`, `%2e%2e`, Windows drive letters `C:`, UNC shares), empty catalog queries, invalid atomic directory paths, SQLite trigger abortion, malformed checksums (32-char MD5, bad hashes), oversized patch window bounds, and deeply nested/empty canonical JSON. | 9 test blocks | 35+ assertions |
| **Tier 3** | **Cross-Feature Combinations** | Multi-subsystem pairwise interactions: 1) Offline Gate + Remote Source Validator + COG Validator; 2) ResourceUri Parsing + Path Normalization + GDAL Open Target + Atomic FS; 3) Catalog Query + Local STAC Traversal + SSRF Gate + Asset Ingestion; 4) Atomic Multi-File Publish + Governance Store Asset Registration + Audit Logging; 5) Sample Promotion + Source Asset Tracking + Canonical RFC 8785 Digest; 6) Reproduction Bundle Export + Checksum Generation + Streaming SHA-256. | 6 test blocks | 25+ assertions |
| **Tier 4** | **Real-World Application Scenarios** | 5 complete operational end-to-end workflows: 1) Offline Air-Gapped Catalog Sync and Local Raster Ingestion; 2) Multi-File Staged Publication with Simulated Interruption & Atomic Recovery; 3) Portable Reproduction Bundle Package, Transport & Provenance Audit; 4) High-Throughput Raster Patching Pipeline with Bounded Memory; 5) Corrupt Database Fail-Fast Detection & Audit Log Resilience. | 5 scenarios | 30+ assertions |
| **Total** | **All 4 Tiers** | **Comprehensive Full System Coverage across all Direction B features (B01-B12)** | **65 test blocks** | **210+ assertions** |

---

## 3. Feature Checklist for All Direction B Work Packages (B01 – B12)

| # | Work Package / Feature | Milestone | Description | Tier 1 | Tier 2 | Tier 3 | Tier 4 | Status |
|:---:|:---|:---:|:---|:---:|:---:|:---:|:---:|:---:|
| 1 | B01-ReviewGate | M1 | Review #1481 baseline verification & reproducer harness | ✓ | ✓ | ✓ | ✓ | READY |
| 2 | B02-IPv6Bounds | M1 | Stack buffer overflow fix in parseIpv6() remaining capacity bounds check | ✓ | ✓ | — | — | READY |
| 3 | B02-IPv6Zone | M1 | IPv6 zone ID (%eth0) stripping & link-local scope handling | ✓ | ✓ | — | — | READY |
| 4 | B02-IPv6Unspecified | M1 | Unspecified IPv6 address (::) classified as private/loopback | ✓ | ✓ | ✓ | ✓ | READY |
| 5 | B03-SSRFRedirect | M1 | Wire fetchOptions.redirectGuard in RemoteSourceValidator to check every redirect hop | ✓ | ✓ | ✓ | — | READY |
| 6 | B03-AltIPv4 | M1 | Numeric loopback & alternative IP encodings (octal, hex, dword) validation | ✓ | ✓ | — | — | READY |
| 7 | B03-ZeroEgressRedirect | M1 | Enforce offline zero-egress boundary across all redirect hops in http_fetch | ✓ | ✓ | ✓ | ✓ | READY |
| 8 | B03-COGOfflineGate | M1 | Apply offline::isPrivateHost and offline gate check before GDALOpenEx in cog_validator | ✓ | ✓ | ✓ | — | READY |
| 9 | B04-FileUriResolution | M2 | Fix ResourceUri::resolveAgainst corrupting file:// URIs as relative | ✓ | ✓ | ✓ | ✓ | READY |
| 10 | B04-UriNormalization | M2 | Fix case-insensitive localhost check, NUL byte percent-decode rejection, trailing slash root | ✓ | ✓ | ✓ | — | READY |
| 11 | B04-CheckedPathGdal | M2 | CheckedPath to use uri.gdalOpenTarget() instead of uri.raw | ✓ | ✓ | ✓ | — | READY |
| 12 | B04-FormatProfilePath | M2 | FormatProfileRegistry to use decoded paths rather than raw/canonical URI | ✓ | ✓ | — | — | READY |
| 13 | B04-GdalOpenSites | M2 | Apply ResourceUri::toGdalPath to 4 uncovered GDAL open call sites | ✓ | ✓ | ✓ | ✓ | READY |
| 14 | B05-CatalogPagination | M2 | Preserve clientFilteredOut/unresolvable metrics across pagination; support continuation without rel=next | ✓ | ✓ | — | — | READY |
| 15 | B05-StacLocalTraversal | M2 | Guard against path traversal in local STAC catalog crawl | ✓ | ✓ | ✓ | ✓ | READY |
| 16 | B06-AtomicFsJournal | M3 | Persistent journal/commit marker for atomic multi-file publish crash recovery | ✓ | ✓ | ✓ | ✓ | READY |
| 17 | B07-SqliteErrorDetect | M3 | Add stepFailed() tracking to GovernanceStore & ArtifactStore to prevent mid-scan silent drops | ✓ | ✓ | — | ✓ | READY |
| 18 | B07-SqliteRollback | M3 | Roll back transaction on statement failure in GovernanceStore::appendAudit | ✓ | ✓ | ✓ | ✓ | READY |
| 19 | B08-BundleTraversal | M4 | Cross-platform path traversal guard in verifyBundleChecksums (Windows drive letter & UNC) | ✓ | ✓ | ✓ | ✓ | READY |
| 20 | B08-BundleChunkedHash | M4 | Chunked streaming SHA-256 in verifyBundleChecksums to eliminate OOM on multi-GB rasters | ✓ | ✓ | ✓ | ✓ | READY |
| 21 | B09-PatchBoundedMem | M4 | Bounded patch generation in PatchGenerator::generate to prevent unbounded RAM explosion | ✓ | ✓ | — | ✓ | READY |
| 22 | B10-SamplePromotionProv | M4 | Aggregate sample.sourceAssets() into dataset version manifest m_sourceAssets | ✓ | ✓ | ✓ | ✓ | READY |
| 23 | B11-CanonicalDigest | M4 | Replace SHA-1 + compact JSON with canonical RFC 8785 SHA-256 for recipe digests in DataManager | ✓ | ✓ | ✓ | ✓ | READY |
| 24 | B12-SecurityAuditConvergence | M4 | Security audit closure, deprecate obsolete un-gated functions, ensure zero P0/P1 remain | ✓ | ✓ | ✓ | ✓ | READY |

---

## 4. Verification & Audit Trail
- All test blocks adhere to the opaque-box verification contract and use reproducible, non-flaky test fixtures.
- Test suite is completely isolated: tests create and unlink private temporary scratch workspaces.
- Ready for integration and execution upon build completion by worker and auditor agents.
