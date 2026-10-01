#include "science_context/observability.h"

namespace sicnu::science_context {

BrokerObservability::BrokerObservability( BrokerObservability &&other ) noexcept
{
    std::lock_guard<std::mutex> lock( other.mMutex );
    mMetrics = std::move( other.mMetrics );
}

BrokerObservability &BrokerObservability::operator=( BrokerObservability &&other ) noexcept
{
    if ( this != &other )
    {
        std::scoped_lock lock( mMutex, other.mMutex );
        mMetrics = std::move( other.mMetrics );
    }
    return *this;
}

void BrokerObservability::recordSynthesize( bool cacheHit, bool truncated, bool blocked,
                                            const std::string &intent, bool conflicted,
                                            bool unknown )
{
    std::lock_guard<std::mutex> lock( mMutex );
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

void BrokerObservability::recordBudgetViolation()
{
    std::lock_guard<std::mutex> lock( mMutex );
    ++mMetrics.budgetViolations;
}

BrokerMetrics BrokerObservability::metrics() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mMetrics;
}

BrokerMetrics BrokerObservability::snapshot() const
{
    return metrics();
}

Json::Value BrokerObservability::toJson() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    Json::Value o( Json::objectValue );
    o["synthesize_count"] = Json::UInt64( mMetrics.synthesizeCount );
    o["cache_hits"] = Json::UInt64( mMetrics.cacheHits );
    o["cache_misses"] = Json::UInt64( mMetrics.cacheMisses );
    o["truncations"] = Json::UInt64( mMetrics.truncations );
    o["budget_violations"] = Json::UInt64( mMetrics.budgetViolations );
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
    std::lock_guard<std::mutex> lock( mMutex );
    mMetrics = BrokerMetrics{};
}

} // namespace sicnu::science_context
