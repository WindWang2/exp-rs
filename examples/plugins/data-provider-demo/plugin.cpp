// examples/plugins/data-provider-demo/plugin.cpp
//
// Data provider plugin: a tiny in-memory "catalog" provider for
// demo-catalog:// URIs backed by a JSON index file shipped in the plugin
// resources. Demonstrates the discover/inspect/open contract; open() hands
// out plain file paths the host data layer can consume directly.
#include "exprs/plugin_interface.h"

#include <json/json.h>

#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace {

Json::Value loadIndex( const std::string &path )
{
    std::ifstream input( path );
    if ( !input )
        return Json::Value( Json::nullValue );
    std::stringstream buffer;
    buffer << input.rdbuf();
    Json::Value parsed;
    Json::Reader reader;
    if ( !reader.parse( buffer.str(), parsed, false ) )
        return Json::Value( Json::nullValue );
    return parsed;
}

class DemoCatalogProvider : public exprs::IPluginDataProviderV1
{
public:
    explicit DemoCatalogProvider( std::string indexPath ) : mIndexPath( std::move( indexPath ) ) {}

    Json::Value discover( const Json::Value &query ) override
    {
        const Json::Value index = indexLocked();
        Json::Value items( Json::arrayValue );
        if ( !index.isArray() )
            return items;
        int page = 0;
        if ( query.isObject() && query.isMember( "page" ) && query["page"].isIntegral() )
            page = query["page"].asInt();
        constexpr int kPageSize = 50;
        const int begin = page * kPageSize;
        for ( Json::ArrayIndex index_ = begin; index_ < index.size()
              && index_ < static_cast<Json::ArrayIndex>( begin + kPageSize );
              ++index_ )
            items.append( index[index_] );
        return items;
    }

    Json::Value inspect( const std::string &uri ) override
    {
        const Json::Value entry = findEntry( uri );
        Json::Value metadata( Json::objectValue );
        metadata["uri"] = uri;
        metadata["found"] = !entry.isNull();
        if ( !entry.isNull() && entry.isObject() )
        {
            metadata["name"] = entry.get( "name", "" );
            metadata["type"] = entry.get( "type", "" );
            metadata["description"] = entry.get( "description", "" );
        }
        return metadata;
    }

    Json::Value open( const std::string &uri ) override
    {
        const Json::Value entry = findEntry( uri );
        if ( !entry.isObject() )
        {
            Json::Value failure( Json::objectValue );
            failure["error"] = "unknown demo-catalog uri: " + uri;
            return failure;
        }
        Json::Value reference( Json::objectValue );
        reference["kind"] = entry.get( "type", "raster" );
        reference["path"] = entry.get( "path", "" );
        Json::Value metadata( Json::objectValue );
        metadata["from"] = "org.example.data-provider-demo";
        reference["metadata"] = metadata;
        return reference;
    }

    Json::Value capabilities() const override
    {
        Json::Value capabilities( Json::objectValue );
        capabilities["maxPageSize"] = 50;
        capabilities["auth"] = "none";
        capabilities["schemes"] = Json::Value( Json::arrayValue );
        capabilities["schemes"].append( "demo-catalog://" );
        return capabilities;
    }

private:
    const std::string &indexPath() const { return mIndexPath; }

    Json::Value findEntry( const std::string &uri ) const
    {
        const std::string prefix = "demo-catalog://";
        if ( uri.rfind( prefix, 0 ) != 0 )
            return Json::Value( Json::nullValue );
        const std::string name = uri.substr( prefix.size() );
        const Json::Value index = indexLocked();
        if ( !index.isArray() )
            return Json::Value( Json::nullValue );
        for ( const Json::Value &entry : index )
        {
            if ( entry.isObject() && entry.get( "name", "" ).asString() == name )
                return entry;
        }
        return Json::Value( Json::nullValue );
    }

    Json::Value indexLocked() const
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( !mIndex.isNull() )
            return mIndex;
        mIndex = loadIndex( mIndexPath );
        return mIndex;
    }

    std::string mIndexPath;
    mutable std::mutex mMutex;
    mutable Json::Value mIndex;
};

class DataProviderDemoPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override { return "org.example.data-provider-demo"; }

    bool initialize( exprs::HostServicesV1 &services ) override
    {
        // The index ships inside the plugin package (resources/demo_index.json).
        const std::string dir = services.pluginDirectory();
        mIndexPath = dir.empty() ? "resources/demo_index.json"
                                 : dir + "/resources/demo_index.json";
        return true;
    }

    void registerContributions( exprs::ContributionContextV1 &context ) override
    {
        context.registerDataProvider(
            "demo:catalog", std::make_shared<DemoCatalogProvider>( mIndexPath ) );
    }

    void shutdown() override {}

private:
    std::string mIndexPath;
};

} // namespace

EXPRS_EXPORT_PLUGIN( DataProviderDemoPlugin )
