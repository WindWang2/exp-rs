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
    trunc["original_bytes"] = bundle.truncation.originalBytes;
    trunc["final_bytes"] = bundle.truncation.finalBytes;
    root["truncation"] = trunc;

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
    const std::string schema = json.get( "schema", "" ).asString();
    if ( schema != kBundleSchemaId )
    {
        if ( error )
            *error = "schema mismatch: expected " + std::string( kBundleSchemaId );
        return false;
    }
    ScientificContextBundle b;
    b.schemaId = schema;
    b.bundleId = json.get( "bundle_id", "" ).asString();
    b.goal = json.get( "goal", "" ).asString();
    b.intent = json.get( "intent", "" ).asString();

    for ( const auto &a : json["assets"] )
    {
        if ( !a.isObject() )
        {
            if ( error )
                *error = "assets entry must be object";
            return false;
        }
        AssetSummary s;
        s.assetId = a.get( "asset_id", "" ).asString();
        s.revision = a.get( "revision", "" ).asString();
        s.displayName = a.get( "display_name", "" ).asString();
        s.modality = a.get( "modality", "" ).asString();
        s.radiometricUnit = a.get( "radiometric_unit", "" ).asString();
        s.crsAuthid = a.get( "crs_authid", "" ).asString();
        s.bandRoles = readStringArray( a["band_roles"] );
        EvidenceBucket bucket = EvidenceBucket::Unknown;
        evidenceBucketFromString( a.get( "evidence", "unknown" ).asString(), bucket );
        s.evidence = bucket;
        s.evidencePaths = readStringArray( a["evidence_paths"] );
        s.conflictAlternatives = readStringArray( a["conflict_alternatives"] );
        s.pathHint = a.get( "path_hint", "" ).asString();
        b.assets.push_back( std::move( s ) );
    }

    for ( const auto &c : json["capabilities"] )
    {
        if ( !c.isObject() )
            continue;
        CapabilityEntry e;
        e.capabilityId = c.get( "capability_id", "" ).asString();
        e.intent = c.get( "intent", "" ).asString();
        e.status = c.get( "status", "" ).asString();
        e.score = c.get( "score", 0.0 ).asDouble();
        e.reasons = readStringArray( c["reasons"] );
        e.prepActions = readStringArray( c["prep_actions"] );
        e.structuralOnly = c.get( "structural_only", true ).asBool();
        b.capabilities.push_back( std::move( e ) );
    }

    for ( const auto &r : json["recipes"] )
    {
        if ( !r.isObject() )
            continue;
        RecipeEntry e;
        e.recipeId = r.get( "recipe_id", "" ).asString();
        e.title = r.get( "title", "" ).asString();
        e.intent = r.get( "intent", "" ).asString();
        e.modality = r.get( "modality", "" ).asString();
        e.score = r.get( "score", 0.0 ).asDouble();
        e.stageCount = r.get( "stage_count", 0 ).asInt();
        e.hasHumanOnly = r.get( "has_human_only", false ).asBool();
        e.hasVerifierHooks = r.get( "has_verifier_hooks", false ).asBool();
        e.matched = readStringArray( r["matched"] );
        b.recipes.push_back( std::move( e ) );
    }

    const Json::Value &constraints = json["constraints"];
    if ( constraints.isObject() )
    {
        b.constraints.autonomyLevel = constraints.get( "autonomy_level", "L2" ).asString();
        b.constraints.allowAutonomousExec = constraints.get( "allow_autonomous_exec", false ).asBool();
        b.constraints.offline = constraints.get( "offline", false ).asBool();
        b.constraints.maxBytes = constraints.get( "max_bytes", 65536 ).asInt();
        b.constraints.maxRecipes = constraints.get( "max_recipes", 5 ).asInt();
        b.constraints.maxCapabilities = constraints.get( "max_capabilities", 8 ).asInt();
        b.constraints.determinismRequired = constraints.get( "determinism_required", true ).asBool();
    }

    b.openQuestions = readStringArray( json["open_questions"] );

    const Json::Value &planner = json["planner_projection"];
    if ( planner.isObject() )
    {
        b.planner.goal = planner.get( "goal", "" ).asString();
        b.planner.intent = planner.get( "intent", "" ).asString();
        b.planner.recipeId = planner.get( "recipe_id", "" ).asString();
        b.planner.inputFacts = planner.get( "input_facts", Json::objectValue );
        b.planner.missingFacts = planner.get( "missing_facts", Json::arrayValue );
        b.planner.limitations = planner.get( "limitations", Json::arrayValue );
        b.planner.openQuestions = planner.get( "open_questions", Json::arrayValue );
        b.planner.executionBlocked = planner.get( "execution_blocked", false ).asBool();
        b.planner.blockReason = planner.get( "block_reason", "" ).asString();
    }

    const Json::Value &trunc = json["truncation"];
    if ( trunc.isObject() )
    {
        b.truncation.truncated = trunc.get( "truncated", false ).asBool();
        b.truncation.sections = readStringArray( trunc["sections"] );
        b.truncation.droppedRecipes = trunc.get( "dropped_recipes", 0 ).asInt();
        b.truncation.droppedCapabilities = trunc.get( "dropped_capabilities", 0 ).asInt();
        b.truncation.droppedQuestions = trunc.get( "dropped_questions", 0 ).asInt();
        b.truncation.originalBytes = trunc.get( "original_bytes", 0 ).asInt();
        b.truncation.finalBytes = trunc.get( "final_bytes", 0 ).asInt();
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
