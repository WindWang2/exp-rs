# 0005 Task Center Dock DAG and Priority Visualization Architecture

We decided to render DAG pipelines as hierarchical tree items with parent-child indentation and color-coded Priority badges (`[高]`, `[中]`, `[低]`) in `TaskCenterDock`.

### Context & Decision
To make multi-stage task pipelines and priorities visually clear at a glance:
1. **Tree Hierarchy Indentation**: Parent tasks appear as top-level `QTreeWidgetItem` entries; child tasks are added as child items under their parent node.
2. **Priority Column & Color Badges**: A dedicated "Priority" column displays `[高]` (Red), `[中]` (Orange), and `[低]` (Blue) text styling.
