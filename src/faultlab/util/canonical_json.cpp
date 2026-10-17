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

bool appendDouble( std::string &out, double value, std::string &error )
{
    if ( std::isnan( value ) )
    {
        // The documented no-data spelling; the sign bit of a NaN is payload
        // nobody controls, so it normalizes away (-nan must not differ).
        out += "NaN";
        return true;
    }
    if ( std::isinf( value ) )
    {
        // No feature meaning anywhere in the lab (noData is NaN, every
        // transform operand is finite-gated) and not spellable as JSON: an
        // unhashable value must not reach a digest under any circumstances
        // (the same refusal the verification canonicalizer makes).
        error = "canonical json refuses non-finite numbers (infinity)";
        return false;
    }
    if ( value == 0.0 )
    {
        // -0.0 normalizes onto +0.0: verification's roundSignificant and
        // grader's canonicalNumber both print "0" for either zero, and one
        // numeric value must own one canonical spelling.
        out += "0";
        return true;
    }
    char buffer[40];
    std::snprintf( buffer, sizeof( buffer ), "%.17g", value );
    out += buffer;
    return true;
}

/// Walk budgets, sized for the lab's own payloads and nothing else:
///   * depth 256 is double the scenario reader's stackLimit (128), so every
///     document that parses stays digestible, while a programmatic nesting
///     bomb (SIGSEGV at ~100k frames) is refused long before the stack is
///     in danger;
///   * nodes/output comfortably cover the largest legal teaching fixture
///     (the 64 MiB sandbox ceiling ≈ 8.4M samples ≈ 210 MiB of %.17g text)
///     yet cap a wide programmatic bomb's CPU/memory cost.
inline constexpr int kCanonicalMaxDepth = 256;
inline constexpr std::uint64_t kCanonicalMaxNodes = 10000000;
inline constexpr std::uint64_t kCanonicalMaxOutputBytes = 256ull * 1024 * 1024;

struct CanonicalWalker
{
    std::string out;
    std::string error;
    std::uint64_t nodes = 0;

    bool fail( const char *reason )
    {
        error = reason;
        return false;
    }

    bool counted()
    {
        ++nodes;
        if ( nodes > kCanonicalMaxNodes )
        {
            return fail( "canonical json exceeded the node budget" );
        }
        if ( out.size() > kCanonicalMaxOutputBytes )
        {
            return fail( "canonical json exceeded the output byte budget" );
        }
        return true;
    }

    bool appendValue( const Json::Value &value, int depth )
    {
        if ( depth > kCanonicalMaxDepth )
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
                out += std::to_string( value.asInt64() );
                return true;
            case Json::uintValue:
                out += std::to_string( value.asUInt64() );
                return true;
            case Json::realValue:
                return appendDouble( out, value.asDouble(), error );
            case Json::stringValue:
                appendEscaped( out, value.asString() );
                return true;
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
                    if ( !appendValue( element, depth + 1 ) )
                    {
                        return false;
                    }
                }
                out.push_back( ']' );
                return true;
            }
            case Json::objectValue:
            {
                std::vector<std::string> keys = value.getMemberNames();
                std::sort( keys.begin(), keys.end() );
                out.push_back( '{' );
                bool first = true;
                for ( const std::string &key : keys )
                {
                    if ( !first )
                    {
                        out.push_back( ',' );
                    }
                    first = false;
                    appendEscaped( out, key );
                    out.push_back( ':' );
                    if ( !appendValue( value[key], depth + 1 ) )
                    {
                        return false;
                    }
                }
                out.push_back( '}' );
                return true;
            }
        }
        return fail( "canonical json encountered an unsupported value type" );
    }
};

} // namespace

bool tryToCanonicalString( const Json::Value &value, std::string &out, std::string &error )
{
    // One pin per canonical walk keeps every %.17g on the classic decimal
    // point; the pin restores the caller's locale before returning.
    const ClassicNumericLocale pin;
    CanonicalWalker walker;
    if ( !walker.appendValue( value, 1 ) )
    {
        // @p out stays untouched on failure: a partial canonical form must
        // never be mistaken for a successful one.
        error = walker.error;
        return false;
    }
    out = std::move( walker.out );
    return true;
}

bool trySha256HexOf( const Json::Value &value, std::string &out, std::string &error )
{
    std::string canonicalText;
    if ( !tryToCanonicalString( value, canonicalText, error ) )
    {
        return false;
    }
    out = sha256Hex( canonicalText );
    return true;
}

std::string toCanonicalString( const Json::Value &value )
{
    std::string out;
    std::string error;
    if ( !tryToCanonicalString( value, out, error ) )
    {
        // The refusal spelling of the value-returning form: no legal value
        // serializes to zero bytes, so empty can never be a real digest.
        return std::string{};
    }
    return out;
}

std::string sha256HexOf( const Json::Value &value )
{
    std::string out;
    std::string error;
    if ( !trySha256HexOf( value, out, error ) )
    {
        return std::string{};
    }
    return out;
}

} // namespace sicnu::faultlab::canonical
