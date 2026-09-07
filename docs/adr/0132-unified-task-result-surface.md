# ADR 0132: Unified Task & Result Surface (UX 4.0, Milestones D/E)

- Status: Accepted (Desktop Workbench & Unified UX 4.0 goal)
- Context: the canonical task list (`RsJobPanel`) rendered pipeline children
  flat, so a DAG run looked like unrelated jobs; the only surface that ever
  grouped them was the dead `TaskCenterDock`. Results had no shared view —
  the job panel showed raw JSON, dialogs hand-rolled localized summaries, and
  the 工作区治理 panel (the governance "Results" home) was inert. Data and
  Workspace panels exposed overlapping concepts with no cross-navigation.
- Decision:
  1. **Grouped pipelines**: RsJobPanel nests pipeline steps under their
     parent task row (`parentTaskIds`), parents expanded by default, newest
     first; incremental inserts attach under an existing parent row. The
     #704 incremental-log and throttled-detail contracts are untouched.
  2. **One result renderer**: `RsResultSummary`
     (src/app/widgets/rs_result_summary.cpp) renders operator result JSON —
     status/context line (operator, elapsed, cache), bounded key metrics,
     warnings, output artifacts with double-click open-on-map, collapsible
     raw JSON. Embedded in `TaskPanelHost` and the new RsJobPanel 结果 tab.
     Deletion of the dead TaskCenterDock freed its grouping logic for porting
     (now in RsJobPanel).
  3. **Concept contract**: 数据管理 = Data (inputs), 工作区治理 = Results &
     governance (results/runs/datasets/experiments). Governance rows open on
     the map through a path signal routed via `ActiveViewHost`/`loadRasterLayer`
     (Data/Display seam preserved; the panel never touches the canvas).
  4. **Stable identities**: context actions act on governance entity ids /
     asset ids (`WorkspaceGovernanceModel::entityId`), never row positions.
- Consequences: a pipeline run reads as one unit; every completed task can be
  read as a result document rather than JSON; Data vs Results are
  distinguishable by surface; dialogs keep their localized domain summaries
  (QA-mask stats, transition matrix) built on the ADR 0108 seam — the shared
  renderer serves hosts that would otherwise show raw JSON only.
