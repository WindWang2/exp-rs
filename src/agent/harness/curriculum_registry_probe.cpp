// src/agent/harness/curriculum_registry_probe.cpp
#include "agent/harness/curriculum_registry_probe.h"

#include "agent/harness/capability_knowledge.h"
#include "operators/framework/rs_operator_registry.h"

namespace sicnu::agent::harness {

CurriculumOperatorProbes defaultCurriculumProbes()
{
    CurriculumOperatorProbes probes;
    probes.registered = []( const std::string &operatorId ) {
        return sicnu::operators::RSOperatorRegistry::instance().hasOperator( operatorId );
    };
    probes.capabilityNote = []( const std::string &operatorId ) {
        const Json::Value entry = CapabilityKnowledge::instance().entryForOperator( operatorId );
        return !entry.isNull();
    };
    return probes;
}

} // namespace sicnu::agent::harness
