> Reconstructed 2026-09 from ADR 0144 + commit evidence; the original planning files were never committed (gitignore whitelist omission). Do not treat as contemporaneous artifacts.

# GOAL — Pi Spatial Scientist Harness 8.0

Track `feat/spatial-scientist-harness-8` · decision record `docs/adr/0144-harness-8.md` · delivered via PR #842.

## Mission

Harness 7.0 kept Pi the only agent loop and delivered capability knowledge,
the intent graph, preflight, plan repair, typed context, and token budgets.
8.0 keeps that architecture and deepens the **evidence chain**: an agent
answer must cite inspectable files, not memory.

## Verified gaps to close (ADR 0144, Context)

- Knowledge covered 91 of ~132 registry operators, no platform tools.
- `DatasetUnderstanding` folded product facts into one field — no typed
  slots, `nodata`, `quality_masks`, per-asset contexts, model contracts.
- No evidence sidecar writers (7.0 adversarial-review gap F1).
- Plans had no identity pins, cleanup policy, or deterministic fingerprint.
- `resolve_intent` lacked missing facts/preparations/solution paths, explainability, external evals.

## Decisions (ADR 0144)

Typed spatial context 2.0 · capability knowledge completion (`surface`
discriminator, full-registry drift floor) · harness-owned atomic evidence
sidecars (uncertainty only from operator-declared facts) · plans 8.0
(identity pins, `plan_fingerprint`, cleanup policy, `harness:explain`) ·
intent/feasibility 2.0, preflight `assumptions`, externalized eval corpus.
Non-negotiable: Pi stays the only agent loop; no uncertainty fabrication; the deterministic core stays C++/JSON.
