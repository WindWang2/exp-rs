# BASELINE — Unified Help 6.0 (branch point 74fd0c7c, master)

## Quantified state at branch point

### Help-like surfaces found

| Surface | Location | Count | State |
| --- | --- | --- | --- |
| CommandRegistry commands | `src/app/workbench/command_defs.cpp` | 34 registered | title + 1-line description (中文), category, keywords; **no purpose/prerequisites/related** |
| ContextRules availability | `src/app/workbench/selection_context.h/.cpp` | 9 predicates + `unavailabilityReason()` | string-based, flat; facts not structured |
| RSOperator registry | `src/operators/rs/rs_operators_init.cpp` | 105 `rs:*` operators (+ opencv/gdal/io/otb/python families) | `schema()` JSON (types/ranges/defaults/one-line param descriptions), `metadata()` (purpose/prerequisites/limitations/workflowHints/tags for some), memoryPolicy, determinismGrade, executionEstimate |
| Operator error taxonomy | `src/operators/framework/rs_operator_error.h` | 24 codes (1000–9999) | message at throw site, no remediation |
| GeoError taxonomy | `src/geospatial/common.h` | 22 codes | message only |
| HarnessError taxonomy | `src/agent/harness/harness_error.h` | 24 stable codes, category + retry class + suggested actions | agent-facing; no human remediation page |
| Dataset diagnostics | `src/dataset/dataset_quality.h`, `leakage_audit.h` | typed findings (LeakageKind, LabelQualityFinding codes e.g. `label.unknown_class`) | codes exist, no help text layer |
| Scientific preflight | `src/agent/harness/scientific_preflight.h` | checks array `{check, passed, severity, code, details}` | no help linkage |
| SchemaFormBuilder | `src/app/shell/schema_form_builder.h` | schema-driven forms | labels from schema; **no unit/recommendation/inline help** |
| Algorithm help catalog | `src/processing/algorithm_help_catalog.*` | GDAL/OTB wrapper help | name-keyed strings, separate world |
| MCP tools | `src/agent/mcp_server.h`, `tool_catalog/` | compact tools/list (#643) | descriptions only, no deeper help link |
| CLI | `src/cli/main_cli.cpp` | QCommandLineParser options (pipeline/list/schema/…) | generic option help only |
| Hand-written tooltips | `setToolTip` in `src/app/**` | 304 call sites | divergent ad-hoc strings |
| What's This | `setWhatsThis` in `src/app/**` | 35 call sites | mostly absent |
| docs/ | `docs/**` | 402 markdown files | narrative, not linked to code facts |

### Identified problems

1. **No stable Help IDs** — nothing addresses a topic across surfaces.
2. **Duplication** — command titles/descriptions retyped per surface (menus, palette,
   ribbon); operator one-liners duplicated into dialogs; error remediation re-explained
   ad hoc at call sites.
3. **Staleness risk** — schema facts (ranges/defaults) copied into prose in dialogs/docs.
4. **Shallow content** — commands have no purpose/prerequisites; parameters lack
   units/scientific meaning/recommendations; diagnostics lack "why it matters/what to do".
5. **No F1 mapping, no Help Center**, no context resolution path widget→topic.
6. **Availability not explainable** — `explain()` returns one flat string, not facts.
7. **Coverage unknown** — no mechanical check that a command/operator/diagnostic has help.

## Existing strengths to build on

- `CommandRegistry` already centralizes command identity + availability + explain.
- `RSOperator::schema()` is a genuine single source of truth for parameter facts.
- `HarnessError` already has category/retry/suggested-actions — the diagnostic catalog
  can reference rather than re-invent.
- Operator `metadata()` already carries purpose/prerequisites/limitations for several
  operators — help layer should surface, not duplicate.
- Compact MCP listing exists (#643) — bounded projection precedent.
