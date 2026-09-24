#include "preflight/provider.h"

namespace sicnu::preflight {

const char *factStatusToString( FactStatus status )
{
    switch ( status )
    {
        case FactStatus::Available:
            return "available";
        case FactStatus::Unknown:
            return "unknown";
        case FactStatus::Unavailable:
            return "unavailable";
    }
    return "unknown";
}

// ---- MemoryFactsProvider ----------------------------------------------------

void MemoryFactsProvider::set( const std::string &assetRef, const SlotFacts &facts )
{
    SlotFactsResult r;
    r.status = FactStatus::Available;
    r.facts = facts;
    entries_[assetRef] = std::move( r );
}

void MemoryFactsProvider::setUnknown( const std::string &assetRef, const std::string &detail )
{
    SlotFactsResult r;
    r.status = FactStatus::Unknown;
    r.detail = detail;
    entries_[assetRef] = std::move( r );
}

void MemoryFactsProvider::setUnavailable( const std::string &assetRef, const std::string &detail )
{
    SlotFactsResult r;
    r.status = FactStatus::Unavailable;
    r.detail = detail;
    entries_[assetRef] = std::move( r );
}

SlotFactsResult MemoryFactsProvider::slotFacts( const std::string &assetRef ) const
{
    const auto it = entries_.find( assetRef );
    if ( it == entries_.end() )
    {
        SlotFactsResult r;
        r.status = FactStatus::Unknown;
        r.detail = "no facts registered for reference";
        return r;
    }
    return it->second;
}

SlotFacts &MemoryFactsProvider::at( const std::string &assetRef )
{
    return entries_[assetRef].facts;
}

// ---- MemoryCapabilityProvider ----------------------------------------------

void MemoryCapabilityProvider::setEntry( const std::string &operatorId, Json::Value entry )
{
    entries_[operatorId] = std::move( entry );
    unavailable_ = false;
}

void MemoryCapabilityProvider::markUnavailable( const std::string &detail )
{
    unavailable_ = true;
    detail_ = detail;
}

CapabilityEntryResult MemoryCapabilityProvider::entryForOperator(
    const std::string &operatorId, const Json::Value & ) const
{
    CapabilityEntryResult r;
    if ( unavailable_ )
    {
        r.status = FactStatus::Unavailable;
        r.detail = detail_;
        return r;
    }
    const auto it = entries_.find( operatorId );
    if ( it == entries_.end() )
    {
        r.status = FactStatus::Unknown;
        r.detail = "operator not declared by the capability authority";
        return r;
    }
    r.status = FactStatus::Available;
    r.entry = it->second;
    return r;
}

} // namespace sicnu::preflight
