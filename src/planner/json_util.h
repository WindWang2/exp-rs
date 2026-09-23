// src/planner/json_util.h
#pragma once

//
// Internal (header-only) JSON helpers shared by the planner schema modules.
// Not part of the public planner API.
//

#include <json/json.h>
#include <string>

namespace sicnu::planner::json_util {

/// Canonical compact serialization: jsoncpp object members iterate in sorted
/// key order (std::map), so an indentation-free write is the repo's canonical
/// form (key-sorted, no whitespace) — the same convention fingerprints key on.
inline std::string canonicalCompact( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["commentStyle"] = "None";
    builder["precision"] = 17;
    return Json::writeString( builder, value );
}

/// Reader discipline (plan.md §7): typed error prefix + human detail.
/// Codes are closed: invalid_document | unsupported_version | invalid_field
/// | out_of_bounds.
inline std::string error( const std::string &code, const std::string &detail )
{
    return code + ": " + detail;
}

/// True when `text` is within [1, max] characters (id/text bounds).
inline bool boundedNonEmpty( const std::string &text, size_t max )
{
    return !text.empty() && text.size() <= max;
}

/// Reads a required non-empty bounded string member.
inline bool readBoundedString( const Json::Value &object, const std::string &key, size_t max,
                               std::string &out, std::string &err )
{
    if ( !object.isMember( key ) || !object[key].isString() )
    {
        err = error( "invalid_field", "missing or non-string '" + key + "'" );
        return false;
    }
    out = object[key].asString();
    if ( !boundedNonEmpty( out, max ) )
    {
        err = error( "out_of_bounds", "'" + key + "' empty or over " + std::to_string( max )
                                         + " chars" );
        return false;
    }
    return true;
}

/// Reads an optional bounded string member (absent or null → empty).
inline bool readOptionalBoundedString( const Json::Value &object, const std::string &key,
                                       size_t max, std::string &out, std::string &err )
{
    out.clear();
    if ( !object.isMember( key ) || object[key].isNull() )
        return true;
    if ( !object[key].isString() )
    {
        err = error( "invalid_field", "non-string '" + key + "'" );
        return false;
    }
    out = object[key].asString();
    if ( out.size() > max )
    {
        err = error( "out_of_bounds", "'" + key + "' over " + std::to_string( max ) + " chars" );
        return false;
    }
    return true;
}

/// Envelope check every fail-closed reader starts with.
inline bool checkEnvelope( const Json::Value &doc, const std::string &expectedKind,
                           std::string &err )
{
    if ( !doc.isObject() )
    {
        err = error( "invalid_document", "document is not a JSON object" );
        return false;
    }
    if ( !doc.isMember( "kind" ) || doc["kind"].asString() != expectedKind )
    {
        err = error( "invalid_document", "kind must be \"" + expectedKind + "\"" );
        return false;
    }
    if ( !doc.isMember( "schema_version" ) || !doc["schema_version"].isString()
         || doc["schema_version"].asString() != "1.0" )
    {
        err = error( "unsupported_version", "only schema_version \"1.0\" is accepted" );
        return false;
    }
    return true;
}

} // namespace sicnu::planner::json_util
