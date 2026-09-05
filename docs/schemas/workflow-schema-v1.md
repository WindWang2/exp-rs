# Public Workflow Schema v1

The workflow document is a versioned contract shared by the C++ engine, the
CLI (`workflow run|validate`) and the Python SDK (`exprs.Workflow`).

```json
{
  "schema_version": 1,
  "id": "lab.optical.demo",
  "title": "Optical preprocessing demo",
  "workspace_kind": "",
  "steps": [
    {
      "id": "cal",
      "kind": "operator",
      "operator": "rs:radiometric_calibration",
      "params": { "input": "in.tif", "output": "cal.tif" }
    },
    {
      "id": "ndvi",
      "operator": "rs:ndvi",
      "inputs": [ { "port": "input", "source": "$cal.output" } ],
      "params": { "output": "ndvi.tif" },
      "gates": [ { "require": "hasArtifact:output", "hint": "run calibration first" } ],
      "resource_estimate_mb": 2048
    }
  ]
}
```

## Versioning rules

- `schema_version` must be a positive integer; documents **without** the
  field are treated as version 1 (legacy tolerance with the pre-SDK engine
  format).
- Version > supported → rejected (`E1005` in diagnostics) with the supported
  range named; `exprs::migrateWorkflowDocument` is the upgrade path.
- Unknown optional fields are ignored.

## Step fields

| field | required | notes |
|---|---|---|
| `id` | yes | unique within the document |
| `kind` | no | `operator` (default) \| `interactive` \| `review` \| `composite` |
| `operator` | operator steps | aliases: `operatorId`, `name` |
| `params` | no | object; `$step.port` placeholders resolve by the engine |
| `inputs` | no | `[{port, source}]`; `source` uses the `$step.port` grammar |
| `gates` | no | `[{require, hint}]`, e.g. `hasArtifact:output`, `paramNonEmpty:input` |
| `artifact_on_success` | no | port name to register as an artifact |
| `resource_estimate_mb` | no | admission-control hint |
| `meta` | no | UI metadata (positions etc.), ignored headless |

## Validation layers

1. **Public schema** (`exprs::validateWorkflowDocument`): shape, version
   gate, unique ids, operator presence. Portable — used by CLI, Python SDK
   and the conformance kit.
2. **Engine semantics** (`workflowDefinitionFromJson` +
   `topologicalSortSteps`): aliases, port-type compatibility, cycles,
   gates. Runs in the CLI validate path and every execution.

A document that passes (1) but fails (2) is reported as
`ValidationFailure` (exit code 2) with the engine's message.
