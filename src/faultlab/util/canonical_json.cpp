// util/canonical_json.cpp — deterministic canonical JSON serialization.
#include "canonical_json.h"

#include "sha256.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace sicnu::faultlab::canonical
{

namespace
{

void appendEscaped( std::string &out, const std::string &text )
{
    out.push_back( '"' );
    for ( const char raw : text )
    {
        const auto c = static_cast<unsigned char>( raw );
        switch ( c )
        {
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
                if ( c < 0x20 )
                {
                    char buffer[8];
                    std::snprintf( buffer, sizeof( buffer ), "\\u%04x", c );
                    out += buffer;
                }
                else
                {
                    out.push_back( raw );
                }
                break;
        }
    }
    out.push_back( '"' );
}

void appendDouble( std::string &out, double value )
{
    if ( std::isnan( value ) )
    {
        out += "NaN";
        return;
    }
    if ( std::isinf( value ) )
    {
        out += ( value < 0.0 ) ? "-Infinity" : "Infinity";
        return;
    }
    char buffer[40];
    std::snprintf( buffer, sizeof( buffer ), "%.17g", value );
    out += buffer;
}

void appendValue( std::string &out, const Json::Value &value )
{
    switch ( value.type() )
    {
        case Json::nullValue:
            out += "null";
            break;
        case Json::booleanValue:
            out += value.asBool() ? "true" : "false";
            break;
        case Json::intValue:
            out += std::to_string( value.asInt64() );
            break;
        case Json::uintValue:
            out += std::to_string( value.asUInt64() );
            break;
        case Json::realValue:
            appendDouble( out, value.asDouble() );
            break;
        case Json::stringValue:
            appendEscaped( out, value.asString() );
            break;
        case Json::arrayValue:
        {
            out.push_back( '[' );
            bool first = true;
            for ( const auto &element : value )
            {
                if ( !first )
                {
                    out.push_back( ',' );
                }
                first = false;
                appendValue( out, element );
            }
            out.push_back( ']' );
            break;
        }
        case Json::objectValue:
        {
            std::vector<std::string> keys = value.getMemberNames();
            std::sort( keys.begin(), keys.end() );
            out.push_back( '{' );
            bool first = true;
            for ( const auto &key : keys )
            {
                if ( !first )
                {
                    out.push_back( ',' );
                }
                first = false;
                appendEscaped( out, key );
                out.push_back( ':' );
                appendValue( out, value[key] );
            }
            out.push_back( '}' );
            break;
        }
    }
}

} // namespace

std::string toCanonicalString( const Json::Value &value )
{
    std::string out;
    appendValue( out, value );
    return out;
}

std::string sha256HexOf( const Json::Value &value )
{
    return sha256Hex( toCanonicalString( value ) );
}

} // namespace sicnu::faultlab::canonical
