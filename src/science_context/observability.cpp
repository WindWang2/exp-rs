// src/science_context/observability.cpp
#include "science_context/observability.h"

namespace sicnu::science_context {

void BrokerObservability::recordSynthesize( bool cacheHit, bool truncated, bool blocked,
                                            const std::string &intent, bool conflicted,
                                            bool unknown )
{
    ++mMetrics.synthesizeCount;
    if ( cacheHit )
        ++mMetrics.cacheHits;
    else
        ++mMetrics.cacheMisses;
    if ( truncated )
        ++mMetrics.truncations;
    if ( blocked )
        ++mMetrics.blockedPlans;
    if ( conflicted )
        ++mMetrics.conflictedAssets;
    if ( unknown )
        ++mMetrics.unknownAssets;
    if ( !intent.empty() )
    {
        mMetrics.lastIntentPath.push_back( intent );
        if ( mMetrics.lastIntentPath.size() > 16 )
            mMetrics.lastIntentPath.erase( mMetrics.lastIntentPath.begin() );
    }
}

BrokerMetrics BrokerObservability::snapshot() const
{
    return mMetrics;
}

Json::Value BrokerObservability::toJson() const
{
    Json::Value o( Json::objectValue );
    o["synthesize_count"] = Json::UInt64( mMetrics.synthesizeCount );
    o["cache_hits"] = Json::UInt64( mMetrics.cacheHits );
    o["cache_misses"] = Json::UInt64( mMetrics.cacheMisses );
    o["truncations"] = Json::UInt64( mMetrics.truncations );
    o["blocked_plans"] = Json::UInt64( mMetrics.blockedPlans );
    o["conflicted_assets"] = Json::UInt64( mMetrics.conflictedAssets );
    o["unknown_assets"] = Json::UInt64( mMetrics.unknownAssets );
    Json::Value path( Json::arrayValue );
    for ( const auto &i : mMetrics.lastIntentPath )
        path.append( i );
    o["last_intent_path"] = path;
    return o;
}

void BrokerObservability::reset()
{
    mMetrics = BrokerMetrics{};
}

} // namespace sicnu::science_context
