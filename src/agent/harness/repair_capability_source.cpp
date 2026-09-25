// src/agent/harness/repair_capability_source.cpp
#include "agent/harness/repair_capability_source.h"

#include "agent/harness/capability_knowledge.h"

namespace sicnu::agent::harness {

std::vector<Json::Value> liveRepairCapabilityEntries()
{
    CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
    if (!knowledge.loaded())
        knowledge.reload();

    // Raw, shipped documents: the provider seam's contract is the
    // as-shipped entry (id, family, resource) — the merged view strips the
    // envelope keys (id among them) on purpose, so it cannot route.
    std::vector<Json::Value> entries;
    for (const std::string &id : knowledge.entryIds())
    {
        const Json::Value entry = knowledge.rawEntry(id);
        if (entry.isObject())
            entries.push_back(entry);
    }
    return entries;
}

bool liveRepairCapabilityProvider(sicnu::repair::JsonRepairCapabilityProvider &out,
                                  sicnu::repair::RepairError &error)
{
    return sicnu::repair::JsonRepairCapabilityProvider::buildFromCapabilityEntries(
        liveRepairCapabilityEntries(), out, error);
}

} // namespace sicnu::agent::harness
