# Step-explanation guidance corpus (RS14-15)

Authored teaching guidance for the explainable-workflow layer
(`src/explain`, ADR: step explanations `exp.step_explanation.v1`).

- **What this is**: the human-written curriculum layer — narrative purpose,
  per-parameter "why this value" rationale, skip consequences and literature
  references — loaded by `sicnu::explain::GuidanceStore`
  (`exp.step_guidance.v1`). Every sentence here is `authored_guidance`
  provenance: it is teaching narrative and **never** a runtime fact. The
  builder/validator enforce that separation; nothing in these files can
  masquerade as a machine fact.
- **Contract**: one JSON object per file, `schema: "exp.step_guidance.v1"`,
  generic entries only (no `role` key). Files are named after their operator
  id with the namespace colon mapped to an underscore (`rs:ndvi` →
  `rs_ndvi.json`) so the tree stays Windows-checkout safe. Loading is
  fail-closed: an invalid file is skipped and reported, never partially
  applied.
- **Live-schema coupling**: `parameterRationale[].parameter` must name a
  parameter that exists in the operator's live schema (`RSOperator::schema()`),
  and every `operatorId` must resolve in the live `RSOperatorRegistry`.
  `tests/test_explain_guidance_coverage` enforces this — renaming an operator
  or parameter without updating the corpus fails CI by design.
- **State tokens**: `stateNarrative` speaks the shared radiometric vocabulary
  `DN | Radiance | TOA | BOA | Index | Mask | * | None` (mirrored from the
  workflow IR 2.0 `PortFact`). The wildcard `*` is deliberately avoided:
  the builder compares narrative and port tokens by equality, so a wildcard
  would read as a contradiction against concrete port facts.
- **Schema**: `data/schemas/exp_step_guidance.schema.json` documents the exact
  shape (the loader in `guidance_store.cpp` is the authority). This directory
  must contain only entry `.json` files — the store loads every `*.json` here.

Extending the corpus: add one file per operator, keep the JSON contract,
and let the coverage oracle verify the coupling. Do not add execution facts
(status, timing, digests) — those come from provenance adapters, not here.
