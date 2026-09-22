// grader_json.cpp — deterministic JSON helpers (canonical leaf form).
#include "grader_json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace sicnu::grader {

namespace {

void escapeStringInto( std::string &out, const std::string &text )
{
    out.push_back( '"' );
    for ( const char ch : text ) {
        switch ( ch ) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if ( static_cast<unsigned char>( ch ) < 0x20 ) {
                char buf[8];
                std::snprintf( buf, sizeof( buf ), "\\u%04x", static_cast<unsigned>( static_cast<unsigned char>( ch ) ) );
                out += buf;
            } else {
                out.push_back( ch );
            }
        }
    }
    out.push_back( '"' );
}

bool appendCanonical( const Json::Value &value, std::string &out, GraderError &error, const std::string &path );

bool appendNumber( const Json::Value &value, std::string &out, GraderError &error, const std::string &path )
{
    const double number = value.asDouble();
    if ( !std::isfinite( number ) ) {
        error = makeError( GraderErrorCode::Internal, "non-finite number cannot be canonicalized", path );
        return false;
    }
    auto spelling = canonicalNumber( number, error );
    if ( !spelling ) {
        if ( error.path.empty() )
            error.path = path;
        return false;
    }
    out += *spelling;
    return true;
}

bool appendCanonical( const Json::Value &value, std::string &out, GraderError &error, const std::string &path )
{
    switch ( value.type() ) {
    case Json::nullValue:
        out += "null";
        return true;
    case Json::booleanValue:
        out += value.asBool() ? "true" : "false";
        return true;
    case Json::intValue:
    case Json::uintValue:
    case Json::realValue:
        return appendNumber( value, out, error, path );
    case Json::stringValue:
        escapeStringInto( out, value.asString() );
        return true;
    case Json::arrayValue: {
        out.push_back( '[' );
        bool first = true;
        for ( const auto &element : value ) {
            if ( !first )
                out.push_back( ',' );
            first = false;
            if ( !appendCanonical( element, out, error, path ) )
                return false;
        }
        out.push_back( ']' );
        return true;
    }
    case Json::objectValue: {
        // Deterministic member order: sorted by key (code-unit order).
        std::map<std::string, const Json::Value *> sorted;
        for ( const auto &key : value.getMemberNames() )
            sorted.emplace( key, &value[key] );
        out.push_back( '{' );
        bool first = true;
        for ( const auto &entry : sorted ) {
            if ( !first )
                out.push_back( ',' );
            first = false;
            escapeStringInto( out, entry.first );
            out.push_back( ':' );
            if ( !appendCanonical( *entry.second, out, error, path.empty() ? entry.first : path + "." + entry.first ) )
                return false;
        }
        out.push_back( '}' );
        return true;
    }
    }
    error = makeError( GraderErrorCode::Internal, "unknown JSON value type", path );
    return false;
}

} // namespace

std::optional<std::string> canonicalNumber( double value, GraderError &error )
{
    if ( !std::isfinite( value ) ) {
        error = makeError( GraderErrorCode::Internal, "non-finite number cannot be canonicalized" );
        return std::nullopt;
    }
    if ( value == 0.0 )
        return std::string( "0" ); // also normalizes -0.0

    // std::to_chars is locale-INDEPENDENT by specification: snprintf/strtod
    // would follow LC_NUMERIC and emit ",'-separated canonical text (and
    // therefore divergent digests) under a non-C numeric locale.
    char buf[64];
    if ( value == std::floor( value ) && std::fabs( value ) < 9.007199254740992e15 ) {
        // Integral within int64-safe range: print without a decimal point.
        const auto result = std::to_chars( buf, buf + sizeof( buf ), value, std::chars_format::fixed, 0 );
        if ( result.ec == std::errc() )
            return std::string( buf, result.ptr );
    } else {
        // Shortest form that round-trips back to the same double.
        const auto result = std::to_chars( buf, buf + sizeof( buf ), value );
        if ( result.ec == std::errc() )
            return std::string( buf, result.ptr );
    }
    error = makeError( GraderErrorCode::Internal, "number failed round-trip canonicalization" );
    return std::nullopt;
}

std::optional<std::string> canonicalizeJson( const Json::Value &value, GraderError &error )
{
    std::string out;
    if ( !appendCanonical( value, out, error, {} ) )
        return std::nullopt;
    return out;
}

std::optional<Json::Value> parseJsonStrict( const std::string &text, GraderError &error )
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["failIfExtra"] = true;
    builder["strictRoot"] = false;
    builder["allowComments"] = false;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    Json::Value doc;
    std::string parseErrors;
    // Same safe-reader discipline as guidance_store / plugin_manifest: the
    // reader can THROW (e.g. "Exceeded stackLimit" on a nesting bomb), and a
    // grading document is untrusted content — the refusal stays typed.
    try
    {
        if ( !reader->parse( text.data(), text.data() + text.size(), &doc, &parseErrors ) ) {
            error = makeError( GraderErrorCode::InvalidJson, "invalid JSON: " + parseErrors );
            return std::nullopt;
        }
    }
    catch ( const Json::Exception &exception )
    {
        error = makeError( GraderErrorCode::InvalidJson, std::string( "invalid JSON: " ) + exception.what() );
        return std::nullopt;
    }
    return doc;
}

} // namespace sicnu::grader
