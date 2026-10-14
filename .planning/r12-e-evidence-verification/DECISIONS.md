# R12-E Decisions

## D8 — preflight digest now rides the RFC 8785 authority (digest bytes change)
`canonicalReportJson` / `computeRequestDigest` delegate to `sicnu::data::canonicalizeJsonRfc8785` via a new PUBLIC link `sicnu_preflight → sicnu_data` (verified acyclic: data links Qt6/GDAL/SQLite3/runtime/geospatial/Platform, none link preflight; root CMakeLists orders data before preflight). Prior bytes used a private jsoncpp writer at precision=12. Audit + re-grep found **no persisted/golden pins and no non-test consumer recomputing these digests**, so no migration shim; replay/invariance tests hold under both serializers. `src/preflight/report.h`, `src/preflight/sha256.h` doc comments updated to match.

## D16 — parity gate over SHA-256 forks; no unification
Four per-leaf forks (verify/grader/preflight/faultlab) are full-read byte-equivalent FIPS 180-4. Unification rejected: faultlab is a deliberately jsoncpp-only leaf, module CMake guards whitelist deps (verify/grader FATAL_ERROR guards), touching 4 modules + ADRs 0172/0174 for zero behavioral gain violates minimal-change doctrine. Gate: `test_verify_chain_locale_matrix` asserts byte-identical digests over a 14-entry boundary corpus + NIST KATs + grader streaming chunk-splits + locale sweep. Repo-wide ~9 SHA-256 copies exist (repair_planner/planner/lab/geospatial are B/C/D surfaces) — noted for the owning directions, not consolidated here.

## Canonical JSON: two authorities, never cross-compared
faultlab's jsoncpp-leaf canonicalizer (%.17g reals, UTF-8 key order, exact int64) and the Qt RFC 8785 canonicalizer (shortest-round-trip reals, UTF-16 key order, ≥2^53 narrowing) produce different bytes for the same document. They feed disjoint digest domains (fault reports vs execution fingerprints). Aligning them would reseal historical faultlab digests — forbidden by the directive. Documented; the parity gate pins each authority's own stability instead.

## verify vs verification verdict-class divergence — recorded, not changed
Identical NaN-metric facts yield Fail/`VERIFY.NUMERIC_NOT_FINITE` (verification) vs Indeterminate/`verify:i_metric_not_finite` (verify). The two lattices are deliberately parallel (`verify_status.h:13-17`); harmonizing is an owner-level semantic ruling, not a defect fix. Also recorded: `verification::VerificationReport::fromJson` is lenient (trusts `reportDigest` tamper evidence) where the verify side recomputes and refuses contradiction.

## D10 — coverage gaps surface as VERIFY.SPEC_INVALID
`buildReport` now folds node-unmapped check results into the task outcome worst-wins and attaches the existing closed-vocabulary code `VERIFY.SPEC_INVALID` (caller-side map defect, ReplanClass::Abort). No new error code was added.

## D7 — mirror depth validation is load-order independent
End-of-`addDocument` validation resolves extends chains across all loaded entries; over-deep or cyclic chains mark the mirror unhealthy (typed problems, queries → Unavailable). Dangling parents remain benign bounded stops (existing forward-reference contract kept).

## Explain validator guidance cross-check — documented as structural-only
`explanation_validator.h` previously promised guidance-based source-reference verification that was never implemented. Header now describes reality (structural checks only); implementing the semantic cross-check is a follow-up, not this direction's scope.

## Not fixed (recorded follow-ups, out of R12-E scope or need owner ruling)
- verify_adapters: no path-containment/symlink policy; GDAL remote (`/vsicurl/`) open without timeout — adapters are deliberately dumb, no production wiring yet. Needs an engine-level policy decision before implementation.
- grader adapters return `{}` vs partial items on different failure paths (cosmetic; error set in all paths).
- preflight sha256 fork builds a padded copy (2× memory) — perf nit at current input sizes.
- explanation builder multi-artifact citation (first artifact only), GUI runId-from-filename — display-accuracy nits in a constructed (non-forged) pipeline.
- Cross-domain observations forwarded: radiometric vocabulary SAR tokens (R12-A/R12-C IR seam), workflow checkpoint bounded reader (R12-C, claimed fixed), help/science_context untyped-JSON sites (R12-C/D), teaching_admin items (R12-D), lab_pack CRLF-normalized digests (R12-C).

## F10 benchmark linkage
`test_fault_matrix` links `sicnu_faultlab` (recorded exception #2 in OWNERSHIP.md) instead of compiling faultlab sources into the test TU — the source-include route duplicated file-local helpers and escaped the src/ include discipline.
