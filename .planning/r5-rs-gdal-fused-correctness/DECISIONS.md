# R5 Track 02 — Decisions

## D1 — #1356 SEGFAULT root cause: TEST harness UAF, not GDAL, not the fused executor

The stable SIGSEGV in "fused NDVI→threshold bit-identical" is a use-after-scope in
`tests/test_fused_chain.cpp` itself: the fixture constructed
`QCoreApplication(argc, argv)` with **stack-local** `argc`/`argv`. QCoreApplication
stores the `int&` / `char**` by POINTER (it never copies), so after the fixture
constructor returned, every later `applicationFilePath()` dereferenced dead stack.

Trigger chain (gdb backtrace, deterministic):
`ChangeDetection::changeMask` SICNU_LOG_INFO → `QgsMessageLog::logMessage`
→ `QgsApplication::members()` (first lazy `ApplicationMembers` construction)
→ `QgsLocalizedDataPathRegistry` → `QgsSettings` → `QLibraryInfo::path`
→ `QCoreApplication::applicationFilePath()` → wild read of the dead `argv`
array → SIGSEGV.

Why it looked like a "GDAL 3.13 numeric/driver interaction": the crash fired on
the first deep Qt path AFTER two GDAL operators ran, inside the fused-chain test
case; actually the fused executor never got to run — the unfused reference chain
crashed first. GDAL version is irrelevant.

Verdict against the issue's fork: neither a GDAL regression nor a fused-chain
UAF — a test-side violation of the repo's own `static argc/argv` convention
(every other test file uses `static`; grep shows only test_fused_chain.cpp and
test_data_manager_reap.cpp deviated; both fixed).

Fix: make argc/argv/arg0 `static` in both construction sites (matches
test_agent_canvas_sync.cpp et al.). Regression = the test itself (it crashed
deterministically before; green after).

## D2 — Master configure break fixed in this branch (cross-track impact flagged)

origin/master `a726d17a6` does not configure with `ENABLE_TESTS=ON`:
merge commit `7bb6398c0` committed an unresolved conflict marker (`=======` at
tests/CMakeLists.txt:14728 and a glued comment at :14543). Every parallel R5
track hitting this needs the same 2-line restore. Fixed here surgically (both
parent sides' content verified intact; nothing dropped).

## D3 — GDAL version matrix scope: isolation evidence instead of 3.10/3.13 matrix

The machine has exactly one GDAL (3.13.3, user-local SDK root; no docker/podman,
no system gdal). Since the root cause is proven repo-side UB (D1), a GDAL
version matrix cannot change the outcome; the guard is the static-storage
pattern + the regression test, and the PR documents this as the "version
isolation evidence" the issue asked for.

## D4 — Adjacent audit fixes (R5 NoData audit; scope = existing contracts only)

Subagent audit (1.94M tokens) produced a 10-row matrix against the #1340
defect families. Fixed in this track (all minimal, contract-preserving):

1. rs:sar_coregister (global): declare the NaN resampling voids (P1; the local
   variant already declared — one-line mirror).
2. rs:sar_interferogram: declare NaN on the interferogram band (parity with
   the coherence band; f4a19336e family).
3./4. image_enhancement filter/speckle: declared sentinels are NaN-washed per
   band before windowed kernels (`streamBandWindowed` gained an optional
   `noData` parameter; default keeps historical behavior), so sentinels can
   neither enter mean/gaussian/median/Lee/Frost/Kuan windows nor be rewritten
   as data on an output that declares NaN voids.
5. rs:modis_georeference: the sinusoidal copy keeps per-band NoData
   declarations (previously dropped, silently feeding the warp an
   undeclared source), and warpToCrs pins an explicit -srcnodata/-dstnodata
   contract (uniform declaration → pin; undeclared float → -dstnodata nan;
   integer/mixed → GDAL per-band default).
6. rs:obia_segment OTB adapter: `relabelAllVoidSegments` — segments whose
   pixels are ALL declared void relabel to 0 (ADR 0054 nodata label); mixed
   segments stay (features layer masks per-pixel already).
7. WarpOptions (geospatial/convert) gained optional sourceNodata/targetNodata
   lists; rs:resample/rs:align populate them (declared-everywhere → pin both;
   undeclared float → declared NaN voids; integer/mixed → default).
8. rs:register_images: warp.noDataValue = the source's declared sentinel
   (closes the "tracked as backlog" comment), output declares the same.
9. rs:obia_segment(simple)/classify(quantize): per-band sentinel wash instead
   of band-1-only (the #803 family).
10. rs:spectral_similarity: per-band sentinel wash (heterogeneous declarations
    all honored; undeclared bands keep the documented -9999 fallback), kernel
    receives NaN and voids on its own non-finite rule.

Deferred with reasons: none of the 10; io_operators' warpRaster call sites
(3) keep GDAL-default behavior — io: surface is #1338's closure area, flagged
for its owners in the PR.

## D5 — Fused tail parity (payload + metadata)

The fused threshold adapter now reports maskedPixels/totalPixels/
maskedPercent (accumulated by a tail-plane observer in the consumer thread),
float-space thresholdUsed, and stamps SICNU_CHANGE_METHOD/SICNU_CHANGE_
THRESHOLD metadata on the output — previously the fused output silently
lacked both the counts payload and the metadata items the unfused operator
writes. Bit-identical scope extended accordingly (test asserts equality of
all of them against the real operator chain).

## D6 — Sanitizer evidence route

valgrind is not installed and the CachyOS ld.so is stripped with no matching
debug package (both the system mirror's valgrind startup check and the Arch
debug package build-id mismatch were tried). Evidence comes from a scoped
ASan+UBSan build (`build-sanitizer`, ENABLE_SANITIZERS=ON, repo preset) of
test_fused_chain; the pre-fix deterministic SIGSEGV + gdb backtrace and the
post-fix green run are recorded in EVIDENCE.md.

## D7 — Independent review round (fresh-eyes subagent, PASS-with-issues) and dispositions

Reviewer verdict: PASS-with-issues — 0 Blocker / 2 High / 4 Medium / 4 Low / 3 Nit.

Fixed in this round:
- High-1 (family sweep incomplete): the remaining stack-local argc/argv app
  sites are fixed — test_gui_job_adapter.cpp (3 sites) and
  test_execution_plane.cpp (1 site); the earlier commit message's incorrect
  "only two files deviated" claim is superseded by the reordered commit's
  message (4 files / 8 sites, static-review verified).
- High-2 (nodata serialization precision): warpRaster now serializes nodata
  via std::to_chars (locale-independent, round-trip exact) instead of
  std::to_string's %-format; fractional sentinels can no longer be rounded
  into mask mismatches.
- Medium-3 (mixed-declaration float rasters): applyWarpNodataContract no
  longer pins -dstnodata nan over partially declared inputs — mixed
  declarations keep GDAL's per-band default, matching warpToCrs's policy.
- Medium-4 (red intermediate commits): history reordered — every fix commit
  now precedes the commits that consume it; test-parity assertions land with
  their implementation; the r5 suite commit is last. HEAD tree is identical
  to the reviewed tree (diff verified empty).
- Medium-6 (missing undeclared-align regression): new pin case — undeclared
  float align pads with NaN and declares NaN, zero-pad count asserted.
- Low-7 (single-use plan): documented on FusedStage::postRunExtras.
- Low-8 (relabel copies labels): the rewrite is skipped when no segment is
  dead (copy happens only when a relabel actually occurs).
- Low-9/Low-10 (comment accuracy / pin-vs-guard): VRT comments now quote the
  observed GDAL single-nodata behavior; the suite header labels which cases
  pin fixes vs guard contracts.
- Nit-11: log-call indentation fixed.
- Medium-5: EVIDENCE.md lands with the completed sanitizer results (this
  branch, final commit) — see D6/EVIDENCE.md §5.

Accepted / documented, not code changes:
- Nit-12 (NaN-declared source in register_images keeps -9999 output
  declaration): output fill and declaration remain mutually consistent;
  changing the fill value would alter pixels. Noted as future polish.
- Nit-13 (commit message wording): clarified in the reordered commit and
  this PR body — the Float32 dtype conversion of the MODIS temp copy is by
  design and remains.
