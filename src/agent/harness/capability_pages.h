// src/agent/harness/capability_pages.h
#pragma once

//
// D8 knowledge page renderer (ADR 0146): pi/knowledge/capability-*.md are
// GENERATED from the same sidecar JSON the query API serves. Hand-editing a
// generated page is a defect — edit the sidecar and regenerate.
//
// Pages (all deterministic — sorted ids, no timestamps):
//   pi/knowledge/capability-index.md                  — family index + budgets
//   pi/knowledge/capability-<family>.md (× 11)        — one page per family
//
// The guard test re-renders in-process and byte-compares against the
// committed files, so pages cannot drift from the metadata.
//

#include <string>
#include <utility>
#include <vector>

namespace sicnu::agent::harness {

struct KnowledgePage
{
    std::string relativePath;  ///< e.g. "pi/knowledge/capability-spectral.md"
    std::string content;       ///< exact committed bytes (UTF-8)
};

/// Renders every generated page from the live CapabilityCatalog +
/// CapabilityRelations. Returns pages in deterministic (path-sorted) order.
std::vector<KnowledgePage> renderCapabilityKnowledgePages();

/// Chinese display name for a canonical family ("spectral" -> 光谱…).
std::string familyDisplayName( const std::string &family );

} // namespace sicnu::agent::harness
