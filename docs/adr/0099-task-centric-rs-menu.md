# ADR 0099: Task-Centric "遥感" Menu (C5)

## Context

The mission's C5 / P1 asks for a task-centric surface: "The application should
not force users to understand backend provider names... Above [the Processing
Toolbox], provide domain-oriented workflows. Prefer entry points such as
Import Remote Sensing Product, Preprocess Imagery, Spectral Analysis,
Classification, Change Detection, Image Fusion, Terrain Analysis". The
capabilities existed behind the 栅格/分析 menus, but there was no single
domain-oriented entry point that grouped the complete optical workflow.

## Decision

Add a top-level **遥感(&S)** menu (between 分析 and 矢量) grouping the domain
workflows into two sections plus a DAG shortcut:

- **产品与预处理**: 导入遥感产品（auto product import）· 辐射定标 · QA 掩膜 ·
  应用掩膜 · 大气校正 · 正射纠正 (RPC/GCP);
- **分析**: 光谱指数 · 光谱分析（光谱库匹配 / ROI 均值谱）· 变化检测 · 影像融合 ·
  地形分析;
- a separator row: **预处理工作流 (DAG)** opening `lab.preprocess.optical`
  through the TaskPanel host.

Every entry reuses an existing dialog/slot or operator — no new algorithm
code, no Processing Registry bypass; the menu is a pure presentation layer.
Icons are drawn from the existing `:icons` resource (i_ort, workflow, ...).

## Consequences

- The complete optical workflow is now reachable from one domain menu in a
  professional order (import → preprocess → analyze), while expert surfaces
  (栅格/分析 menus, Processing Toolbox) remain for fine-grained access.
- The menu wires to proven slots and compiles into the app target; like the
  other menu groups it is verified by build, not by a headless UI test
  (consistent with the project's existing menu-testing posture).
