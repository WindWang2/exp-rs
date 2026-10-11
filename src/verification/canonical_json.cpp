// canonical_json.cpp — see canonical_json.h for why each bound exists.

#include "verification/canonical_json.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace sicnu::verification
{

namespace
{

struct CanonicalWriter
{
    const CanonicalLimits &limits;
    std::string &out;
    std::string &error;
    std::size_t nodes = 0;

    bool fail( const std::string &reason )
    {
        error = reason;
        return false;
    }

    bool tooDeep( int depth ) const { return depth > limits.maxDepth; }

    bool counted()
    {
        ++nodes;
        if ( nodes > limits.maxElements )
        {
            return fail( "canonical json exceeded the node budget" );
        }
        if ( out.size() > limits.maxOutputBytes )
        {
            return fail( "canonical json exceeded the output byte budget" );
        }
        return true;
    }

    static void appendEscaped( std::string &target, const std::string &text )
    {
        for ( unsigned char c : text )
        {
            switch ( c )
            {
                case '"':
                    target += "\\\"";
                    break;
                case '\\':
                    target += "\\\\";
                    break;
                case '\b':
                    target += "\\b";
                    break;
                case '\f':
                    target += "\\f";
                    break;
                case '\n':
                    target += "\\n";
                    break;
                case '\r':
                    target += "\\r";
                    break;
                case '\t':
                    target += "\\t";
                    break;
                default:
                    if ( c < 0x20 )
                    {
                        static const char kHex[] = "0123456789abcdef";
                        target += "\\u00";
                        target += kHex[ ( c >> 4 ) & 0xF ];
                        target += kHex[ c & 0xF ];
                    }
                    else
                    {
                        target += static_cast<char>( c );
                    }
                    break;
            }
        }
    }

    bool writeString( const std::string &text )
    {
        if ( text.size() > limits.maxStringChars )
        {
            return fail( "canonical json string exceeds the per-string budget" );
        }
        out += '"';
        appendEscaped( out, text );
        out += '"';
        return true;
    }

    bool writeNumber( const Json::Value &value )
    {
        if ( value.isIntegral() )
        {
            const Json::Int64 asInt = value.asInt64();
            out += std::to_string( asInt );
            return true;
        }

        const double asDouble = value.asDouble();
        if ( !std::isfinite( asDouble ) )
        {
            // Refuse rather than emit "nan"/"inf": an unhashable value must not
            // reach a digest under any circumstances.
            return fail( "canonical json refuses non-finite numbers" );
        }
        out += roundSignificant( asDouble );
        return true;
    }

    bool write( const Json::Value &value, int depth )
    {
        if ( tooDeep( depth ) )
        {
            return fail( "canonical json exceeded the depth budget" );
        }
        if ( !counted() )
        {
            return false;
        }

        switch ( value.type() )
        {
            case Json::nullValue:
                out += "null";
                return true;
            case Json::booleanValue:
                out += value.asBool() ? "true" : "false";
                return true;
            case Json::intValue:
            case Json::uintValue:
            case Json::realValue:
                return writeNumber( value );
            case Json::stringValue:
                return writeString( value.asString() );
            case Json::arrayValue:
            {
                const Json::ArrayIndex size = value.size();
                if ( size > limits.maxMembers )
                {
                    return fail( "canonical json array exceeds the member budget" );
                }
                out += '[';
                for ( Json::ArrayIndex i = 0; i < size; ++i )
                {
                    if ( i != 0 )
                    {
                        out += ',';
                    }
                    if ( !write( value[i], depth + 1 ) )
                    {
                        return false;
                    }
                }
                out += ']';
                return true;
            }
            case Json::objectValue:
            {
                std::vector<std::string> keys = value.getMemberNames();
                if ( keys.size() > limits.maxMembers )
                {
                    return fail( "canonical json object exceeds the member budget" );
                }
                // jsoncpp already keeps members sorted; sorting anyway makes the
                // guarantee independent of whatever produced the document.
                std::sort( keys.begin(), keys.end() );
                out += '{';
                bool first = true;
                for ( const std::string &key : keys )
                {
                    if ( !first )
                    {
                        out += ',';
                    }
                    first = false;
                    if ( !writeString( key ) )
                    {
                        return false;
                    }
                    out += ':';
                    if ( !write( value[key], depth + 1 ) )
                    {
                        return false;
                    }
                }
                out += '}';
                return true;
            }
        }

        return fail( "canonical json encountered an unsupported value type" );
    }
};

} // namespace

std::string roundSignificant( double value, int digits )
{
    std::ostringstream stream;
    stream.imbue( std::locale::classic() );
    stream << std::setprecision( digits ) << value;
    return stream.str();
}

bool canonicalJson( const Json::Value &value, std::string &out, std::string &error,
                    const CanonicalLimits &limits )
{
    error.clear();
    std::string staged;
    CanonicalWriter writer{ limits, staged, error };
    if ( !writer.write( value, 1 ) )
    {
        // Leave @p out untouched on failure: a partial canonical form must
        // never be mistaken for a successful one.
        return false;
    }
    out = std::move( staged );
    return true;
}

} // namespace sicnu::verification
