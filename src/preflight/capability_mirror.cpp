#include "preflight/capability_mirror.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace sicnu::preflight {
namespace {

constexpr int kMaxMergeDepth = 4; // authority parity (capability_knowledge bound)
constexpr std::size_t kMaxEntries = 2048;

Json::Value parseText( const std::string &text, std::string &errors )
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["stackLimit"] = 1000;
    Json::Value parsed;
    std::stringstream stream( text );
    Json::parseFromStream( builder, stream, &parsed, &errors );
    return parsed;
}

std::string lowered( std::string text )
{
    std::transform( text.begin(), text.end(), text.begin(),
                    []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    return text;
}

bool stringInArray( const Json::Value &array, const std::string &value )
{
    if ( !array.isArray() )
        return false;
    const std::string needle = lowered( value );
    for ( const auto &item : array )
        if ( item.isString() && lowered( item.asString() ) == needle )
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

namespace {

/// Fail-closed validation of the policy shapes the preflight rules consume.
/// Anything malformed skips the entry (counted) so a half-declared policy can
/// never silently narrow a check.
std::string policyProblem( const Json::Value &entry )
{
    auto objectOfNonNegativeInts = []( const Json::Value &node ) {
        if ( !node.isObject() )
            return false;
        for ( const auto &key : node.getMemberNames() )
            if ( !node[key].isInt() || node[key].asInt() < 0 )
                return false;
        return true;
    };
    auto arrayOfStrings = []( const Json::Value &node ) {
        if ( !node.isArray() )
            return false;
        for ( const auto &item : node )
            if ( !item.isString() )
                return false;
        return true;
    };

    const Json::Value &roles = entry["band_roles"];
    if ( !roles.isNull() && !objectOfNonNegativeInts( roles ) )
        return "band_roles must map roles to non-negative ints";
    const Json::Value &radiometric = entry["radiometric"];
    if ( !radiometric.isNull() )
    {
        if ( !radiometric.isObject() )
            return "radiometric must be an object";
        if ( radiometric.isMember( "acceptable" ) && !arrayOfStrings( radiometric["acceptable"] ) )
            return "radiometric.acceptable must be an array of strings";
        if ( radiometric.isMember( "warn" ) && !arrayOfStrings( radiometric["warn"] ) )
            return "radiometric.warn must be an array of strings";
    }
    const Json::Value &modality = entry["modality"];
    if ( !modality.isNull() && !arrayOfStrings( modality ) )
        return "modality must be an array of strings";
    const Json::Value &temporal = entry["temporal"];
    if ( !temporal.isNull() )
    {
        if ( !temporal.isObject() )
            return "temporal must be an object";
        if ( temporal.isMember( "min_scenes" ) && !temporal["min_scenes"].isInt() )
            return "temporal.min_scenes must be an int";
        if ( temporal.isMember( "max_gap_days" ) && !temporal["max_gap_days"].isInt() )
            return "temporal.max_gap_days must be an int";
        if ( temporal.isMember( "requires_acquisition_time" ) &&
             !temporal["requires_acquisition_time"].isBool() )
            return "temporal.requires_acquisition_time must be a bool";
    }
    const Json::Value &model = entry["model_compatibility"];
    if ( !model.isNull() )
    {
        if ( !model.isObject() )
            return "model_compatibility must be an object";
        if ( model.isMember( "families" ) && !arrayOfStrings( model["families"] ) )
            return "model_compatibility.families must be an array of strings";
        if ( model.isMember( "input_band_roles" ) &&
             !objectOfNonNegativeInts( model["input_band_roles"] ) )
            return "model_compatibility.input_band_roles must map roles to non-negative ints";
    }
    return "";
}

} // namespace

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
        const std::string policyProblemText = policyProblem( entry );
        if ( !policyProblemText.empty() )
        {
            problems_.push_back( origin + ": entry " + entry["id"].asString() + ": " +
                                 policyProblemText );
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
        std::string parseErrors;
        const Json::Value parsed = parseText( buffer.str(), parseErrors );
        if ( parsed.isNull() && !parseErrors.empty() )
        {
            problems_.push_back( file.filename().string() + ": " + parseErrors );
            continue;
        }
        addDocument( parsed, file.filename().string() );
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
    // Merge chain, parent-first, mirroring the Qt authority's walk
    // (capability_knowledge.cpp): extends ancestors to the root, the family
    // default of the TERMINAL ancestor (where extends is absent), then the
    // entry itself; later sources overwrite top-level keys (child wins).
    std::vector<const Entry *> chain;
    std::set<std::string> visited;
    visited.insert( entry.value["id"].asString() );

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
    // Family fallback at the terminal level: the terminal ancestor's family
    // default (never the requested entry's own family, which the authority
    // ignores when extends is declared).
    const std::string terminalFamily =
        cursor->value["family"].isString() ? cursor->value["family"].asString() : std::string();
    if ( !terminalFamily.empty() )
    {
        const Entry *familyDefault = findEntry( "family:" + terminalFamily );
        if ( familyDefault != nullptr && familyDefault != &entry &&
             visited.insert( familyDefault->value["id"].asString() ).second )
            ancestors.push_back( familyDefault );
    }
    std::reverse( ancestors.begin(), ancestors.end() );
    for ( const Entry *ancestor : ancestors )
        chain.push_back( ancestor );
    chain.push_back( &entry );

    // Ancestors and family defaults never contribute identity or taxonomy:
    // "id" and "kind" come from the entry itself only (authority parity).
    Json::Value merged( Json::objectValue );
    for ( const Entry *source : chain )
    {
        const bool isSelf = source == &entry;
        for ( const auto &key : source->value.getMemberNames() )
        {
            bool skip = key == "extends" || key == "when";  // never copied from anyone
            if ( !isSelf && ( key == "id" || key == "kind" ) )
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
    if ( entry->value["kind"].isString() && entry->value["kind"].asString() == "family_default" )
    {
        // Authority parity: family defaults are merge sources, not operators.
        result.status = FactStatus::Unknown;
        result.detail = "family defaults are not operators";
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
