# PLAN — lab-content-expansion

Owner scope: `data/labs/`, `docs/labs/`, `data/labs/data-specs/`. Out of scope: `src/operators/`.
Diff budget: `data/labs/`, `docs/labs/`, `scripts/`, `tests/` (lab-chain tests only).

## Phase 0 — Baseline audit (in_progress)
- [ ] Infra: pipeline runner CLI, pipeline_schema.json, Processing Registry resolution
- [ ] LabSpec v1 schema draft (D2 absent — see DECISIONS D002)
- [ ] Capability matrix × 4 themes (subagent research + own verification)
- Output: BASELINE.md

## Phase 1 — D3a temporal
## Phase 2 — D3b SAR
## Phase 3 — D3c hyperspectral
## Phase 4 — D3d cartography + F data-spec summary
## Phase 5 — E headless runs + G docs
## Phase 6 — adversarial review (2 read-only)
## Phase 7 — remediation
## Phase 8 — rebase + PR

Per-lab artifact checklist (repeat × 4):
- [ ] `data/labs/<lab_id>.labspec.json` (validates vs labspec.schema.v1)
- [ ] `data/labs/pipelines/<lab_id>.pipeline.json` (headless-runnable)
- [ ] `data/labs/data-specs/<lab_id>.json` (D1 handoff, offline)
- [ ] `data/labs/grading/<lab_id>.intent.json` (assertion intent only)
- [ ] `docs/labs/lab<N>_<slug>.md` (中文: 目的/原理/步骤/预期结果/思考题; zero drift vs LabSpec)
- [ ] operator_ids all resolve in Processing Registry
