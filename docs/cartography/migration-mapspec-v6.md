# Migrating MapSpec v5 → v6

Cartography Production 11.0 adds the series/production metadata surface.
v6 is a **strict superset** of v5: every new field is optional, every v5
document is already a valid v6 document, and `upgradeMapSpec` bumps the
version stamp idempotently.

## What v6 adds

| Member | Where | Shape | Meaning |
|---|---|---|---|
| `pages[k].variables` | page entries | object, ≤32 scalar members | The page's declared data context. The series planner substitutes `{{name}}` tokens from it at plan time; validation enforces the bound and scalar shapes. |
| `pages[k].series_row` | page entries | `{index: int, feature_id?: string, title?: string}` | Series-planner provenance: which row produced the page. |
| `pages[k].crs` | page entries | non-empty string | Provenance-only label of the row's CRS (e.g. `EPSG:4326`). Not consumed by the compiler — frame extents stay numeric. |
| `page.role: "index"` | page entries | role vocabulary extension | Marks a materialized series index page (previously `cover|map|report|appendix`). |

## Who writes these members

Hand-authored documents may declare any of them (validation accepts), but
the normal producer is the **series planner** (`planSeries`,
`src/agent/mapspec/series_planner.h`): one single-page template + a
`table`/`vector` series definition → a multi-page v6 document with
per-page variables, extents, title/page-number substitution and an
optional index page. The planner also clones items to pages k ≥ 1 with
`<id>-p<k>` ids and remaps `map_ref`, `locator.target` and
`constraints[].items` references.

## Migration action required

None. `upgradeMapSpec` bumps `spec_version` to 6 in place; older surfaces
that never read the new members behave exactly as before. Consumers that
re-serialize documents will observe the version stamp change only.
