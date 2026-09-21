/***************************************************************************
  scientific_state/asset_state_diff.cpp
  RS14-01 Scientific Data Passport — state diff API (sicnu.asset_state_diff.v1).
 ***************************************************************************/

#include "scientific_state/asset_state_diff.h"

#include "scientific_state/asset_state_json.h"

#include <map>
#include <set>

namespace sicnu::state
{

namespace
{

/// Canonical scalar/array text for path comparison (JSON text consistency).
std::string valueToText( const Json::Value &value )
{
    if ( value.isString() )
        return value.asString();
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    builder[ "commentStyle" ] = "None";
    return Json::writeString( builder, value );
}

/// Flattens a canonical state document into path → value text. Claim records
/// become "claims[<claim path>]" entries; their kind texts are captured
/// separately for readable claim_changed before/after fields. jsoncpp object
/// member names come back lexicographically sorted, so the walk order — and
/// therefore the map — is deterministic.
void flattenDocument( const Json::Value &node, const std::string &prefix,
                      std::map<std::string, std::string> &out,
                      std::map<std::string, std::string> &claimKinds )
{
    if ( node.isObject() )
    {
        for ( const std::string &key : node.getMemberNames() )
        {
            const std::string childPath = prefix.empty() ? key : prefix + "." + key;
            flattenDocument( node[ key ], childPath, out, claimKinds );
        }
        return;
    }
    if ( prefix == "claims" && node.isArray() )
    {
        for ( const Json::Value &claim : node )
        {
            const std::string key = "claims[" + claim[ "path" ].asString() + "]";
            out[ key ] = valueToText( claim );
            claimKinds[ key ] = claim[ "kind" ].asString();
        }
        return;
    }
    out[ prefix ] = valueToText( node );
}

bool isKnownDiffKind( const std::string &kind )
{
    return kind == "changed" || kind == "added" || kind == "removed" ||
           kind == "claim_changed";
}

} // namespace

StateDiff diffStates( const RemoteSensingAssetState &before,
                      const RemoteSensingAssetState &after )
{
    std::map<std::string, std::string> beforeMap;
    std::map<std::string, std::string> beforeKinds;
    std::map<std::string, std::string> afterMap;
    std::map<std::string, std::string> afterKinds;
    flattenDocument( assetStateToJson( before ), "", beforeMap, beforeKinds );
    flattenDocument( assetStateToJson( after ), "", afterMap, afterKinds );

    StateDiff diff;
    auto beforeIt = beforeMap.begin();
    auto afterIt = afterMap.begin();
    while ( beforeIt != beforeMap.end() || afterIt != afterMap.end() )
    {
        if ( afterIt == afterMap.end() ||
             ( beforeIt != beforeMap.end() && beforeIt->first < afterIt->first ) )
        {
            FieldDiff field;
            field.path = beforeIt->first;
            field.kind = "removed";
            field.before = beforeIt->second;
            diff.diffs.push_back( field );
            ++beforeIt;
        }
        else if ( beforeIt == beforeMap.end() || afterIt->first < beforeIt->first )
        {
            FieldDiff field;
            field.path = afterIt->first;
            field.kind = "added";
            field.after = afterIt->second;
            diff.diffs.push_back( field );
            ++afterIt;
        }
        else
        {
            if ( beforeIt->second != afterIt->second )
            {
                FieldDiff field;
                field.path = beforeIt->first;
                const bool isClaim = beforeIt->first.rfind( "claims[", 0 ) == 0;
                field.kind = isClaim ? "claim_changed" : "changed";
                if ( isClaim )
                {
                    field.before = beforeKinds[ beforeIt->first ];
                    field.after = afterKinds[ afterIt->first ];
                }
                else
                {
                    field.before = beforeIt->second;
                    field.after = afterIt->second;
                }
                diff.diffs.push_back( field );
            }
            else
            {
                ++diff.unchangedFields;
            }
            ++beforeIt;
            ++afterIt;
        }
    }
    return diff;  // std::map iteration is already path-sorted
}

Json::Value stateDiffToJson( const StateDiff &diff )
{
    Json::Value doc( Json::objectValue );
    doc[ "schema" ] = diff.schemaId.empty() ? kAssetStateDiffSchemaId : diff.schemaId;

    Json::Value diffs( Json::arrayValue );
    for ( const FieldDiff &field : diff.diffs )
    {
        Json::Value node( Json::objectValue );
        node[ "path" ] = field.path;
        node[ "kind" ] = field.kind;
        if ( !field.before.empty() )
            node[ "before" ] = field.before;
        if ( !field.after.empty() )
            node[ "after" ] = field.after;
        diffs.append( node );
    }
    doc[ "diffs" ] = diffs;
    doc[ "unchanged_fields" ] = static_cast<Json::UInt64>( diff.unchangedFields );
    return doc;
}

bool stateDiffFromJson( const Json::Value &json, StateDiff &out, AssetStateError &error )
{
    error = AssetStateError{};
    out = StateDiff{};

    if ( !json.isObject() )
    {
        error.code = StateErrorCode::MalformedJson;
        error.message = "document is not a JSON object";
        return false;
    }

    const Json::Value &schema = json[ "schema" ];
    if ( !schema.isString() || schema.asString() != kAssetStateDiffSchemaId )
    {
        error.code = StateErrorCode::SchemaMismatch;
        error.message = std::string( "schema must be " ) + kAssetStateDiffSchemaId;
        return false;
    }

    StateDiff diff;
    diff.schemaId = kAssetStateDiffSchemaId;

    if ( json.isMember( "unchanged_fields" ) )
    {
        if ( !json[ "unchanged_fields" ].isIntegral() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'unchanged_fields' must be an integer";
            return false;
        }
        diff.unchangedFields =
            static_cast<std::size_t>( json[ "unchanged_fields" ].asUInt64() );
    }

    const Json::Value &diffs = json[ "diffs" ];
    if ( !diffs.isNull() )
    {
        if ( !diffs.isArray() )
        {
            error.code = StateErrorCode::InvalidField;
            error.message = "field 'diffs' must be an array";
            return false;
        }
        for ( const Json::Value &node : diffs )
        {
            if ( !node.isObject() )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'diffs' must contain objects";
                return false;
            }
            FieldDiff field;
            if ( !node.isMember( "path" ) || !node[ "path" ].isString() )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'diffs[].path' must be a string";
                return false;
            }
            field.path = node[ "path" ].asString();
            if ( !node.isMember( "kind" ) || !node[ "kind" ].isString() ||
                 !isKnownDiffKind( node[ "kind" ].asString() ) )
            {
                error.code = StateErrorCode::InvalidField;
                error.message = "field 'diffs[].kind' is not a known diff kind";
                return false;
            }
            field.kind = node[ "kind" ].asString();
            if ( node.isMember( "before" ) )
            {
                if ( !node[ "before" ].isString() )
                {
                    error.code = StateErrorCode::InvalidField;
                    error.message = "field 'diffs[].before' must be a string";
                    return false;
                }
                field.before = node[ "before" ].asString();
            }
            if ( node.isMember( "after" ) )
            {
                if ( !node[ "after" ].isString() )
                {
                    error.code = StateErrorCode::InvalidField;
                    error.message = "field 'diffs[].after' must be a string";
                    return false;
                }
                field.after = node[ "after" ].asString();
            }
            diff.diffs.push_back( field );
        }
    }

    out = std::move( diff );
    return true;
}

std::string serializeStateDiff( const StateDiff &diff )
{
    const Json::Value doc = stateDiffToJson( diff );
    Json::StreamWriterBuilder builder;
    builder[ "indentation" ] = "";
    builder[ "commentStyle" ] = "None";
    return Json::writeString( builder, doc );
}

} // namespace sicnu::state
