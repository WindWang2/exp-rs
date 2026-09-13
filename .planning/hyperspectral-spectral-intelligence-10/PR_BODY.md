# PR BODY — hyperspectral-spectral-intelligence-10

> Final numbers to be refreshed at Phase 9 against the final HEAD; structure
> fixed here so the review delta is visible.

## Baseline
origin/master `7d78059d1a6d316d606656759a506d17bc5e3b55` (2026-09-13, PR #958 merge).
Worktree `../exp-rs-hyperspectral-spectral-intelligence-10`, branch
`zcode/hyperspectral-spectral-intelligence-10`.

## Architecture
ADR 0148 (docs/adr/0148-spectral-intelligence-platform.md):
typed spectral-table artifacts riding the existing path/string placeholder
contract; one shared reference seam; complete streaming MNF chain with an
exposed transform model; FCLS; band-select/library-select composition.
Design decisions with options-considered: .planning/.../DECISIONS.md (D-1..D-12).

## Major deliverables
1. `SpectralTable` domain (digest, provenance/license rule, bounds) + wavelength grid helpers.
2. Shared reference seam: `refsRef`/`endmembersRef`/`targetRef`/`libraryPath`(+`libraryMaterials`) on rs:sam_classify, rs:spectral_unmixing, rs:matched_filter, rs:ace; wavelength reconciliation with typed refusals; provenance echo.
3. `rs:endmember_extraction` `endmembersOut` artifact + payload port; WorkflowRuntime payload-port recording (session/TaskCenter parity).
4. `mnf_transform` streaming kernel + `rs:mnf` rewrite (`transformOut`) + `rs:mnf_inverse` (raster + spectrum modes, dropped-mass errorOut).
5. `SpectralUnmixing::unmixFcls` (NNLS, sum-to-one, collinearity/zero-norm refusals) + `method` param.
6. `rs:spectral_band_select`, `rs:library_select`.

## Compatibility
Inline-array inputs unchanged and still the required-by-seam default; new
params optional; placeholder grammar/TaskCenter untouched; legacy
`ImageEnhancement::mnf/processMnfFile` untouched; `rs:mnf` results switch to
the new kernel (invariants pinned: SNR ordering, roundtrip, singular
refusal — not eigenvector signs); algorithm_meta sidecar set unchanged
(new operators declare no taskFamily, D-11).

## Tests
Local evidence only; no online CI dependency. All commands in
.planning/.../EVIDENCE.md. 9 test binaries green at review baseline
(~65,900 assertions across the spectral + workflow + operator families),
including: MNF roundtrip/SNR/singular-refusal/256-band scale; FCLS known
answers; digest/tamper/license-rule; PPI→unmix→SAM single-workflow
acceptance; CLI `--list`/`--schema`; algorithm-meta drift gate.

## Performance/resource
`rs:mnf` drops FullRaster (~4× raster) to row-streaming O(row·bands + bands²);
256-band logical-cube case is the scale evidence; FCLS scaling documented in
PERFORMANCE.md; no wall-clock gates added.

## Review findings
Populated from REVIEW_LOG.md at Phase 7 (two read-only subagents:
architecture/science, performance/concurrency/test-trust).

## Known limitations
- Component subsets in MNF inverse are a documented approximation
  (quantified via errorOut / reconstructionError).
- Transform artifact band bound 1024; spectral-table bound 4 Mi cells.
- FCLS sum-to-one is penalty-based (~1e-6, reported as QA).
- New operators carry no `task` capability families yet (sidecar regeneration
  deferred to a dedicated PR per D-11).

## Follow-ups
- Local/dual-window RX variants, sparse unmixing, SID-SAM hybrid (recorded
  non-goals, DECISIONS D-7).
- Promote new operators into capability task families + sidecar export.
- GUI spectral workbench consuming artifacts from the library selectors.
