# Adversarial review — Science Context Broker

## Attacks tried / mitigations

| Attack | Mitigation |
|---|---|
| Hostile schema / non-object JSON | `bundleFromJson` fail-closed; tool adapters throw |
| Absolute paths in tool output | `redactPathHint` basename-only |
| Conflicted radiometry auto-pick | ObservedState omits unit; summary unit cleared |
| Unknown radiometry defaulted | claim Unknown removes `radiometric_state` |
| Autonomy L2 auto-exec | `allowAutonomousExec` false unless L5; open question + limitation |
| Recipe auto-execute | `recipe:search` always `auto_execute:false` |
| Non-determinism via metrics | Live counters kept off bundle bytes |
| Over-budget silent drop | TruncationMeta sections + dropped counts |
| SAR matching optical recipe | Modality filter in RecipeRouter |
| Unknown intent invention | `impossible` + UNKNOWN_INTENT; no fake operators |
| Parallel track collision | Own directory + data_platform append only |

## Residual limits

- Capability rules are a closed structural subset (not full harness knowledge load).
- Recipe documents are injected into `RecipeRouter` (process wiring to on-disk
  `ScientificRecipeRegistry` is a follow-up; `src/recipes` is still unwired in
  root CMake on this tip).
- Catalog/GDAL collectors are injected via `PassportResolver` — headless tools
  accept inline `passport_json` for parity without DataManager.
