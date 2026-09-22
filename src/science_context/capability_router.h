// src/science_context/capability_router.h
#pragma once

#include "science_context/bundle.h"
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
};

struct CapabilityRouterResult
{
    std::string intent;
    std::string intentStatus;
    std::vector<CapabilityEntry> entries;
    std::vector<std::string> openQuestions;
};

CapabilityRouterResult routeCapabilities( const CapabilityQuery &query );
bool isKnownBrokerIntent( const std::string &intent );
std::string resolveIntentFromGoal( const std::string &goalText, std::string *status = nullptr );

} // namespace sicnu::science_context
