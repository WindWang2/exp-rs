// src/science_context/capability_router.h
#pragma once

#include "science_context/bundle.h"
#include "science_context/capability_facts.h"
#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::science_context {

struct CapabilityQuery
{
    std::string goal;
    std::string intent;
    Json::Value observedState{Json::objectValue};
    ContextConstraints constraints;
    int limit = 8;
    /// Live capability authority; nullptr ⇒ the router's builtin table runs
    /// and the bundle provenance reports builtin_fallback/degraded.
    const CapabilityFactsLookup *facts = nullptr;
};

struct CapabilityRouterResult
{
    std::string intent;
    std::string intentStatus;
    std::vector<CapabilityEntry> entries;
    std::vector<std::string> openQuestions;
    // Provenance of the capability facts that produced the entries.
    bool factsFromAuthority = false;
    std::string factsAuthority;
    std::uint64_t factsRevision = 0;
    bool presenceUnknown = false; ///< operator registry presence unverifiable
};

CapabilityRouterResult routeCapabilities( const CapabilityQuery &query );
bool isKnownBrokerIntent( const std::string &intent );
std::string resolveIntentFromGoal( const std::string &goalText, std::string *status = nullptr );

} // namespace sicnu::science_context
