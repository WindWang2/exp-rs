// src/agent/harness/capability_catalog.h
#pragma once

//
// D8 capability knowledge layer (ADR 0146): typed per-operator capability
// metadata for ALL 111 rs: operators, one sidecar per operator under
// data/processing/algorithm_meta/capability/rs-<slug>.json.
//
// Relationship to the other layers (each fact has ONE derivation target):
//   - AlgorithmDescriptor / RSOperator (code)      = source of truth for
//     mechanically derivable facts (io, parameters, determinism grade,
//     memory policy, prerequisites, limitations). scripts/capability_knowledge_tool
//     regenerates them; hand-editing them is drift (tests fail).
//   - this directory's sidecars                    = the D8 catalog: derived
//     facts frozen as data + authored enrichment (failure_modes, applicability,
//     teaching_use, summary 中文) that no code path can invent.
//   - data/agent/capabilities/*.json               = the preflight/feasibility
//     layer (ADR 0142) — unchanged by D8; the guard test cross-checks the
//     modality/band-role facts both layers declare.
//   - pi/knowledge/capability-*.md                 = rendered FROM these
//     sidecars (capability_pages.h); hand-editing the pages is a defect.
//
// Sidecar wire shape (closed keys; validateEntry rejects anything else):
// {
//   "id": "rs:ndvi",
//   "schema_version": 2,
//   "display_name": "...", "description": "...",      (derived)
//   "task": "...", "input": "raster", "output": "raster",
//   "tags": [...], "notes": "...",                    (v1 overlay fields, derived)
//   "capability": {
//     "schema_version": 2,
//     "family": "spectral",            (canonical D8 family, authored map)
//     "operator_group": "spectral",    (raw group(), derived)
//     "summary": "一句话中文摘要",       (authored)
//     "io": { "inputs": [...], "outputs": [...], "parameters": [...] },
//     "modality": ["optical"],
//     "band_roles": {"nir": 1, "red": 1},
//     "crs": {"requires_projected": false, "requires_shared_grid": false},
//     "determinism": {"grade": "bit_exact", "stochastic": false,
//                      "memory_policy": "streaming", "large_raster_safe": true,
//                      "cost_class": "O(tile)"},
//     "prerequisites": ["..."], "limitations": ["..."],
//     "failure_modes": [{"code": "...", "when": "...", "remedy": "..."}],
//     "applicability": {"land_cover": [...], "scenes": [...], "notes": "..."},
//     "teaching_use": {"concepts": [...], "courses": [...], "exercise": "..."}
//   }
// }
//
// Query API (stable for D9): byFamily / byInputModality / determinismOf /
// stochasticOperators / requiresGrid / manifestPage (hard 64 KiB cap) /
// errorCatalog (hard 8 KiB cap). The graph queries (chainFrom / composeChain)
// live in capability_relations.h.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::processing {
struct AlgorithmDescriptor;
}

namespace sicnu::agent::harness {

/// The 11 canonical D8 families (closed vocabulary).
const std::vector<std::string> &capabilityFamilies();

/// Canonical family for an operator id + raw group(). Total over all 111
/// rs: operators (the guard test pins totality); falls back to group-based
/// mapping, then "io".
std::string canonicalFamily( const std::string &operatorId, const std::string &group );

/// Deterministically derives the v2 "capability" block (and the v1 overlay
/// fields) from a live descriptor. Authored keys already present in
/// `authoredCapability` (failure_modes / applicability / teaching_use /
/// summary / band_roles / modality overrides) win over derived defaults;
/// derived keys are always recomputed. Pure: same input, same output.
Json::Value deriveCapabilityBlock( const sicnu::processing::AlgorithmDescriptor &desc,
                                   const Json::Value &authoredCapability );

/// Loads + validates the v2 sidecars and answers the bounded queries the
/// agent harness (and D9) consume. Fail-closed like CapabilityKnowledge:
/// entries with validation problems are skipped and reported.
class CapabilityCatalog {
  public:
    static CapabilityCatalog &instance();

    /// Directory override (tests); empty restores the default search path
    /// ($SICNU_CAPABILITY_META_DIR, <cwd>/data/processing/algorithm_meta/capability,
    /// <app>/../data/processing/algorithm_meta/capability, SICNU_SOURCE_DIR/...).
    void setDirectory( const std::string &directory );
    std::string directory() const;

    /// (Re)scans the directory. Returns the number of valid entries loaded.
    int reload();
    bool loaded() const;
    std::vector<std::string> loadProblems() const;

    /// Operator ids in deterministic (sorted) order.
    std::vector<std::string> entryIds() const;
    bool hasEntry( const std::string &operatorId ) const;

    /// The raw sidecar document (v1 fields + "capability" block), or null.
    Json::Value entry( const std::string &operatorId ) const;

    /// The parsed "capability" block (empty object when unknown).
    Json::Value capability( const std::string &operatorId ) const;

    /// Ids whose capability.family == family, sorted.
    std::vector<std::string> byFamily( const std::string &family ) const;

    /// Ids whose capability.modality array contains modality, sorted.
    std::vector<std::string> byInputModality( const std::string &modality ) const;

    /// ADR 0124 grade ("bit_exact" | "tolerance"), "" when unknown.
    std::string determinismOf( const std::string &operatorId ) const;

    /// True when the operator declares stochastic behaviour (same inputs,
    /// potentially different outputs across runs — e.g. unseeded k-means).
    bool isStochastic( const std::string &operatorId ) const;

    /// All stochastic operator ids (the "may not reproduce" surface), sorted.
    std::vector<std::string> stochasticOperators() const;

    /// True when the operator demands its raster inputs on one shared grid
    /// (ADR 0098 contract; rs:align is the canonical fixer).
    bool requiresGrid( const std::string &operatorId ) const;

    /// Hard budgets (docs/agent/evaluation-suite.md). A manifest page for a
    /// queried family must stay under 64 KiB; the typed failure-mode catalog
    /// under 8 KiB. Both are asserted by tests with measured bytes.
    static constexpr size_t kManifestBudgetBytes = 64 * 1024;
    static constexpr size_t kErrorCatalogBudgetBytes = 8 * 1024;

    /// Bounded manifest page for one family (or all families when family is
    /// empty). Shape: {family, page, page_count, total, entries:[...]}.
    /// Entries are trimmed schema-aware until the rendered JSON fits the
    /// 64 KiB budget; pageSize <= 64.
    Json::Value manifestPage( const std::string &family, int page, int pageSize ) const;

    /// The typed failure-mode catalog across all operators, deterministic
    /// order, rendered under the 8 KiB budget: {codes:[{code, operators:[...]}],
    /// budget_bytes}. Codes come from the closed failure-mode vocabulary.
    Json::Value errorCatalog() const;

    /// Closed failure-mode code vocabulary (failure_modes[].code).
    static const std::vector<std::string> &failureModeCodes();

    /// Structural + vocabulary validation of one raw sidecar. Returns one
    /// problem string per finding; empty means valid. Static so tests can
    /// validate documents without a directory scan.
    static std::vector<std::string> validateEntry( const Json::Value &sidecar );

  private:
    CapabilityCatalog() = default;
    std::string defaultDirectory() const;

    std::string mDirectory;
    bool mLoaded = false;
    Json::Value mEntries{Json::objectValue};  ///< operator id -> raw sidecar
    std::vector<std::string> mOrder;          ///< sorted ids
    std::vector<std::string> mLoadProblems;
};

} // namespace sicnu::agent::harness
