# Recipe Authoring Guide (Platform 5.0)

Recipes are metadata under `data/agent/recipes/*.json` that orchestrate
existing operators into AgentPlan v2 documents. They never carry kernels —
the operator registry stays the single algorithm authority. Platform 5.0 adds
facets and hardens the authoring contract.

## Minimal document

```json
{
  "schema_version": "1.0",
  "kind": "harness_recipe",
  "recipe_id": "harness.optical_ndwi",
  "title": "NDWI surface water index",
  "intent": "ndwi",
  "family": "water",
  "modality": "optical",
  "sensors": ["sentinel-2", "landsat-8"],
  "description": "…",
  "slots":    [{ "name": "primary", "kind": "raster", "required": true, "description": "…" }],
  "steps":    [{ "id": "ndwi", "operator_id": "rs:spectral_index",
                 "params": { "input": "$primary.path", "index": "NDWI",
                             "output": "$outputs.ndwi" } }],
  "outputs":  [{ "name": "ndwi", "from_step": "ndwi", "port": "output", "kind": "raster" }],
  "verification": { "enabled": true },
  "map_output": { "from_step": "ndwi", "renderer_hint": "singleband_pseudocolor" }
}
```

## Platform 5.0 facets (additive)

| field | contract |
|---|---|
| `family` | task family (`flood`, `sar-change`, `temporal`, `terrain`, …) — search facet |
| `modality` | `optical` \| `sar` \| `temporal` \| `terrain` \| `model` \| `multimodal` |
| `sensors` | lowercase hints (`sentinel-1`, `modis`, …) |

All three are required for new recipes (the lint gate checks them) and are
backfilled on the five baseline recipes.

## The mini-language

- `$<slot>.path` — bound slot's resolved path (slot must be declared; the
  lint gate and `instantiateRecipe` both enforce this)
- `$outputs.<name>` — derived output path. Declared `outputs` get
  deterministic paths (`<output_dir>/<recipe>_<name>.tif`); **any other
  `$outputs.*` reference in a step becomes an intermediate artifact with a
  derived path** — declare only what consumers should see.
- `$params.<key>` — caller binding under `bindings.params`
- `when_slot` / `when_param` — gates; a closed gate drops the step unless
  `params_when_skipped` provides an alternative parameter template (downstream
  steps then read it too)

## Intent + preflight packs

`intent` must be in the closed vocabulary (`isKnownIntent`): the five Harness
4.0 intents plus `evi savi ndre ndwi mndwi ndsi nbr dnbr ndbi bsi water flood
sar_water sar_flood sar ship temporal terrain accuracy qa preprocess inference`.

Band-ratio intents route through the generalized `bandRatioRules` pack:
required windows are checked as roles **or** wavelength windows (NIR
750–1100 nm, red 600–700 nm, green 500–600 nm, blue 430–520 nm, SWIR
1550–1750 / 2080–2350 nm, red edge 700–745 nm). Raw-DN inputs are warned;
missing windows block the plan. Pair intents (`change`, `sar_change`, `dnbr`,
`accuracy`, `sar_flood`) need two comparable inputs.

## Checklist before shipping a recipe

1. Operator ids exist in the registry (`rs_operators_init.cpp` families) and
   parameter names match the operator's declared `make*Param` props.
2. Slots referenced by steps are declared; gates reference declared
   slots/params; outputs reference existing steps.
3. Facets present; `intent` routes to the right preflight pack.
4. `cartography:lint_catalog` stays clean and, if the recipe backs a
   solution, `solution:validate` resolves every reference.
5. Deterministic map_output renderer hint consistent with the StyleSpec id
   used by the consuming solution.
