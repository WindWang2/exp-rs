// src/agent/mapspec/mapspec.h
#pragma once

//
// MapSpec — declarative cartographic document (ADR 0127).
//
// MapSpec is the high-level semantic representation of a map product. QGIS
// stays the rendering/layout engine; MapSpecCompiler translates a MapSpec
// into a QgsPrintLayout through LayoutService, and MapSpecExtractor mirrors
// layouts back (best-effort roundtrip).
//
// A MapSpec document is a versioned JSON envelope:
//   { schema_version: "1.0", kind: "map_spec", spec_version: 1,
//     layout_name: "...", page: {width_mm, height_mm, orientation},
//     map_frames[], layers[], symbols[], legends[], north_arrows[],
//     scale_bars[], titles[], labels[], charts[], colorbars[], inset_maps[],
//     grids[], annotations[], source_notes[], constraints[] }
//
// Every item carries a stable `id` (agent referent) and an optional
// `semantic_role` ("title.main", "legend.primary", …) used by templates and
// preflight. Geometry uses `rect_mm: [x, y, w, h]` in page millimeters.
//

#include <json/json.h>

#include "../sicnu_agent_export.h"

#include <string>
#include <vector>

namespace sicnu::agent::mapspec {

// spec_version history:
//   0 — pre-release drafts: {layout, page, items: [{kind, …}]}.
//   1 — collections + stable ids (ADR 0127).
//   2 — compositional constraints (Design System 4.0, ADR 0131): style block
//       (token_set/medium/overrides), item anchors/min/max sizes/z_index/page
//       index/binding, slots, typed constraints, multi-page + atlas hook.
//       v2 is a strict superset of v1: every new field is optional.
//   3 — knowledge platform 5.0: bounded conditional visibility
//       (visible_if/content_if per item, page_if per page), the relative
//       constraint kinds above/below/left_of/right_of/inside/keep_with/
//       avoid_overlap/fit_content, inset locator extent indicators
//       (inset_maps[].locator.target), per-item style_ref, page roles and
//       the full atlas surface (filter/sort/margins/feature variables).
//       v3 is a strict superset of v2: every new field is optional.
inline constexpr int kMapSpecCurrentVersion = 3;

/// Ordered item collection names of a MapSpec document.
// SICNU_AGENT_EXPORT: const free variables are not auto-exported from the
// Windows DLL (CMake WINDOWS_EXPORT_ALL_SYMBOLS limitation) — the test and
// tool consumers link them through the import library.
extern SICNU_AGENT_EXPORT const char *const kCollections[];
extern SICNU_AGENT_EXPORT const int kCollectionCount;

/// True when `name` is a known item collection.
bool isCollection( const std::string &name );

/// Short id prefix for a collection ("map_frames" → "map", "titles" → "title").
std::string idPrefixFor( const std::string &collection );

/// True when `edge` is a valid anchor edge ("top-left", "top-center", …,
/// "bottom-right").
bool isAnchorEdge( const std::string &edge );

/// True when `kind` is a solver-enforced constraint kind (align,
/// match_width, match_height, stack, distribute — v2; plus the relative
/// kinds above, below, left_of, right_of, inside, keep_with, avoid_overlap,
/// fit_content — v3).
bool isConstraintKind( const std::string &kind );

/// True when `kind` is one of the v3 relative placement constraint kinds
/// (above, below, left_of, right_of, inside, keep_with, avoid_overlap,
/// fit_content).
bool isRelativeConstraintKind( const std::string &kind );

/// Creates an empty MapSpec with a page. `page` may carry width_mm/height_mm
/// (defaults: A4 landscape 297×210).
Json::Value makeMapSpec( const std::string &layoutName, Json::Value page );

/// Assigns an unused id for `collection` ("title-3") and appends `item`.
/// Returns the assigned id. Unknown collection leaves the doc untouched.
std::string appendMapSpecItem( Json::Value &spec, const std::string &collection, Json::Value item );

/// Locates an item by id across all collections; returns
/// {collection, index} as {"collection": "...", "index": n} or empty object.
Json::Value findMapSpecItem( const Json::Value &spec, const std::string &id );

/// Removes an item by id; false when the id is unknown.
bool removeMapSpecItem( Json::Value &spec, const std::string &id );

/// Full validation: envelope, page geometry, per-item required fields,
/// id uniqueness, rect bounds, reference integrity (map_ref → map_frames),
/// and the v2 composition surfaces (style, anchors, sizes, z_index, page
/// index, slots, constraints, pages, atlas). Returns one human-readable
/// problem per entry; empty means valid.
std::vector<std::string> validateMapSpec( const Json::Value &spec );

/// Migrates older documents to kMapSpecCurrentVersion. Returns the upgraded
/// document; unknown/malformed input returns it unchanged.
Json::Value upgradeMapSpec( const Json::Value &doc );

/// Evaluates the v3 conditional fields (`visible_if` / `content_if` per
/// item, `page_if` per page) against `context` and prunes `spec` in place:
/// items whose visible_if is false are removed; items whose content_if is
/// false lose their `content` member (an item left without content or text
/// is removed); pages whose page_if is false are removed together with the
/// items placed on them (remaining page indices are remapped compactly).
/// Conditions that fail to evaluate (unknown context path) keep the content
/// — nothing is ever silently dropped — and the error is appended to *errors.
/// Returns the evaluation ledger: [{id, condition, field, outcome, error?}].
Json::Value resolveMapSpecConditions( Json::Value &spec, const Json::Value &context,
                                      std::vector<std::string> *errors );

/// Applies one patch op to the document:
///   {op: "add",    collection: "titles",   value: {…}}           (id assigned)
///   {op: "update", id: "title-1",          value: {…}}           (fields merged)
///   {op: "remove", id: "title-1"}
/// Returns false with *error set when the op is invalid.
bool applyMapSpecPatch( Json::Value &spec, const Json::Value &patch, std::string *error );

/// Convenience: applies a list of patches (stops at the first failure).
bool applyMapSpecPatches( Json::Value &spec, const Json::Value &patches, std::string *error );

} // namespace sicnu::agent::mapspec
