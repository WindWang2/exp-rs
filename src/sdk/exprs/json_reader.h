/***************************************************************************
 * exprs/json_reader.h — type-guarded field access for untrusted JSON
 *
 * Every read of externally-authored JSON (plugin manifests, package
 * metadata, workflow documents, IPC payloads) goes through these helpers.
 * JsonCpp's as*() family THROWS Json::LogicError on a wrong-typed value, and
 * its get()/operator[] assert (debug) or are undefined (release) on a
 * non-object receiver: an unguarded read therefore turns a malformed
 * document into an escaping exception (or an abort) instead of a typed
 * refusal. These helpers never throw; a wrong type is reported through the
 * caller's existing error string and the caller decides the diagnostic.
 *
 * Semantics (shared by every reader):
 *   - absent, null and undefined values SUCCEED and leave @p out untouched
 *     (defaults stay in place);
 *   - a present value of the wrong type FAILS: @p error is set (first error
 *     wins) and the caller should refuse the document;
 *   - a receiver that is not a JSON object FAILS (same contract as a
 *     wrong-typed field), never asserts.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <cstddef>
#include <string>

namespace exprs::jsonread
{

/// Safe member access: returns a null value when @p object is not an object
/// (jsoncpp's get()/operator[] would assert or read out of bounds).
inline const Json::Value &member( const Json::Value &object, const char *key )
{
    static const Json::Value nullValue;
    return object.isObject() ? object[ key ] : nullValue;
}

namespace detail
{
inline bool receiverIsObject( const Json::Value &object, const char *key, std::string &error )
{
    if ( object.isObject() )
        return true;
    if ( error.empty() )
        error = std::string( "field '" ) + key + "' read from a non-object value";
    return false;
}
} // namespace detail

/// Reads a string field. An explicit JSON null counts as absent.
inline bool readString( const Json::Value &object, const char *key, std::string &out,
                        std::string &error )
{
    if ( !detail::receiverIsObject( object, key, error ) )
        return false;
    const Json::Value &value = member( object, key );
    if ( value.isNull() )
        return true;
    if ( !value.isString() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be a string";
        return false;
    }
    out = value.asString();
    return true;
}

/// Reads a string field and enforces @p maxLength (bytes).
inline bool readStringBounded( const Json::Value &object, const char *key, std::string &out,
                               size_t maxLength, std::string &error )
{
    if ( !detail::receiverIsObject( object, key, error ) )
        return false;
    const Json::Value &value = member( object, key );
    if ( value.isNull() )
        return true;
    if ( !value.isString() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be a string";
        return false;
    }
    if ( value.asString().size() > maxLength )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' exceeds the "
                    + std::to_string( maxLength ) + " byte limit";
        return false;
    }
    out = value.asString();
    return true;
}

/// Reads a boolean field (JSON null counts as absent).
inline bool readBool( const Json::Value &object, const char *key, bool &out, std::string &error )
{
    if ( !detail::receiverIsObject( object, key, error ) )
        return false;
    const Json::Value &value = member( object, key );
    if ( value.isNull() )
        return true;
    if ( !value.isBool() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be a boolean";
        return false;
    }
    out = value.asBool();
    return true;
}

/// Reads an int field; integral values outside the int range are refused
/// (jsoncpp's isInt() is the exact predicate asInt() documents).
inline bool readInt( const Json::Value &object, const char *key, int &out, std::string &error )
{
    if ( !detail::receiverIsObject( object, key, error ) )
        return false;
    const Json::Value &value = member( object, key );
    if ( value.isNull() )
        return true;
    if ( !value.isInt() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be an integer";
        return false;
    }
    out = value.asInt();
    return true;
}

/// Reads a 64-bit integer field (index/size fields).
inline bool readInt64( const Json::Value &object, const char *key, long long &out,
                       std::string &error )
{
    if ( !detail::receiverIsObject( object, key, error ) )
        return false;
    const Json::Value &value = member( object, key );
    if ( value.isNull() )
        return true;
    if ( !value.isInt64() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be a 64-bit integer";
        return false;
    }
    out = static_cast<long long>( value.asInt64() );
    return true;
}

/// Reads a numeric (integer or floating) field into @p out as a double.
/// Callers that need an int range check should use readInt.
inline bool readDouble( const Json::Value &object, const char *key, double &out,
                        std::string &error )
{
    if ( !detail::receiverIsObject( object, key, error ) )
        return false;
    const Json::Value &value = member( object, key );
    if ( value.isNull() )
        return true;
    if ( !value.isNumeric() )
    {
        if ( error.empty() )
            error = std::string( "field '" ) + key + "' must be a number";
        return false;
    }
    out = value.asDouble();
    return true;
}

/// True when @p value is an object or null/undefined (a JSON subtree field
/// that may legitimately be absent). A present non-object is refused by the
/// caller with its own diagnostic.
inline bool isObjectOrNull( const Json::Value &value )
{
    return value.isNull() || value.isObject();
}

} // namespace exprs::jsonread
