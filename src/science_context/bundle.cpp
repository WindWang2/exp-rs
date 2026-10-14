// src/science_context/bundle.cpp
#include "science_context/bundle.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>

namespace sicnu::science_context {

namespace {

std::uint64_t fnv1a64( const std::string &s )
{
    std::uint64_t h = 14695981039346656037ull;
    for ( unsigned char c : s )
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex16( std::uint64_t v )
{
    static const char *kHex = "0123456789abcdef";
    std::string out( 16, '0' );
    for ( int i = 15; i >= 0; --i )
    {
        out[static_cast<std::size_t>( i )] = kHex[v & 0xf];
        v >>= 4;
    }
    return out;
}

Json::Value stringArray( const std::vector<std::string> &v )
{
    Json::Value a( Json::arrayValue );
    for ( const auto &s : v )
        a.append( s );
    return a;
}

std::vector<std::string> readStringArray( const Json::Value &a )
{
    std::vector<std::string> out;
    if ( !a.isArray() )
        return out;
    for ( const auto &x : a )
    {
        if ( x.isString() )
            out.push_back( x.asString() );
    }
    return out;
}

Json::Value assetToJson( const AssetSummary &a )
{
    Json::Value o( Json::objectValue );
    o["asset_id"] = a.assetId;
    o["revision"] = a.revision;
    o["display_name"] = a.displayName;
    o["modality"] = a.modality;
    o["radiometric_unit"] = a.radiometricUnit;
    o["crs_authid"] = a.crsAuthid;
    o["band_roles"] = stringArray( a.bandRoles );
    o["evidence"] = evidenceBucketToString( a.evidence );
    o["evidence_paths"] = stringArray( a.evidencePaths );
    o["conflict_alternatives"] = stringArray( a.conflictAlternatives );
    o["path_hint"] = a.pathHint;
    o["source"] = a.source;
    return o;
}

Json::Value capToJson( const CapabilityEntry &c )
{
    Json::Value o( Json::objectValue );
    o["capability_id"] = c.capabilityId;
    o["intent"] = c.intent;
    o["status"] = c.status;
    o["score"] = c.score;
    o["reasons"] = stringArray( c.reasons );
    o["prep_actions"] = stringArray( c.prepActions );
    o["structural_only"] = c.structuralOnly;
    return o;
}

Json::Value recipeToJson( const RecipeEntry &r )
{
    Json::Value o( Json::objectValue );
    o["recipe_id"] = r.recipeId;
    o["title"] = r.title;
    o["intent"] = r.intent;
    o["modality"] = r.modality;
    o["score"] = r.score;
    o["stage_count"] = r.stageCount;
    o["has_human_only"] = r.hasHumanOnly;
    o["has_verifier_hooks"] = r.hasVerifierHooks;
    o["matched"] = stringArray( r.matched );
    return o;
}

} // namespace

std::string contentSourceToString( ContentSource source )
{
    switch ( source )
    {
        case ContentSource::Unavailable:
            return "unavailable";
        case ContentSource::InlineInput:
            return "inline_input";
        case ContentSource::BuiltinFallback:
            return "builtin_fallback";
        case ContentSource::LiveAuthority:
            return "live_authority";
    }
    return "unavailable";
}

bool contentSourceFromString( const std::string &text, ContentSource &out )
{
    if ( text == "unavailable" )
    {
        out = ContentSource::Unavailable;
        return true;
    }
    if ( text == "inline_input" )
    {
        out = ContentSource::InlineInput;
        return true;
    }
    if ( text == "builtin_fallback" )
    {
        out = ContentSource::BuiltinFallback;
        return true;
    }
    if ( text == "live_authority" )
    {
        out = ContentSource::LiveAuthority;
        return true;
    }
    return false;
}

namespace {

Json::Value sectionSourceToJson( const SectionSource &s )
{
    Json::Value o( Json::objectValue );
    o["source"] = contentSourceToString( s.source );
    o["authority"] = s.authority;
    o["revision"] = Json::UInt64( s.revision );
    o["degraded"] = s.degraded;
    return o;
}

std::string readString( const Json::Value &val, const std::string &key, const std::string &fallback = "" )
{
    if ( val.isObject() && val.isMember( key ) && val[key].isString() )
        return val[key].asString();
    return fallback;
}

SectionSource sectionSourceFromJson( const Json::Value &o )
{
    SectionSource s;
    if ( !o.isObject() )
        return s;
    contentSourceFromString( readString( o, "source", "unavailable" ), s.source );
    s.authority = readString( o, "authority", "" );
    s.revision = o.isMember( "revision" ) && o["revision"].isUInt64() ? o["revision"].asUInt64() : 0;
    s.degraded = o.isMember( "degraded" ) && o["degraded"].isBool() ? o["degraded"].asBool() : false;
    return s;
}

} // namespace

std::string evidenceBucketToString( EvidenceBucket bucket )
{
    switch ( bucket )
    {
        case EvidenceBucket::Known:
            return "known";
        case EvidenceBucket::Assumed:
            return "assumed";
        case EvidenceBucket::Unknown:
            return "unknown";
        case EvidenceBucket::Conflicted:
            return "conflicted";
    }
    return "unknown";
}

bool evidenceBucketFromString( const std::string &text, EvidenceBucket &out )
{
    if ( text == "known" )
    {
        out = EvidenceBucket::Known;
        return true;
    }
    if ( text == "assumed" )
    {
        out = EvidenceBucket::Assumed;
        return true;
    }
    if ( text == "unknown" )
    {
        out = EvidenceBucket::Unknown;
        return true;
    }
    if ( text == "conflicted" )
    {
        out = EvidenceBucket::Conflicted;
        return true;
    }
    return false;
}

Json::Value bundleToJson( const ScientificContextBundle &bundle )
{
    Json::Value root( Json::objectValue );
    root["schema"] = bundle.schemaId;
    root["bundle_id"] = bundle.bundleId;
    root["goal"] = bundle.goal;
    root["intent"] = bundle.intent;

    Json::Value assets( Json::arrayValue );
    for ( const auto &a : bundle.assets )
        assets.append( assetToJson( a ) );
    root["assets"] = assets;

    Json::Value caps( Json::arrayValue );
    for ( const auto &c : bundle.capabilities )
        caps.append( capToJson( c ) );
    root["capabilities"] = caps;

    Json::Value recipes( Json::arrayValue );
    for ( const auto &r : bundle.recipes )
        recipes.append( recipeToJson( r ) );
    root["recipes"] = recipes;

    Json::Value constraints( Json::objectValue );
    constraints["autonomy_level"] = bundle.constraints.autonomyLevel;
    constraints["allow_autonomous_exec"] = bundle.constraints.allowAutonomousExec;
    constraints["offline"] = bundle.constraints.offline;
    constraints["max_bytes"] = bundle.constraints.maxBytes;
    constraints["max_recipes"] = bundle.constraints.maxRecipes;
    constraints["max_capabilities"] = bundle.constraints.maxCapabilities;
    constraints["determinism_required"] = bundle.constraints.determinismRequired;
    root["constraints"] = constraints;

    root["open_questions"] = stringArray( bundle.openQuestions );

    Json::Value planner( Json::objectValue );
    planner["goal"] = bundle.planner.goal;
    planner["intent"] = bundle.planner.intent;
    planner["recipe_id"] = bundle.planner.recipeId;
    planner["input_facts"] = bundle.planner.inputFacts;
    planner["missing_facts"] = bundle.planner.missingFacts;
    planner["limitations"] = bundle.planner.limitations;
    planner["open_questions"] = bundle.planner.openQuestions;
    planner["execution_blocked"] = bundle.planner.executionBlocked;
    planner["block_reason"] = bundle.planner.blockReason;
    root["planner_projection"] = planner;

    Json::Value trunc( Json::objectValue );
    trunc["truncated"] = bundle.truncation.truncated;
    trunc["sections"] = stringArray( bundle.truncation.sections );
    trunc["dropped_recipes"] = bundle.truncation.droppedRecipes;
    trunc["dropped_capabilities"] = bundle.truncation.droppedCapabilities;
    trunc["dropped_questions"] = bundle.truncation.droppedQuestions;
    trunc["dropped_assets"] = bundle.truncation.droppedAssets;
    trunc["original_bytes"] = bundle.truncation.originalBytes;
    trunc["final_bytes"] = bundle.truncation.finalBytes;
    root["truncation"] = trunc;

    Json::Value sources( Json::objectValue );
    sources["assets"] = sectionSourceToJson( bundle.sources.assets );
    sources["capabilities"] = sectionSourceToJson( bundle.sources.capabilities );
    sources["recipes"] = sectionSourceToJson( bundle.sources.recipes );
    sources["planner_facts"] = sectionSourceToJson( bundle.sources.plannerFacts );
    root["sources"] = sources;

    root["observability"] = bundle.observability;
    return root;
}

bool bundleFromJson( const Json::Value &json, ScientificContextBundle &out, std::string *error )
{
    if ( !json.isObject() )
    {
        if ( error )
            *error = "bundle root must be object";
        return false;
    }
    const std::string schema = readString( json, "schema", "" );
    if ( schema != kBundleSchemaId )
    {
        if ( error )
            *error = "schema mismatch: expected " + std::string( kBundleSchemaId );
        return false;
    }
    ScientificContextBundle b;
    b.schemaId = schema;
    b.bundleId = readString( json, "bundle_id", "" );
    b.goal = readString( json, "goal", "" );
    b.intent = readString( json, "intent", "" );

    for ( const auto &a : json["assets"] )
    {
        if ( !a.isObject() )
        {
            if ( error )
                *error = "assets entry must be object";
            return false;
        }
        AssetSummary s;
        s.assetId = readString( a, "asset_id", "" );
        s.revision = readString( a, "revision", "" );
        s.displayName = readString( a, "display_name", "" );
        s.modality = readString( a, "modality", "" );
        s.radiometricUnit = readString( a, "radiometric_unit", "" );
        s.crsAuthid = readString( a, "crs_authid", "" );
        s.bandRoles = readStringArray( a["band_roles"] );
        EvidenceBucket bucket = EvidenceBucket::Unknown;
        evidenceBucketFromString( readString( a, "evidence", "unknown" ), bucket );
        s.evidence = bucket;
        s.evidencePaths = readStringArray( a["evidence_paths"] );
        s.conflictAlternatives = readStringArray( a["conflict_alternatives"] );
        s.pathHint = readString( a, "path_hint", "" );
        s.source = readString( a, "source", "unavailable" );
        b.assets.push_back( std::move( s ) );
    }

    for ( const auto &c : json["capabilities"] )
    {
        if ( !c.isObject() )
            continue;
        CapabilityEntry e;
        e.capabilityId = readString( c, "capability_id", "" );
        e.intent = readString( c, "intent", "" );
        e.status = readString( c, "status", "" );
        e.score = c.isMember( "score" ) && c["score"].isDouble() ? c["score"].asDouble() : 0.0;
        e.reasons = readStringArray( c["reasons"] );
        e.prepActions = readStringArray( c["prep_actions"] );
        e.structuralOnly = c.isMember( "structural_only" ) && c["structural_only"].isBool() ? c["structural_only"].asBool() : true;
        b.capabilities.push_back( std::move( e ) );
    }

    for ( const auto &r : json["recipes"] )
    {
        if ( !r.isObject() )
            continue;
        RecipeEntry e;
        e.recipeId = readString( r, "recipe_id", "" );
        e.title = readString( r, "title", "" );
        e.intent = readString( r, "intent", "" );
        e.modality = readString( r, "modality", "" );
        e.score = r.isMember( "score" ) && r["score"].isDouble() ? r["score"].asDouble() : 0.0;
        e.stageCount = r.isMember( "stage_count" ) && r["stage_count"].isInt() ? r["stage_count"].asInt() : 0;
        e.hasHumanOnly = r.isMember( "has_human_only" ) && r["has_human_only"].isBool() ? r["has_human_only"].asBool() : false;
        e.hasVerifierHooks = r.isMember( "has_verifier_hooks" ) && r["has_verifier_hooks"].isBool() ? r["has_verifier_hooks"].asBool() : false;
        e.matched = readStringArray( r["matched"] );
        b.recipes.push_back( std::move( e ) );
    }

    const Json::Value &constraints = json["constraints"];
    if ( constraints.isObject() )
    {
        b.constraints.autonomyLevel = readString( constraints, "autonomy_level", "L2" );
        b.constraints.allowAutonomousExec = constraints.isMember( "allow_autonomous_exec" ) && constraints["allow_autonomous_exec"].isBool() ? constraints["allow_autonomous_exec"].asBool() : false;
        b.constraints.offline = constraints.isMember( "offline" ) && constraints["offline"].isBool() ? constraints["offline"].asBool() : false;
        b.constraints.maxBytes = constraints.isMember( "max_bytes" ) && constraints["max_bytes"].isInt() ? constraints["max_bytes"].asInt() : 65536;
        b.constraints.maxRecipes = constraints.isMember( "max_recipes" ) && constraints["max_recipes"].isInt() ? constraints["max_recipes"].asInt() : 5;
        b.constraints.maxCapabilities = constraints.isMember( "max_capabilities" ) && constraints["max_capabilities"].isInt() ? constraints["max_capabilities"].asInt() : 8;
        b.constraints.determinismRequired = constraints.isMember( "determinism_required" ) && constraints["determinism_required"].isBool() ? constraints["determinism_required"].asBool() : true;
    }

    b.openQuestions = readStringArray( json["open_questions"] );

    const Json::Value &planner = json["planner_projection"];
    if ( planner.isObject() )
    {
        b.planner.goal = readString( planner, "goal", "" );
        b.planner.intent = readString( planner, "intent", "" );
        b.planner.recipeId = readString( planner, "recipe_id", "" );
        b.planner.inputFacts = planner.isMember( "input_facts" ) && planner["input_facts"].isObject() ? planner["input_facts"] : Json::Value( Json::objectValue );
        b.planner.missingFacts = planner.isMember( "missing_facts" ) && planner["missing_facts"].isArray() ? planner["missing_facts"] : Json::Value( Json::arrayValue );
        b.planner.limitations = planner.isMember( "limitations" ) && planner["limitations"].isArray() ? planner["limitations"] : Json::Value( Json::arrayValue );
        b.planner.openQuestions = planner.isMember( "open_questions" ) && planner["open_questions"].isArray() ? planner["open_questions"] : Json::Value( Json::arrayValue );
        b.planner.executionBlocked = planner.isMember( "execution_blocked" ) && planner["execution_blocked"].isBool() ? planner["execution_blocked"].asBool() : false;
        b.planner.blockReason = readString( planner, "block_reason", "" );
    }

    const Json::Value &trunc = json["truncation"];
    if ( trunc.isObject() )
    {
        b.truncation.truncated = trunc.isMember( "truncated" ) && trunc["truncated"].isBool() ? trunc["truncated"].asBool() : false;
        b.truncation.sections = readStringArray( trunc["sections"] );
        b.truncation.droppedRecipes = trunc.isMember( "dropped_recipes" ) && trunc["dropped_recipes"].isInt() ? trunc["dropped_recipes"].asInt() : 0;
        b.truncation.droppedCapabilities = trunc.isMember( "dropped_capabilities" ) && trunc["dropped_capabilities"].isInt() ? trunc["dropped_capabilities"].asInt() : 0;
        b.truncation.droppedQuestions = trunc.isMember( "dropped_questions" ) && trunc["dropped_questions"].isInt() ? trunc["dropped_questions"].asInt() : 0;
        b.truncation.droppedAssets = trunc.isMember( "dropped_assets" ) && trunc["dropped_assets"].isInt() ? trunc["dropped_assets"].asInt() : 0;
        b.truncation.originalBytes = trunc.isMember( "original_bytes" ) && trunc["original_bytes"].isInt() ? trunc["original_bytes"].asInt() : 0;
        b.truncation.finalBytes = trunc.isMember( "final_bytes" ) && trunc["final_bytes"].isInt() ? trunc["final_bytes"].asInt() : 0;
    }

    const Json::Value &sources = json["sources"];
    if ( sources.isObject() )
    {
        b.sources.assets = sectionSourceFromJson( sources["assets"] );
        b.sources.capabilities = sectionSourceFromJson( sources["capabilities"] );
        b.sources.recipes = sectionSourceFromJson( sources["recipes"] );
        b.sources.plannerFacts = sectionSourceFromJson( sources["planner_facts"] );
    }

    b.observability = json.get( "observability", Json::objectValue );
    out = std::move( b );
    return true;
}

std::string serializeBundle( const ScientificContextBundle &bundle )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return Json::writeString( builder, bundleToJson( bundle ) );
}

std::string computeBundleId( const ScientificContextBundle &bundle )
{
    ScientificContextBundle copy = bundle;
    copy.bundleId.clear();
    copy.observability = Json::Value( Json::objectValue );
    return hex16( fnv1a64( serializeBundle( copy ) ) );
}

} // namespace sicnu::science_context
