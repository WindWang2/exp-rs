// util/canonical_json.cpp — deterministic canonical JSON serialization.
#include "canonical_json.h"

#include "sha256.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <vector>

#if defined( _WIN32 )
#include <locale.h>
#else
#include <clocale>
#if defined( __APPLE__ )
#include <xlocale.h>
#endif
#endif

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

/// Per-thread numeric-locale pin held for the duration of one canonical
/// walk. Mirrors the verifier's guard (src/verify/verify_locale.cpp):
/// faultlab is a jsoncpp-only leaf and must not grow a sicnu_verifier link
/// for the guard, so the pattern is kept here. Without the pin,
/// appendDouble's snprintf("%.17g") follows the calling thread's
/// LC_NUMERIC — a comma-decimal locale would emit "0,5" and digests would
/// diverge across hosts (#1447).
class ClassicNumericLocale
{
  public:
    ClassicNumericLocale();
    ~ClassicNumericLocale();

    ClassicNumericLocale( const ClassicNumericLocale & ) = delete;
    ClassicNumericLocale &operator=( const ClassicNumericLocale & ) = delete;

  private:
#if defined( _WIN32 )
    int previousThreadSetting = -1;
    std::string previousSetting;
#else
    void *previousLocale = nullptr; ///< opaque locale_t (LC_GLOBAL_LOCALE allowed)
#endif
};

#if defined( _WIN32 )

ClassicNumericLocale::ClassicNumericLocale()
{
    // Restore both the per-thread-locale regime and the previous LC_NUMERIC:
    // a thread that already opted into per-thread locales keeps its regime.
    previousThreadSetting = _configthreadlocale( _ENABLE_PER_THREAD_LOCALE );
    if ( const char *previous = setlocale( LC_NUMERIC, nullptr ) )
        previousSetting = previous;
    setlocale( LC_NUMERIC, "C" );
}

ClassicNumericLocale::~ClassicNumericLocale()
{
    if ( !previousSetting.empty() )
        setlocale( LC_NUMERIC, previousSetting.c_str() );
    if ( previousThreadSetting != -1 )
        _configthreadlocale( previousThreadSetting );
}

#elif defined( LC_NUMERIC_MASK )

ClassicNumericLocale::ClassicNumericLocale()
{
    // uselocale() switches per-thread without touching the process locale;
    // the classic locale is created once per process (thread-safe static
    // init) and lives as long as the digest contract does.
    static locale_t classic =
        newlocale( LC_NUMERIC_MASK, "C", static_cast<locale_t>( nullptr ) );
    if ( classic )
        previousLocale = uselocale( classic );
}

ClassicNumericLocale::~ClassicNumericLocale()
{
    if ( previousLocale )
        uselocale( static_cast<locale_t>( previousLocale ) );
}

#else

// No per-thread locale API in reach of this build: the guard degrades to a
// no-op, preserving today's behavior — canonical text stays correct while
// the process remains in the C locale, which is its default.

ClassicNumericLocale::ClassicNumericLocale() = default;
ClassicNumericLocale::~ClassicNumericLocale() = default;

#endif

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
    // One pin per canonical walk keeps every %.17g on the classic decimal
    // point; the pin restores the caller's locale before returning.
    const ClassicNumericLocale pin;
    std::string out;
    appendValue( out, value );
    return out;
}

std::string sha256HexOf( const Json::Value &value )
{
    return sha256Hex( toCanonicalString( value ) );
}

} // namespace sicnu::faultlab::canonical
