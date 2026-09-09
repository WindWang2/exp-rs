# Solution Authoring Guide (Platform 5.0)

A **SolutionTemplate** is the task-level knowledge package: it binds an
analysis recipe, a map template, optional report template and StyleSpec
references into one validated, searchable, instantiable unit. Solutions never
embed or execute algorithms — the recipe compiles to an AgentPlan v2 document
and runs through the authoritative workflow engine; the map template
instantiates through the standard `TemplateRegistry`.

```
User intent
  → solution:search(id?) / solution:describe
  → input contracts (what to bind)
  → solution:instantiate
      ├─ RecipeCatalog::instantiateRecipe → AgentPlan v2 → WorkflowDefinition
      ├─ TemplateRegistry::instantiateTemplate → MapSpec draft (+ condition_context)
      └─ StyleRegistry references (token-resolved at instantiate time)
  → harness:execute_plan → cartography:compose → style:apply → layout:export
```

## Files

- `data/agent/solutions/<id>.json`, one document per file (≤ 512 KB)
- `id` must live in the `solution.` namespace and is immutable per version

## Required fields

| field | contract |
|---|---|
| `schema_version` | `"1.0"` |
| `kind` | `"solution_template"` |
| `id`, `version`, `title` | identity |
| `tasks` | string array (≤ 24) — search facet |
| `modalities` | subset of `optical sar temporal terrain vector model multimodal` |
| `sensors` | free-form hints, lowercase canonical (`sentinel-1`, …) |
| `input_contracts` | `[{name, kind, required, description, accepted?}]` ≤ 16 |
| `analysis_recipe` | `data/agent/recipes` id (validated at lint) |
| `map_template` | `data/cartography/templates` id |
| `style_spec` | `{role → style id}` (validated) |
| `quality_grade` | `experimental` · `reviewed` · `certified` |

Optional: `keywords` (≤ 24), `report_template`, `recipe_params`,
`expected_artifacts` (≤ 16), `limitations`, `aliases`, `extends`, `family`,
`medium`, `verification`.

## Inheritance, aliases, deprecation

- `extends: "<solution id>"` deep-merges the parent under the child (child
  fields win). Cycles and unknown parents are load problems; the document is
  skipped rather than half-loaded.
- `aliases: ["flood.sar"]` resolve through `SolutionRegistry::find` and
  `solution:describe`; alias collisions are load problems.
- Deprecation convention: set `quality_grade: "experimental"`, describe the
  successor in `limitations`, and add the old id as an *alias of the new
  solution* so old references keep resolving.

## Instantiation contract

`solution:instantiate({id, bindings: {slots, params, output_dir}, title?,
condition_context?})`:

1. Every `required` input contract must be bound (typed error listing the
   missing slots otherwise).
2. `recipe_params` are the defaults; caller `params` win per key.
3. The MapSpec draft gets `condition_context` stamped only when the caller
   passes one — conditions without a context keep their content (see
   `mapspec-reference.md`, v3).
4. Style entries are token-resolved eagerly so post-run `style:apply` cannot
   fail on a dangling token reference.

## Checklist before shipping a solution

1. `solution:validate {id}` — clean (references resolve against the live
   catalogs).
2. `cartography:lint_catalog` — the `solution_references` and `style_tokens`
   checks stay clean.
3. End-to-end fixture: instantiate → plan compiles (`compilePlanToWorkflowJson`)
   → mapspec validates → preflight runs (see `test_platform5.cpp`).
4. Every claim in `description`/`limitations` matches what the recipe chain
   actually produces.

## Platform 6.0 — explainable matching

`solution:search` (and `searchSolutions`) explain every outcome:

- each hit carries `match: {reasons: [...]}` — e.g. `task:flood`,
  `modality:optical`, `keyword:'flood'`;
- the envelope carries `rejected: [{id, reasons}]` (first 10, deterministic
  order) stating why near-miss solutions were excluded — e.g. `"modality
  'optical' not declared"`.

Matching stays exact and bounded; explanations are computed during the same
single pass, so search cost is unchanged.
