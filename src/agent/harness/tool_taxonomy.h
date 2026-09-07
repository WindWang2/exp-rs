// src/agent/harness/tool_taxonomy.h
#pragma once

//
// Harness 4.0 stable tool taxonomy (mission Phase 2).
//
// Every agent-callable tool classifies into one `domain.action` entry. The
// vocabulary is closed and stable: Pi can rely on the strings for planning;
// new tools must classify into the existing vocabulary (extending it requires
// a docs/adr update, mirroring the contract-document rule in ADR 0128).
//
// The classification is derived from the tool id (namespace + name) with an
// explicit override table — never from tool descriptions, which are prose.
//

#include <string>

namespace sicnu::agent::harness {

/// One taxonomy classification: `domain.action`, e.g. "raster.inspect".
struct ToolTaxonomy {
  std::string domain;  ///< e.g. "data", "raster", "processing", "workflow"
  std::string action;  ///< e.g. "inspect", "run", "preflight"

  /// Dot-joined wire form ("raster.inspect"). Empty domain yields "".
  std::string toString() const;
  bool valid() const { return !domain.empty() && !action.empty(); }
};

/// Closed vocabulary checks (used by tests to pin the taxonomy).
bool isKnownTaxonomyDomain( const std::string &domain );
bool isKnownTaxonomyAction( const std::string &domain, const std::string &action );

/// Classifies any tool id. Unknown ids classify as {"other","other"} — the
/// classification is total over the id space and never fails.
ToolTaxonomy taxonomyForTool( const std::string &toolId );

} // namespace sicnu::agent::harness
