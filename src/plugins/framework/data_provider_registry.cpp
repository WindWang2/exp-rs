/***************************************************************************
 * src/plugins/framework/data_provider_registry.cpp
 ***************************************************************************/
#include "data_provider_registry.h"

namespace sicnu::plugins {

DataProviderRegistry &DataProviderRegistry::instance()
{
    static DataProviderRegistry registry;
    return registry;
}

bool DataProviderRegistry::registerProvider(
    const std::string &pluginId, const std::string &providerId, const std::string &displayName,
    const std::string &description, const std::vector<std::string> &schemes,
    std::shared_ptr<exprs::IPluginDataProviderV1> provider )
{
    if ( providerId.empty() || !provider )
        return false;
    std::lock_guard<std::mutex> lock( mMutex );
    for ( Entry &entry : mEntries )
    {
        if ( entry.providerId == providerId )
        {
            entry.pluginId = pluginId;
            entry.displayName = displayName;
            entry.description = description;
            entry.schemes = schemes;
            entry.provider = std::move( provider );
            return true;
        }
    }
    Entry entry;
    entry.pluginId = pluginId;
    entry.providerId = providerId;
    entry.displayName = displayName;
    entry.description = description;
    entry.schemes = schemes;
    entry.provider = std::move( provider );
    mEntries.push_back( std::move( entry ) );
    return true;
}

void DataProviderRegistry::unregisterPlugin( const std::string &pluginId )
{
    std::lock_guard<std::mutex> lock( mMutex );
    mEntries.erase( std::remove_if( mEntries.begin(), mEntries.end(),
                                    [&]( const Entry &entry ) {
                                        return entry.pluginId == pluginId;
                                    } ),
                    mEntries.end() );
}

const DataProviderRegistry::Entry *DataProviderRegistry::find(
    const std::string &providerId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    for ( const Entry &entry : mEntries )
    {
        if ( entry.providerId == providerId )
            return &entry;
    }
    return nullptr;
}

const DataProviderRegistry::Entry *DataProviderRegistry::findByScheme(
    const std::string &scheme ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    for ( const Entry &entry : mEntries )
    {
        for ( const std::string &candidate : entry.schemes )
        {
            if ( !candidate.empty() && scheme.rfind( candidate, 0 ) == 0 )
                return &entry;
        }
    }
    return nullptr;
}

std::vector<DataProviderRegistry::Entry> DataProviderRegistry::providers() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mEntries;
}

size_t DataProviderRegistry::count() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mEntries.size();
}

} // namespace sicnu::plugins
