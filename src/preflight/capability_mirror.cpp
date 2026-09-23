#include "preflight/capability_mirror.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace sicnu::preflight {
namespace {

constexpr int kMaxMergeDepth = 16;
constexpr std::size_t kMaxEntries = 2048;

Json::Value parseText( const std::string &text )
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["stackLimit"] = 1000;
    Json::Value parsed;
    std::string errors;
    std::stringstream stream( text );
    Json::parseFromStream( builder, stream, &parsed, &errors );
    return parsed;
}

bool stringInArray( const Json::Value &array, const std::string &value )
{
    if ( !array.isArray() )
        return false;
    for ( const auto &item : array )
        if ( item.isString() && item.asString() == value )
            return true;
    return false;
}

} // namespace

Json::Value makeVariantParams( const std::string &key, const std::string &value )
{
    Json::Value params( Json::objectValue );
    params[key] = value;
    return params;
}

const CapabilityMirrorProjection::Entry *CapabilityMirrorProjection::findEntry(
    const std::string &id ) const
{
    for ( const auto &entry : entries_ )
        if ( entry.value.isObject() && entry.value["id"].isString() &&
             entry.value["id"].asString() == id )
            return &entry;
    return nullptr;
}

void CapabilityMirrorProjection::addDocument( const Json::Value &documentArray,
                                              const std::string &origin )
{
    configured_ = true;
    if ( !documentArray.isArray() )
    {
        problems_.push_back( origin + ": document is not a top-level array" );
        return;
    }
    std::set<std::string> seen;
    for ( const auto &entry : documentArray )
    {
        if ( !entry.isObject() || !entry["id"].isString() )
        {
            problems_.push_back( origin + ": entry without a string id" );
            continue;
        }
        const std::string id = entry["id"].asString();
        if ( !seen.insert( id ).second || findEntry( id ) != nullptr )
        {
            problems_.push_back( origin + ": duplicate entry id " + id );
            continue;
        }
        if ( entries_.size() >= kMaxEntries )
        {
            problems_.push_back( origin + ": entry cap exceeded" );
            return;
        }
        entries_.push_back( Entry{ entry, origin } );
    }
}

int CapabilityMirrorProjection::loadDirectory( const std::string &directory )
{
    configured_ = true;
    std::error_code ec;
    if ( !std::filesystem::is_directory( directory, ec ) )
    {
        loadFailed_ = true;
        problems_.push_back( directory + ": mirror directory missing or unreadable" );
        return 0;
    }
    std::vector<std::filesystem::path> files;
    for ( const auto &item : std::filesystem::directory_iterator( directory, ec ) )
    {
        std::error_code fileEc;
        if ( item.is_regular_file( fileEc ) && item.path().extension() == ".json" )
            files.push_back( item.path() );
    }
    std::sort( files.begin(), files.end() ); // deterministic load order
    int loaded = 0;
    for ( const auto &file : files )
    {
        std::ifstream in( file, std::ios::binary );
        if ( !in )
        {
            problems_.push_back( file.filename().string() + ": unreadable" );
            continue;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        addDocument( parseText( buffer.str() ), file.filename().string() );
        ++loaded;
    }
    return loaded;
}

bool CapabilityMirrorProjection::configured() const
{
    return configured_;
}

bool CapabilityMirrorProjection::healthy() const
{
    return configured_ && problems_.empty();
}

const std::vector<std::string> &CapabilityMirrorProjection::problems() const
{
    return problems_;
}

Json::Value CapabilityMirrorProjection::mergeEntry( const Entry &entry ) const
{
    // Merge chain, parent-first: family default, extends ancestors, the
    // entry, then the first matching variant. Later sources overwrite
    // top-level keys (child wins).
    std::vector<const Entry *> chain;
    std::set<std::string> visited;
    visited.insert( entry.value["id"].asString() );

    const std::string family =
        entry.value["family"].isString() ? entry.value["family"].asString() : std::string();
    if ( !family.empty() )
    {
        const Entry *familyDefault = findEntry( "family:" + family );
        if ( familyDefault != nullptr && familyDefault != &entry &&
             visited.insert( familyDefault->value["id"].asString() ).second )
            chain.push_back( familyDefault );
    }
    // Walk the extends chain to the root, then apply root-first.
    std::vector<const Entry *> ancestors;
    const Entry *cursor = &entry;
    int depth = 0;
    while ( cursor->value["extends"].isString() && depth < kMaxMergeDepth )
    {
        const Entry *parent = findEntry( cursor->value["extends"].asString() );
        if ( parent == nullptr || !visited.insert( parent->value["id"].asString() ).second )
            break; // unknown parent or cycle: bounded, fail-closed stop
        ancestors.push_back( parent );
        cursor = parent;
        ++depth;
    }
    std::reverse( ancestors.begin(), ancestors.end() );
    for ( const Entry *ancestor : ancestors )
        chain.push_back( ancestor );
    chain.push_back( &entry );

    static const char *kNeverCopied[] = { "extends", "when" };
    Json::Value merged( Json::objectValue );
    for ( const Entry *source : chain )
    {
        for ( const auto &key : source->value.getMemberNames() )
        {
            bool skip = false;
            for ( const char *never : kNeverCopied )
                if ( key == never )
                    skip = true;
            if ( !skip )
                merged[key] = source->value[key];
        }
    }
    return merged;
}

CapabilityEntryResult CapabilityMirrorProjection::entryForOperator(
    const std::string &operatorId, const Json::Value &variantParams ) const
{
    CapabilityEntryResult result;
    if ( !configured_ )
    {
        result.status = FactStatus::Unknown;
        result.detail = "capability mirror not configured";
        return result;
    }
    if ( !healthy() )
    {
        result.status = FactStatus::Unavailable;
        result.detail = "capability mirror failed to load: " +
                        ( problems_.empty() ? std::string( "unknown problem" ) : problems_.front() );
        return result;
    }
    const Entry *entry = findEntry( operatorId );
    if ( entry == nullptr )
    {
        result.status = FactStatus::Unknown;
        result.detail = "operator not declared in the capability mirror";
        return result;
    }

    Json::Value merged = mergeEntry( *entry );
    const Json::Value &variants = entry->value["variants"];
    if ( variants.isArray() && variantParams.isObject() )
    {
        for ( const auto &variant : variants )
        {
            if ( !variant.isObject() )
                continue;
            const Json::Value &when = variant["when"];
            if ( !when.isObject() )
                continue;
            // Schema: when = { param: <name>, values: [<accepted values>] }.
            const Json::Value &paramName = when["param"];
            const Json::Value &values = when["values"];
            const Json::Value &given =
                paramName.isString() ? variantParams[paramName.asString()] : Json::Value();
            const bool matches = values.isArray() && given.isString() &&
                                 stringInArray( values, given.asString() );
            if ( !matches )
                continue;
            for ( const auto &key : variant.getMemberNames() )
            {
                if ( key != "when" )
                    merged[key] = variant[key];
            }
            break; // first matching variant only
        }
    }
    result.status = FactStatus::Available;
    result.entry = std::move( merged );
    return result;
}

std::vector<std::string> CapabilityMirrorProjection::entryIds() const
{
    std::vector<std::string> ids;
    ids.reserve( entries_.size() );
    for ( const auto &entry : entries_ )
        if ( entry.value["id"].isString() )
            ids.push_back( entry.value["id"].asString() );
    std::sort( ids.begin(), ids.end() );
    return ids;
}

} // namespace sicnu::preflight
