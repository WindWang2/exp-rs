// src/agent/mapspec/series_planner.h
#pragma once

//
// Series planning (Cartography Production 11.0).
//
// A series turns ONE page prototype into a multi-page product driven by
// feature / region / time rows: every row materializes a page with its own
// variables, extent, title and page number, plus an optional index page.
// The output is an ordinary MapSpec document (spec_version 6: per-page
// `variables`, `series_row` provenance and the "index" page role are the
// v6 surface) — nothing new downstream: validate/compile/preflight/
// produce all treat it exactly like a hand-authored multi-page document.
//
// Two sources:
//   * "table"  — explicit rows in the definition (region tables, dates);
//   * "vector" — a coverage vector layer (filter/sort) materialized into
//     rows. For feature-driven deliveries that exceed the document page
//     cap, the atlas path (page.atlas + cartography:produce atlas mode)
//     is the sanctioned large-series mechanism; the planner states so in
//     a problem instead of silently truncating.
//
// Substitution tokens (resolved at plan time, BEFORE compile — they never
// reach QGIS's own [% %] expression syntax):
//   {{name}}         — page variable (row.variables[name], scalar)
//   {{page_number}}  — 1-based page index
//   {{page_total}}   — total materialized pages
// Unknown tokens stay literal and are reported as problems: the ledger is
// the honest "what did I not resolve" surface.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::mapspec {

/// Per-page variable budget (mirrors the atlas feature_variables budget).
inline constexpr int kMaxSeriesVariables = 32;
/// Materialized series respect the existing 10-page document cap; larger
/// feature-driven products belong to the atlas delivery path.
inline constexpr int kMaxSeriesPages = 10;

/// One materialized page's inputs.
struct SeriesRow
{
    std::string title;        ///< main-title override; empty = keep template title
    Json::Value extent;       ///< [xmin, ymin, xmax, ymax] | null
    std::string crs;          ///< provenance-only page CRS label
    Json::Value variables;    ///< {name: scalar}, ≤ kMaxSeriesVariables
    std::string feature_id;   ///< vector-source provenance
};

/// Pure core: materializes a single-page `template_spec` into a multi-page
/// MapSpec with one page per row (page 0 keeps the template's item ids;
/// pages k ≥ 1 clone every item with a `-p<k>` suffix and remap the known
/// id references: map_ref, locator.target, constraint items[]). Returns
/// null with problems when the template or rows are unusable.
Json::Value planSeriesPages( const Json::Value &template_spec,
                             const std::vector<SeriesRow> &rows,
                             std::vector<std::string> *problems );

/// Parses the "table" series form:
///   { type: "table", index_page?: {enabled?, title?},
///     rows: [{title?, extent?, crs?, variables?, feature_id?}] }
/// (rows without an extent are legal: the template extent carries over).
Json::Value seriesDefinitionToRows( const Json::Value &definition,
                                    std::vector<std::string> *problems );

/// QGIS-backed "vector" series form:
///   { type: "vector", layer: "<name|layer id>", filter?, sort_by?, order?,
///     title_field?, variable_fields?: [≤8 field names], max_rows? }
/// Extents come from feature geometry bounding boxes (layer CRS); rows
/// carry the requested variable fields as scalars. Layer unresolved →
/// problem, never an empty series.
Json::Value seriesDefinitionToRowsVector( const Json::Value &definition,
                                          std::vector<std::string> *problems );

/// Top-level entry: dispatches on definition.type ("table" | "vector"),
/// then materializes. `definition.index_page.enabled` appends the index
/// page (role "index", one bounded label listing "N. title" rows).
Json::Value planSeries( const Json::Value &template_spec, const Json::Value &definition,
                        std::vector<std::string> *problems );

} // namespace sicnu::agent::mapspec
