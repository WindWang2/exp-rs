# GOAL — D3 · Lab Content Expansion（时序 / SAR / 高光谱 / 制图出图）

Four new undergraduate labs on the rs: operator platform (111 operators, only 7 labs today):

- **D3a** 时序: NDVI time series → temporal_trend / phenology-context / temporal_anomaly
- **D3b** SAR: speckle suppression + SAR change detection; teach fixed bugs #785 (look azimuth) & #803 (multiband NoData)
- **D3c** 高光谱: MNF → PPI endmember extraction → SAM/SID matching → linear unmixing
- **D3d** 制图出图: compose any prior result into a compliant thematic map via MapSpec 3.0 (legend/scalebar/north arrow/source note); declare dependency on D10 fixing `test_mapspec`

Each lab ships: LabSpec (D2 format) + 中文原理讲解 + data requirement spec (`data/labs/data-specs/*.json` for D1) + grading assertion intent (`data/labs/grading/<lab_id>.intent.json`) + 思考题 + headless-executable pipeline.

## Hard constraints (from track GOAL, verbatim compliance)

- Branch `zcode/lab-content-expansion`, worktree `../exp-rs-lab-content-expansion`, base origin/master.
- `master` read-only. No changes under `src/operators/` — operator gaps go to `ISSUES.md`.
- ≤2 subagents at any moment, read-only (research in Phase 0, adversarial review in Phase 6). Main agent does 100% of implementation.
- No CI: local evidence only → EVIDENCE.md.
- Build: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, Ninja `-j2` (drop to `-j1` if RSS > 70% or load > 1.5× cores); log CPU/RSS every 60 s during long builds.
- Tests: `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` first.
- Temporal data cap: ≤12 epochs, ≤512×512/epoch, ≤8-bit-scaled float32.
- Temp fixtures under `data/labs/_tmp/` (gitignored), deleted before final commit; never commit generated rasters.
- Grading intent declared here, grader implemented by D4 (`grading_ref` in LabSpec).
- Completion gate: 4 LabSpecs validate; all `operator_id`s resolve in Processing Registry; 4 pipelines run headless offscreen with outputs within declared tolerance; 中文 docs zero-drift vs LabSpec; P0=P1=0 in review; diff confined to `data/labs/`, `docs/labs/`, `scripts/`, `tests/` (new lab-chain tests only).
- PR created, NOT merged.

## Phase order

0. Baseline audit (operator surface × 4 themes, capability matrix) — this dir: BASELINE.md
1. D3a temporal lab
2. D3b SAR lab
3. D3c hyperspectral lab
4. D3d cartography lab + data-spec summary
5. Headless verification of 4 chains + docs generation/审校
6. Adversarial review (2 read-only subagents: scientific-correctness + pedagogy)
7. Remediation P0/P1 + actionable P2
8. Rebase latest master, docs sync, PR

## Files

PLAN.md · MILESTONES.md · DECISIONS.md · EVIDENCE.md · REVIEW_LOG.md · PR_BODY.md · BASELINE.md
