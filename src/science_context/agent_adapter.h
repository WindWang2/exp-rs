// src/science_context/agent_adapter.h
#pragma once

//
// Qt-free JSON entry points for agent / MCP / CLI surfaces.
// The thin data_platform_tools shell converts Json::Value ↔ QVariantMap.
//

#include "science_context/broker.h"

#include <json/json.h>

#include <string>

namespace sicnu::science_context::agent_adapter {

/// Process-wide broker for tool dispatch (tests may replace via setBroker).
ScienceContextBroker &sharedBroker();
void setSharedBrokerForTest( ScienceContextBroker *broker );

/// RAII helper for overriding the shared broker in tests with guaranteed reset.
struct ScopedSharedBrokerOverride
{
    explicit ScopedSharedBrokerOverride( ScienceContextBroker *broker );
    ~ScopedSharedBrokerOverride();

    ScopedSharedBrokerOverride( const ScopedSharedBrokerOverride & ) = delete;
    ScopedSharedBrokerOverride &operator=( const ScopedSharedBrokerOverride & ) = delete;
    ScopedSharedBrokerOverride( ScopedSharedBrokerOverride && ) = delete;
    ScopedSharedBrokerOverride &operator=( ScopedSharedBrokerOverride && ) = delete;

  private:
    ScienceContextBroker *mPrevious = nullptr;
};

/// scientific:context — synthesize a bounded bundle.
Json::Value scientificContext( const Json::Value &args );

/// scientific:capabilities — capability routing only.
Json::Value scientificCapabilities( const Json::Value &args );

/// data:asset_passport — passport summary (+ optional full state).
Json::Value dataAssetPassport( const Json::Value &args );

/// recipe:search — deterministic top-K recipe hits.
Json::Value recipeSearch( const Json::Value &args );

} // namespace sicnu::science_context::agent_adapter
