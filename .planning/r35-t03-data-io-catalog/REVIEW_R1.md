# REVIEW_R1 — R35-T03 (independent, fresh-context reviewer; read full diff 120677e6..8d2bcec4)
Verdict: APPROVE_WITH_FIXES.
- F1 [P2] splitInstrumentTokens dropped INTERNAL whitespace ("OLI TIRS"→"olitirs") re-breaking
  record/item parity for multi-word instruments. → FIXED b3e4b7c (edge-trim per segment) + tests.
- F2 [P3] strictNonNegativeInteger accepted exactly 2^63 (double rounding of qint64max) → wraps
  negative on qint64 encode, breaking round-trip. → FIXED (>=2^63 refused) + test.
- F3 [P3] dir-visit cap coupling undocumented. → documented in CatalogServiceOptions::localMaxFiles.
- F4 [P3] depth-cap truncation not surfaced though comment claimed it. → FIXED (walkTruncated set).
- F5 [P3] per-directory listing bound not stated. → documented in walk comment.
- F6 [P3] skip-counter bucket shift (walk-time vs read-time). → NO-CHANGE (internal metadata-only counters).
- F7 [P3] NaN/Inf raw value into jsoncpp details. → FIXED (value omitted).
- F8 [P3] BFS vector erase(begin()) O(n²). → FIXED (head index).
- F9 [P3] directory-read path untested (fake.json filtered as dir). → FIXED (catalog.json-as-directory case).
- F10 [P3] WP11 replace test Windows rename semantics. → FIXED (POSIX guard).
- F11 [P3] multi-lexical-path duplicates via symlink loops (same as old scan aliasing). → NO-CHANGE, documented.
- F12 [P3] defensive dead guards (gcount()<0, tilesX>INT_MAX unreachable). → NO-CHANGE kept (harmless).
Verified sound by reviewer: finite-admission coverage, int64 math and narrowing sites, walk
termination/break-at-cap/order-neutrality, inspectOpenRaster fidelity (same flags/options as
inspectRaster; all slice() callers in-grid), manifest producer compatibility (all in-repo writers
emit integral revisions), test fixture validity (STAC shape, href resolution lexical, RasterWriter API).
