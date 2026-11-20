# ACCEPTANCE — R35-T03 (per-WP completion states at final HEAD 62d4355)
- WP01 query finite admission: DONE — CatalogQuery+AssetQuery NaN/Inf typed refusal; legal 6-value bbox kept; tests 1 case (io).
- WP02 instrument token parity: DONE — exact-token edge-trimmed matching; MSI↔MSI2 split; multi-word parity; tests 2 cases.
- WP03 temporal boundary docs: ALREADY_DONE_WITH_EVIDENCE — both public contracts document their boundary (fabric half-open
  at catalog_service.cpp recordMatchesTemporal comment; asset_query instantInRange inclusive) — no change made by decision D8.
- WP04 bounded JSON read: DONE — actual read carries the cap (stat+1 buffer, growth refusal, read-error vs invalid-JSON typed split); tests 1 case.
- WP05 bounded directory walk: DONE — per-dir VSIReadDir BFS, depth/file/dir-visit caps, truncation via truncatedByCap; symlink-loop bounded; tests 2 cases.
- WP10 checked tile math: DONE — clamp/slice/plan/readBlock int64 + representability; out-of-grid typed; coverage-once test; INT_MAX pure-plan.
- WP11 single-open metadata: DONE — inspectOpenRaster; reader handle+metadata same generation; A/B replace barrier test (POSIX).
- WP16 strict manifest decode: DONE — wrong-type refusal with stable diagnostics; revision 0 legal; 2^63 bound; absent optionals legal; 3 test cases.
- WP06-09, WP12-15, WP17-20: NOT_STARTED this PR — recorded as the remaining backlog of the 20-WP track plan (see PLAN stub);
  the brief's priority set (WP01-05, 10-11, 16) is fully covered.

Local verification summary: new 11/11 CTest ×2 runs; 10 affected suites PASS (numbers in PR_BODY);
r15_B r403 SSRF failure attributed pre-existing via base-revert rebuild experiment.
