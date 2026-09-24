// src/repair_planner/repair_provider.h
#pragma once

//
// RS14-03 completion slice C: the capability provider seam.
//
// The planner is a portable leaf (no sicnu library dependencies, same
// discipline as src/contracts) — so candidate capability knowledge reaches it
// through this interface instead of a direct dependency on the harness
// knowledge layer (src/agent/harness/capability_knowledge.*). Embedders adapt
// the real knowledge documents to this seam; tests use fakes and
// real-shaped documents.
//
// Capability entries are capability-knowledge-shaped JSON documents (id,
// family, resource.cost_class, ...) exactly as shipped in
// data/agent/capabilities/*.json. The provider never executes anything and
// never invents entries: an entry that cannot identify itself (missing id) is
// rejected at build time, fail-closed.
//
// The requirement-kind -> serving-operator-id routing table lives in the
// .cpp as a single closed map over the ids the real knowledge documents
// already carry. An id the knowledge layer does not ship routes nothing; a
// test pins the routing against the actual documents.

#include <json/json.h>
#include <map>
#include <string>
#include <vector>

#include "repair_schema.h"

namespace sicnu::repair {

class RepairCapabilityProvider
{
  public:
    virtual ~RepairCapabilityProvider() = default;

    /// Capability entries that can serve a requirement kind, in provider
    /// order (the planner re-sorts deterministically). Empty when none.
    virtual std::vector<Json::Value>
    capabilitiesForRequirement( const std::string &requirementKind ) const = 0;

    /// True when the kind is in the provider's served vocabulary. A false
    /// return is the typed "unknown capability" path — the planner records
    /// it, it never degrades to a placeholder.
    virtual bool knowsRequirementKind( const std::string &requirementKind ) const = 0;
};

/// JSON-backed provider: a map of requirement kind -> array of capability
/// entries. Fakes and embedders construct it directly.
class JsonRepairCapabilityProvider : public RepairCapabilityProvider
{
  public:
    JsonRepairCapabilityProvider() = default;

    /// Build from an explicit kind -> entries map. Fails (typed
    /// invalid_input) when any entry is not an object without a non-empty
    /// string "id".
    static bool build( const std::map<std::string, Json::Value> &families,
                       JsonRepairCapabilityProvider &out, RepairError &error );

    /// Real-shaped construction: route capability-knowledge entries (as
    /// loaded from data/agent/capabilities/*.json) into requirement kinds via
    /// the closed family routing table. Entries the table does not cover are
    /// skipped (they serve no repair requirement); entries without an id are
    /// typed invalid_input.
    static bool buildFromCapabilityEntries( const std::vector<Json::Value> &entries,
                                            JsonRepairCapabilityProvider &out,
                                            RepairError &error );

    std::vector<Json::Value>
    capabilitiesForRequirement( const std::string &requirementKind ) const override;

    bool knowsRequirementKind( const std::string &requirementKind ) const override;

  private:
    std::map<std::string, Json::Value> mFamilies; ///< kind -> array of entries
};

/// The closed requirement-kind -> capability-family routing table (the
/// families the real knowledge documents declare). Exposed for drift tests.
std::vector<std::string> servingOperatorsForRequirement( const std::string &requirementKind );

} // namespace sicnu::repair
