// src/agent/harness/capability_relations.h
#pragma once

//
// D8 capability relation graph (ADR 0146): the operator relation data that
// lets the agent COMPOSE processing chains by query instead of guessing.
//
// Data lives in data/processing/algorithm_meta/capability/capability_relations.json:
// {
//   "schema_version": 1,
//   "chains":     [ {"from": "rs:a", "to": "rs:b",
//                    "when": {"radiometric_state": ["dn", "toa"]},
//                    "why": "..."} ],
//   "exclusive":  [ ["rs:atmospheric_dos1", "rs:atmospheric_dos2"] ],
//   "requires_shared_grid": ["rs:change_difference", ...],
//   "grid_fixer": "rs:align"
// }
//
// Semantics:
//   - chains: A→B is a LEGAL follow-with edge. `when` is a fact gate: the edge
//     participates in composition only when every gated fact matches the
//     caller's facts (facts missing a gated key never match — deterministic,
//     conservative). Edges are advisory for browsing (chainFrom) and
//     active for composition (composeChain).
//   - exclusive: the two operators must not both appear in one chain
//     (alternative implementations of the same step).
//   - requires_shared_grid: the operator's raster inputs must sit on one
//     grid (ADR 0098); the composer inserts the grid fixer ahead of it
//     unless the facts declare the inputs already aligned.
//
// Validation (fail-closed, at load): every referenced operator id must exist
// in the CapabilityCatalog; chains must form a DAG; an exclusive pair may not
// also appear as a chain edge in either direction; `when` values must be
// strings or arrays of strings.
//

#include <json/json.h>

#include <set>
#include <string>
#include <vector>

namespace sicnu::agent::harness {

/// Deterministic composition result: the ordered steps (grid fixer first,
/// then upstream prerequisites, target last), the edges that were skipped and
/// why, and advisory notes (prerequisites, reproducibility warnings).
/// Wire shape:
/// {target, steps:[{id, reason}], skipped:[{from, to, reason}], notes:[...]}
Json::Value composeChain( const std::string &targetId, const Json::Value &facts );

/// Browsing view of the graph around one operator:
/// {id, upstream:[{from, why, when}], downstream:[{to, why, when}]}
Json::Value chainFrom( const std::string &operatorId );

/// Registers the harness:compose_chain tool on the SpatialToolRegistry so the
/// agent consumes the graph as a tool, not as prose. Called once from
/// registerCapabilityGraphTools() — no separate call site.
void registerCapabilityCompositionTools();

/// Load-time validator (also used by the guard test). `knownOperatorIds` is
/// the set of catalog operator ids the references must resolve to.
std::vector<std::string> validateRelations( const Json::Value &doc,
                                            const std::set<std::string> &knownOperatorIds );

/// Internal representation used by composeChain/chainFrom. Reloads from the
/// default path on first use; setFilePath() overrides (tests).
class CapabilityRelations {
  public:
    static CapabilityRelations &instance();

    void setFilePath( const std::string &path );
    int reload();
    bool loaded() const;
    std::vector<std::string> loadProblems() const;

    bool hasEdge( const std::string &from, const std::string &to ) const;
    bool exclusiveWith( const std::string &a, const std::string &b ) const;
    bool requiresGrid( const std::string &operatorId ) const;
    std::string gridFixer() const;

    const std::vector<Json::Value> &chains() const { return mChains; }
    const std::vector<std::pair<std::string, std::string>> &exclusivePairs() const
    {
      return mExclusive;
    }
    const std::set<std::string> &sharedGridOperators() const { return mSharedGrid; }

  private:
    CapabilityRelations() = default;
    std::string defaultFilePath() const;

    std::string mFilePath;
    bool mLoaded = false;
    std::vector<Json::Value> mChains;                              ///< edge objects in file order
    std::vector<std::pair<std::string, std::string>> mExclusive;   ///< sorted pairs
    std::set<std::string> mSharedGrid;
    std::string mGridFixer = "rs:align";
    std::vector<std::string> mLoadProblems;
};

} // namespace sicnu::agent::harness
