# E2E Test Infrastructure: Direction B Geodata Durability, Security & Provenance (R12-B)

## 1. Test Philosophy
The R12-B test suite provides an **opaque-box, requirement-driven end-to-end verification harness** for geospatial data infrastructure, crash consistency, network security perimeters, SQLite transaction durability, and scientific evidence provenance in `exp-rs`.

Core testing tenets:
- **Opaque-Box & Requirement-Driven**: Tests assert externally observable contracts, file artifacts, database states, and cryptographic digests rather than inspecting private implementation details.
- **Fail-Closed Security Perimeter**: Verify that SSRF filters, IPv6 address bounds, and offline mode prevent unauthorized or unexpected outbound network egress under all conditions.
- **Crash Consistency & Atomic Durability**: Verify that multi-file datasets (rasters, sidecars, PAM files) publish transactionally without leaving partial or corrupt states on disk during crashes or exceptions.
- **SQLite Transaction Invariants**: Validate that database operations (assets, lineage, artifacts, audit logs) fail fast on errors rather than silently swallowing corrupted pages or aborted statements.
- **Cryptographic Authority & Determinism**: Enforce RFC 8785 canonical JSON serialization with single-standard SHA-256 digests across all recipe and manifest identity representations.
- **Bounded Resource Disciplines**: Ensure patch tiling and hash computations operate within strictly bounded memory limits, eliminating OOM exceptions on multi-GB datasets.

---

## 2. Systematic Verification Tiers

The test suite is organized into 4 systematic verification tiers:

### Tier 1: Feature Coverage (>=5 test cases per feature area)
Isolates and validates the primary behavior and contracts of each feature area across Direction B:
- **Area 1: IPv6 & Address Parsing / Memory Safety (B02)**: Buffer bounds, RFC 6874 zone ID stripping, loopback/private classifications, `::` handling.
- **Area 2: SSRF Protection & Offline Zero-Egress Boundaries (B03)**: Alternative IPv4 formats, offline gate enforcement, COG offline validation, redirect guard contracts.
- **Area 3: Resource URI Resolution & Canonicalization (B04)**: `file://` anchor resolution, case-insensitive localhost, NUL byte rejection, root trailing slashes, GDAL open targets.
- **Area 4: Catalog & STAC Traversal / Pagination Integrity (B05)**: Parameter validation, pagination metric retention, local STAC traversal containment, cooperative cancellation.
- **Area 5: Atomic Publishing & Crash Recovery (B06)**: Single-file atomic write, exception rollback, multi-file sidecar ordering, backup file restoration, fsync validation.
- **Area 6: SQLite Durability & Statement Error Propagation (B07)**: Governance store schema, asset transactions, audit transaction rollback on statement failure, artifact lifecycle.
- **Area 7: Reproduction Bundle Containment & Streaming Verification (B08)**: Checksum verification, path traversal rejection (POSIX, Windows drive letters, UNC), missing/tampered file detection.
- **Area 8: Resource Bounded Generation & Provenance Tracking (B09, B10)**: Patch generator config validation, bounded patch window generation, sample promotion provenance with source asset linkage.
- **Area 9: Canonical Digest & Authority Convergence (B11, B12)**: RFC 8785 canonical JSON sorting and formatting, SHA-256 recipe digests, streaming file digests.

### Tier 2: Boundary & Corner Cases (>=5 test cases per feature area)
Adversarial boundary stress, malformed encodings, and edge conditions:
- **Area 1 (IPv6)**: Extreme IPv6 representations (7 colons, double-colons at boundaries, 8-group tails, hex boundary overflows, complex interface zone IDs).
- **Area 2 (SSRF)**: Exotic IPv4 representations (octal with leading zeroes, hex dwords, shorthand 2-part/3-part IPs, overflow integer IPs).
- **Area 3 (URI)**: Path traversal escapes (`..`, encoded `%2e%2e`, encoded `%2f`), Windows drive letters (`file:///C:/`), UNC paths (`file://server/share/`), `%00` embedded NUL.
- **Area 4 (Catalog)**: Empty STAC collections, missing next links, zero-item pages, invalid bounding box coordinates, malformed ISO timestamps.
- **Area 5 (Atomic FS)**: Read-only targets, non-existent parent directories, multi-file group partial publication failures, locked file collisions.
- **Area 6 (SQLite)**: Corrupted SQLite database files, constraint violations, transaction rollback on injected trigger failure.
- **Area 7 (Bundle)**: Empty checksums.txt, malformed hash lengths, non-hex characters, zero-byte files, multi-MB payloads.
- **Area 8 (Patch)**: 1x1 rasters with oversized windows, 0 stride handling, invalid seeds, border policy edge conditions.
- **Area 9 (Digest)**: Empty objects, deeply nested JSON, floating point extreme numbers (-0.0, NaN, exponential notation).

### Tier 3: Cross-Feature Interactions (Pairwise Combinations)
Verifies multi-subsystem contracts and data handoffs:
1. **Interaction 1**: Offline Gate + Remote Source Validator + COG Validator.
2. **Interaction 2**: ResourceUri Parsing + Path Normalization + GDAL Open Target + Atomic FS.
3. **Interaction 3**: Catalog Query + Local STAC Traversal + SSRF Gate + Asset Ingestion.
4. **Interaction 4**: Atomic Multi-File Publish + Governance Store Asset Registration + Audit Logging.
5. **Interaction 5**: Sample Promotion + Source Asset Tracking + Dataset Store Manifest Commit + Canonical RFC 8785 Digest.
6. **Interaction 6**: Reproduction Bundle Export + Checksum Generation + Path Traversal Verification + Streaming SHA-256.

### Tier 4: Real-World Application Workflows (>=5 Realistic Scenarios)
Comprehensive end-to-end operational workflows:
- **Scenario 1: Offline Air-Gapped Catalog Sync and Local Raster Ingestion**
  Ingest local STAC metadata and raster imagery in an air-gapped environment (`SICNU_OFFLINE=1`), ensuring 0 egress attempts, valid local URI canonicalization, and indexing into `GovernanceStore`.
- **Scenario 2: Multi-File Staged Publication with Simulated Interruption & Atomic Recovery**
  Publish a multi-file raster dataset (GeoTIFF + `.ovr` + `.aux.xml`), simulate an interruption before the main file commit, verify complete cleanup and absence of corrupt residue.
- **Scenario 3: Portable Reproduction Bundle Package, Transport & Provenance Audit**
  Package an experiment run, environment metadata, dataset version pin, and checksums into a portable reproduction bundle; verify integrity, ensure zero secret leakage, and validate streaming checksums.
- **Scenario 4: High-Throughput Raster Patching Pipeline with Bounded Memory**
  Generate patch windows from a large virtual extent using sliding-window and random strategies, verify memory limits are strictly observed, and promote patches into governed samples with source asset lineage.
- **Scenario 5: Corrupt Database Fail-Fast Detection & Audit Log Resilience**
  Execute governance and artifact mutations, simulate statement failures and database corruption, verify typed error propagation, and confirm transaction rollback and audit preservation.

---

## 3. Feature Inventory & Coverage Matrix

| # | Feature ID | Feature / Defect Description | Milestone | Tier 1 | Tier 2 | Tier 3 | Tier 4 |
|---|---|---|---|:---:|:---:|:---:|:---:|
| 1 | B01-ReviewGate | PR #1481 full regression gate baseline verification & reproducer harness | M1 | ✓ | ✓ | ✓ | ✓ |
| 2 | B02-IPv6Bounds | Stack buffer overflow fix in parseIpv6() remaining capacity bounds check | M1 | ✓ | ✓ | — | — |
| 3 | B02-IPv6Zone | IPv6 zone ID (%eth0) stripping & link-local scope handling | M1 | ✓ | ✓ | — | — |
| 4 | B02-IPv6Unspecified | Unspecified IPv6 address (::) classified as private/loopback | M1 | ✓ | ✓ | ✓ | ✓ |
| 5 | B03-SSRFRedirect | Wire fetchOptions.redirectGuard in RemoteSourceValidator to check every redirect hop | M1 | ✓ | ✓ | ✓ | — |
| 6 | B03-AltIPv4 | Numeric loopback & alternative IP encodings (octal, hex, dword) validation | M1 | ✓ | ✓ | — | — |
| 7 | B03-ZeroEgressRedirect | Enforce offline zero-egress boundary across all redirect hops in http_fetch | M1 | ✓ | ✓ | ✓ | ✓ |
| 8 | B03-COGOfflineGate | Apply offline::isPrivateHost and offline gate check before GDALOpenEx in cog_validator | M1 | ✓ | ✓ | ✓ | — |
| 9 | B04-FileUriResolution | Fix ResourceUri::resolveAgainst corrupting file:// URIs as relative | M2 | ✓ | ✓ | ✓ | ✓ |
| 10 | B04-UriNormalization | Fix case-insensitive localhost check, NUL byte percent-decode rejection, trailing slash root | M2 | ✓ | ✓ | ✓ | — |
| 11 | B04-CheckedPathGdal | CheckedPath to use uri.gdalOpenTarget() instead of uri.raw | M2 | ✓ | ✓ | ✓ | — |
| 12 | B04-FormatProfilePath | FormatProfileRegistry to use decoded paths rather than raw/canonical URI | M2 | ✓ | ✓ | — | — |
| 13 | B04-GdalOpenSites | Apply ResourceUri::toGdalPath to 4 uncovered GDAL open call sites | M2 | ✓ | ✓ | ✓ | ✓ |
| 14 | B05-CatalogPagination | Preserve clientFilteredOut/unresolvable metrics across pagination; support continuation without rel=next | M2 | ✓ | ✓ | — | — |
| 15 | B05-StacLocalTraversal | Guard against path traversal in local STAC catalog crawl | M2 | ✓ | ✓ | ✓ | ✓ |
| 16 | B06-AtomicFsJournal | Persistent journal/commit marker for atomic multi-file publish crash recovery | M3 | ✓ | ✓ | ✓ | ✓ |
| 17 | B07-SqliteErrorDetect | Add stepFailed() tracking to GovernanceStore & ArtifactStore to prevent mid-scan silent drops | M3 | ✓ | ✓ | — | ✓ |
| 18 | B07-SqliteRollback | Roll back transaction on statement failure in GovernanceStore::appendAudit | M3 | ✓ | ✓ | ✓ | ✓ |
| 19 | B08-BundleTraversal | Cross-platform path traversal guard in verifyBundleChecksums (Windows drive letter & UNC) | M4 | ✓ | ✓ | ✓ | ✓ |
| 20 | B08-BundleChunkedHash | Chunked streaming SHA-256 in verifyBundleChecksums to eliminate OOM on multi-GB rasters | M4 | ✓ | ✓ | ✓ | ✓ |
| 21 | B09-PatchBoundedMem | Bounded patch generation in PatchGenerator::generate to prevent unbounded RAM explosion | M4 | ✓ | ✓ | — | ✓ |
| 22 | B10-SamplePromotionProv | Aggregate sample.sourceAssets() into dataset version manifest m_sourceAssets | M4 | ✓ | ✓ | ✓ | ✓ |
| 23 | B11-CanonicalDigest | Replace SHA-1 + compact JSON with canonical RFC 8785 SHA-256 for recipe digests in DataManager | M4 | ✓ | ✓ | ✓ | ✓ |
| 24 | B12-SecurityAuditConvergence | Security audit closure, deprecate obsolete un-gated functions, ensure zero P0/P1 remain | M4 | ✓ | ✓ | ✓ | ✓ |

---

## 4. Test Architecture & Runner Governance

### Execution Environment
- **Test Framework**: Catch2 v3 (`Catch2::Catch2WithMain`).
- **Binary Target**: `test_r12b_e2e_durability` (located in `${CMAKE_BINARY_DIR}/tests/test_r12b_e2e_durability`).
- **Registration**: Registered in `tests/CMakeLists.txt` via `sicnu_discover_tests` with prefix `test_r12b_e2e_durability::`.
- **Headless Isolation**: Run with offscreen Qt platform (`QT_QPA_PLATFORM=offscreen`) and system library path (`LD_LIBRARY_PATH=/usr/lib`).

### Concurrency & Resource Discipline
- **Build Parallelism**: Strict `-j2` concurrency cap:
  `export CMAKE_BUILD_PARALLEL_LEVEL=2; export MAKEFLAGS=-j2`
- **Exclusive Build Lock**: Serialize builds with the host-wide flock lock:
  `flock /tmp/exp-rs-build-j2.lock -c 'cmake --build build-r12-b-data --target test_r12b_e2e_durability --parallel 2'`
- **Test Execution Parallelism**: Strict `-j1` sequential test execution:
  `export CTEST_PARALLEL_LEVEL=1; ctest --test-dir build-r12-b-data -R test_r12b_e2e_durability -j1 --output-on-failure`

### Command Line Invocations

**Build the Test Executable:**
```bash
flock /tmp/exp-rs-build-j2.lock -c 'cmake --build build-r12-b-data --target test_r12b_e2e_durability --parallel 2'
```

**Run via CTest:**
```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ctest --test-dir build-r12-b-data -R test_r12b_e2e_durability --output-on-failure -j1
```

**Run Directly via Binary:**
```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib ./build-r12-b-data/tests/test_r12b_e2e_durability
```
