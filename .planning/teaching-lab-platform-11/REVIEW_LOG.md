# REVIEW_LOG

## Self review (Phase 7, full diff origin/master...HEAD)
Continuous during development; caught before the independent review:
LabDataPackResult.ok flag never set; std::optional-of-incomplete-type (MSVC);
usageError ref/ptr mismatches; QSet::insert API misuse; classificationForced
scoping; positional band indexing (readWindow OOB — found via cdb stack);
binary-mode detection ignoring max_false_alarm_rate-only assertions; SAM mode
logic; roster header line; missing submissions in the realGrade scale test.

## Independent adversarial review (read-only subagent, 2026-09-16)

Scope: full `git diff origin/master...HEAD`. Verdict: **P0=0 P1=3**.

## Findings & dispositions

| id | severity | finding | disposition | commit/test |
|---|---|---|---|---|
| R1 | P1 | grading_corpus.pack.json pinned a stale wrong_answer_corpus.json checksum (self-check would fail out of the box) | FIXED — gen_lab_packs.py rerun; drift gates green | review-fixes commit |
| R2 | P1 | series_separation slope divided by band count (unweighted mean/sxx vs pooled per-pixel OLS) | FIXED — weighted pooled regression (xw/sxx weighted by n_b); regression test asserts slope 2.0 on a synthetic 4-band stack | review-fixes commit; test_lab_grader_kernels series_separation case |
| R3 | P1 | spectral_signature OOB: reference spectrum length vs bands.size() unchecked for non-first references; zone_reference index unchecked | FIXED — validator enforces 1:1 spectrum length and index range | review-fixes commit |
| R4 | P2 | mis-typed rules fields (zone/min/path as wrong JSON types) reach raw asInt/asDouble → uncaught Json::LogicError kills the CLI | FIXED — validateLabKernelParams type-checks bounds/zone_expected/separations/x/zones_nodata/band/zone/file_check path | review-fixes commit |
| R5 | P2 | non-array `bounds`/`zone_expected` iterate as empty → vacuous full-weight pass | FIXED — isArray enforced | review-fixes commit |
| R6 | P2 | LabDataPack::load threw Json::LogicError on non-string schema_version/generator/notes/provenance (contract: never throws) | FIXED — isString() guards with typed failures | review-fixes commit |
| R7 | P2 | bare "execute" signal misroutes English concept questions ("how do I execute step 2?") to refusal | FIXED — replaced with "execute step"/"execute the lab" phrases | review-fixes commit |
| R8 | P2 | doc drift: --grade vs --grade-transcript; fisher `zone` param undocumented/unvalidated; --out vs --report-out message | FIXED — docs corrected; fisher validator requires `zone`; runLabReport message names --report-out | review-fixes commit |
| R9 | P3 | file_check sibling path could escape the artifact directory (information reflection) | FIXED — canonical-path containment check, graded failure when outside | review-fixes commit |
| R10 | P3 | batch summary exclusion case-sensitive on Windows | FIXED — lower-cased comparison under Q_OS_WIN | review-fixes commit |
| R11 | P3 | self-check pack load errors overwrote one evidence key | FIXED — keyed by file name | review-fixes commit |
| R12 | P3 | zone_stats band < 1 reached readWindow as artifact-class error instead of usage | FIXED — typed usage error | review-fixes commit |
| R13 | P3 | ENL/SAM test assertions too weak (object-presence only) | FIXED — closed-form assertions: ENL(raw)=25, ratio bound, mean_shift; SAM exact 0° and closed-form rotated angle | review-fixes commit |

## Clean (explicitly verified by the reviewer)

- Report redaction: LabGradeEmbedding::toJson applies deepRedactSecretKeys to
  the inline copy; the CLI test plants and greps a secret across all three
  renderings.
- Raster seam for the 6 pre-existing rules labs is behavior-identical; CSV
  schema unchanged for old callers; batch atomicity/flush/cancel semantics as
  documented.

## Pre-existing (documented, out of scope)

- test_labspec id-regex failure: D16's lab8_temporal_analysis.lab.json
  (id temporal_phenology_timeline) vs D18's stricter id check — both files
  untouched by this branch.
- test_labspec docs probe: gen_lab_docs.py TypeError on the same D16 file +
  `python3` interpreter name absent on this host.

## Post-fix verification

Full targeted matrix rerun twice after fixes + rebase (results in EVIDENCE.md).
