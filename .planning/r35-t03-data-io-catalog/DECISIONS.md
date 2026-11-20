# DECISIONS — R35-T03 (unattended defaults, per .agents/AGENTS.md)
- D1 (WP02): instrument token contract = exact-token, case-insensitive, comma-split with ASCII-space trim on
  the record side (enrichment joins with ","). Matches itemMatchesInstruments predicate; "MSI" never matches
  "MSI2". No alias semantics invented.
- D2 (WP01): NaN/Inf bbox values and non-finite cloudCoverMax → GeoError(InvalidArgument) at validate(), before
  any IO. Legal 6-value bbox (indices 0/1/3/4 horizontal) and anti-meridian tolerance (w<=e only) unchanged.
- D3 (WP04): readJsonFile reads at most kMaxCatalogJsonBytes+1 bytes via istream::read — stat precheck kept as
  fast path only. Over-budget-during-read → InvalidMetadata (same cap message semantics); read error (badbit,
  e.g. directory named *.json) → OpenFailed path stays distinct from invalid-JSON InvalidMetadata.
- D4 (WP05): replace VSIReadDirRecursive full-tree materialization with iterative per-directory VSIReadDir BFS.
  Bounds: localMaxFiles candidates, localMaxDepth, directory-visit cap = localMaxFiles (work bound), visited-set
  for lexical cycles; symlinks followed like the old code (documented: not a sandbox; depth cap bounds loops).
  Truncation surfaced via existing page.truncatedByCap; skipped entries counted in LocalImpl counters.
  No cancel parameter added at open time (no API surface today; searchPage cancel unchanged).
- D5 (WP10): clampWindowToRaster/planTileWalk/TilePlan::slice/readBlock compute in int64 then narrow with
  representability checks. slice() throws GeoError(InvalidArgument) for out-of-range tile indices (WP10 asks
  for rejection; all current callers pass in-range indices).
- D6 (WP11): extract handle-driven metadata body in canonical_metadata.cpp; new public
  inspectOpenRaster(void* gdalDatasetHandle, path, options) (header stays GDAL-free via void*).
  RasterReader::open derives metadata from its own handle — single open, same generation.
- D7 (WP16): revision==0 is LEGAL ("not pinned", dataset_manifest.h:44). Strict decode: present-but-wrong-type
  (array/object/integer/string) → Result::failure with "dataset.manifest_invalid" + field-specific message;
  explicit negative revision → failure; absent optionals stay absent. Old legal manifests remain compatible.
- D8 (WP03): fabric temporal arm is documented half-open (end exclusive) by comment; asset_query instant arm is
  documented inclusive. NOT unified in this PR (different public contracts); both comments already state the
  boundary. Recorded as no-change with evidence.

## R1 review dispositions (commit b3e4b7c34b)
- F1 P2 token-internal whitespace: FIXED (edge-trim per segment) + multi-word tests.
- F2 P3 2^63 revision: FIXED (>= 2^63 refused) + test.
- F3 P3 dir-visit cap coupling: documented in catalog_service.h localMaxFiles.
- F4 P3 depth-cap truncation not reported: FIXED (walkTruncated set in depth branch).
- F5 P3 per-directory listing unbounded: documented in walk comment (VSIReadDir has no partial mode; tree bound is the improvement).
- F6 P3 skip-counter bucket shift (dangling entries counted at walk vs read time): NO-CHANGE — counters are internal metadata-only (header contract), both typed-skip buckets.
- F7 P3 NaN/Inf in jsoncpp details: FIXED (value omitted).
- F8 P3 BFS O(n^2) erase: FIXED (head index).
- F9 P3 untested directory-read path: FIXED (catalog.json-as-directory link-follow case).
- F10 P3 WP11 test Windows rename semantics: FIXED (POSIX guard).
- F11 P3 multi-lexical-path duplicate candidates via symlink loops: NO-CHANGE — same aliasing as the old VSIReadDirRecursive walk (which additionally recursed unboundedly); depth cap bounds it; documented in walk comment.
- F12 P3 defensive dead guards (gcount()<0, tilesX>INT_MAX): NO-CHANGE kept as defensive checks (reviewer: harmless).
