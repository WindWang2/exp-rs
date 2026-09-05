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

#include <string>
#include <vector>

namespace sicnu::agent::mapspec {

inline constexpr int kMapSpecCurrentVersion = 1;

/// Ordered item collection names of a MapSpec document.
extern const char *const kCollections[];
extern const int kCollectionCount;

/// True when `name` is a known item collection.
bool isCollection( const std::string &name );

/// Short id prefix for a collection ("map_frames" → "map", "titles" → "title").
std::string idPrefixFor( const std::string &collection );

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
/// id uniqueness, rect bounds, reference integrity (map_ref → map_frames).
/// Returns one human-readable problem per entry; empty means valid.
std::vector<std::string> validateMapSpec( const Json::Value &spec );

/// Migrates older documents to kMapSpecCurrentVersion. Returns the upgraded
/// document; unknown/malformed input returns it unchanged.
Json::Value upgradeMapSpec( const Json::Value &doc );

/// Applies one patch op to the document:
///   {op: "add",    collection: "titles",   value: {…}}           (id assigned)
///   {op: "update", id: "title-1",          value: {…}}           (fields merged)
///   {op: "remove", id: "title-1"}
/// Returns false with *error set when the op is invalid.
bool applyMapSpecPatch( Json::Value &spec, const Json::Value &patch, std::string *error );

/// Convenience: applies a list of patches (stops at the first failure).
bool applyMapSpecPatches( Json::Value &spec, const Json::Value &patches, std::string *error );

} // namespace sicnu::agent::mapspec
