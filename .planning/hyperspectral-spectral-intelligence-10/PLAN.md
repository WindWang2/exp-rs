# PLAN — hyperspectral-spectral-intelligence-10

Mission: turn the existing spectral kernel stack + library domain into a
composable Spectral Intelligence Platform: typed artifacts flow through
workflows (PPI → MNF-space → SAM/SID/unmixing), libraries are first-class
operator inputs, MNF is a complete invertible chain, unmixing is numerically
guarded with a true FCLS option.

## Work packages

| ID | Package | Key deliverables |
|---|---|---|
| WP-A | Spectral table domain + wavelength helper | `spectral_table.{h,cpp}` (typed load/save/validate/digest/bounds), `spectral_wavelength.{h,cpp}` (grid read+normalize+typed refusals), `tests/test_spectral_table.cpp` |
| WP-B | Artifact flow (H-2) | `rs:endmember_extraction.endmembersOut` + payload `endmembersArtifact`; `rs_spectral_reference_input.{h,cpp}` seam; `refsRef`/`endmembersRef`/`targetRef` on sam/unmix/mf/ace; pipeline test PPI→unmix+SAM via placeholders |
| WP-C | Library as first-class input (H-1) | `libraryPath` + `libraryMaterials` on sam/unmix through the WP-B seam (library sniffing + filters + wavelength reconciliation + measured/synthetic labeling + provenance echo) |
| WP-D | MNF chain (H-3) | `mnf_transform.{h,cpp}` streaming kernel (model, forward, inverse, SNR, singular refusals); `rs:mnf` rewrite + `transformOut`; `rs:mnf_inverse` (raster + single-spectrum conversion); known-answer roundtrip; reconstruction error |
| WP-E | FCLS + guards (F) | `method=fcls` (NNLS + sum-to-one), collinearity/zero-norm refusals, abundance-sum QA in payload |
| WP-F | Preprocessing gaps (E) | `rs:spectral_band_select` (wavelength ranges / indices / bad bands; unit normalization; metadata propagation) |
| WP-G | Library selection operator (G) | `rs:library_select` (material/wavelength/sensor projection → artifact; near-duplicate QA) |
| WP-H | Integration + scale + edge + review + PR | sidecars, CLI/MCP/registry verification, scale evidence (256-band synthetic), edge matrix, 2 read-only subagent reviews, final verification, PR |

## Execution order

WP-A → WP-B → WP-C → WP-D → WP-E → WP-F → WP-G → WP-H.
(C depends on B's seam; D is independent of B/C but shares the artifact
vocabulary; E/F/G are small relative to A–D.)

## Phase mapping (goal brief §二)

Phase 0 done (baseline/planning). Phase 1 = ARCHITECTURE.md + DECISIONS.md
(this + prior files). Phase 2 = WP-A. Phase 3 = WP-B..G. Phase 4 = WP-H
integration. Phase 5 = scale/numerics. Phase 6 = edge matrix. Phase 7 =
review (2 read-only subagents). Phase 8 = final verification. Phase 9 = PR.

## Completion gate

1. `ctest -R "test_spectral_table|test_endmember|test_sam|test_spectral_unmixing|test_mnf" -j1` → all pass (targeted set grows per WP).
2. Pipeline known-answer: one WorkflowDefinition JSON runs PPI → SAM + unmixing with placeholder-bound artifacts; test asserts abundance/labels against synthetic simplex ground truth.
3. `rs:mnf_inverse` known-answer: roundtrip on synthetic cube ≤ 1e-5 relative; truncated reconstruction error reported and bounded.
4. Library input: SAM with `libraryPath` + material filter matches inline-refs run on the resampled entries (cross-checked).
5. FCLS: pure-pixel known answer exact-to-tolerance; collinear set refuses typed.
6. Provenance/license machine-check: loader refuses measured tables without license (test asserts the refusal).
7. All of the above re-run at final HEAD (Phase 8), outputs in EVIDENCE.md.
