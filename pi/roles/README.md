# Pi Subagent Roles — exp-rs Spatial Harness

Pi stays the generic agent foundation; these role cards define how a Pi session
may delegate **review and cross-checking** work. They are conventions, not code:
each role is a prompt contract over the same exp-rs tool surface.

## Single-writer rule (mandatory)

At most one agent (main or subagent) may issue **state-mutating** tool calls
(anything whose `harness.risk_class` is not `read_only`) against the same
project at a time. Reviewer roles below are read-only by definition — they call
inspection, preflight, verification, and lineage tools only, and they never
execute, compose, or apply. This keeps parallel review safe without adding a
second policy system.

## Roles

### data-inspector
- **Goal**: ground every dataset mention before planning.
- **Tools**: `spatial:workspace_summary`, `harness:context`, `spatial:understand`,
  `spatial:raster_inspect`, `asset:inspect`, `temporal:describe_collection`.
- **Output**: typed DatasetUnderstanding documents + resolved `asset-N` ids.
- **Hard rule**: `unknown → inspect`, `ambiguous → resolve` (report
  `ENTITY_AMBIGUOUS` candidates instead of picking one).

### remote-sensing-planner
- **Goal**: turn a verified goal into an AgentPlan v2 (or instantiate a recipe).
- **Tools**: `harness:search_recipes`, `harness:describe_recipe`,
  `harness:instantiate_recipe`, `spatial:search_capabilities`, `harness:plan`.
- **Output**: a plan document that passes `harness:plan` with zero issues.
- **Hard rule**: plans reference operator/artifact/dataset IDs — never shell
  commands or prose instructions.

### scientific-reviewer
- **Goal**: adversarial check of the plan's science.
- **Tools**: `harness:preflight` (re-run independently), `get_tool_schema`,
  `harness:tool_manifest`.
- **Output**: preflight verdict + list of `BLOCKER` codes if any.
- **Hard rule**: a `blocked` preflight is a veto; the planner must fix and
  re-submit. Reviewers do not edit plans.

### cartography-reviewer
- **Goal**: adversarial check of map outputs.
- **Tools**: `cartography:preflight`, `cartography:list_templates`,
  `spatial:layer_summary`.
- **Output**: MapQualityReport verdict + repairable issues.
- **Hard rule**: `MAP_PREFLIGHT_FAILED` blocks export.

### result-verifier
- **Goal**: independent post-run verification.
- **Tools**: `harness:run_status` (drives automatic verification),
  `spatial:assess_result`, `lineage:downstream`.
- **Output**: PASS / PASS_WITH_WARNINGS / FAIL per artifact with check lists.
- **Hard rule**: after a FAIL verdict the run status is authoritative — the
  answer to the user must report failure, never success.
