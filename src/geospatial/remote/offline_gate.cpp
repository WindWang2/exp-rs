/***************************************************************************
 * geospatial/remote/offline_gate.cpp — process-wide offline gate (goal D7)
 ***************************************************************************/
#include "offline_gate.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <cpl_conv.h>

namespace sicnu::geo::offline {

namespace {

std::atomic<bool> g_offline{ false };

constexpr const char *const kDenyExtension = ".__offline_blocked__";

bool envFlagEnabled( const char *name )
{
    const char *v = std::getenv( name );
    if ( !v )
        return false;
    std::string s = v;
    // trim
    const auto isSpace = []( unsigned char c ) { return std::isspace( c ) != 0; };
    s.erase( s.begin(), std::find_if_not( s.begin(), s.end(), isSpace ) );
    s.erase( std::find_if_not( s.rbegin(), s.rend(), isSpace ).base(), s.end() );
    for ( char &c : s )
        c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

bool parseNumericPart( const std::string &part, std::uint64_t &outValue )
{
    if ( part.empty() )
        return false;
    int base = 10;
    std::size_t start = 0;
    if ( part.size() >= 2 && part[0] == '0' && ( part[1] == 'x' || part[1] == 'X' ) )
    {
        base = 16;
        start = 2;
        if ( start == part.size() )
            return false;
    }
    else if ( part.size() > 1 && part[0] == '0' )
    {
        base = 8;
        start = 1;
    }

    std::uint64_t val = 0;
    for ( std::size_t i = start; i < part.size(); ++i )
    {
        const unsigned char c = static_cast<unsigned char>( part[i] );
        int digit = -1;
        if ( base == 16 )
        {
            if ( c >= '0' && c <= '9' ) digit = c - '0';
            else if ( c >= 'a' && c <= 'f' ) digit = c - 'a' + 10;
            else if ( c >= 'A' && c <= 'F' ) digit = c - 'A' + 10;
        }
        else if ( base == 8 )
        {
            if ( c >= '0' && c <= '7' ) digit = c - '0';
        }
        else // base == 10
        {
            if ( c >= '0' && c <= '9' ) digit = c - '0';
        }
        if ( digit < 0 )
            return false;
        val = val * base + static_cast<std::uint64_t>( digit );
        if ( val > 0xFFFFFFFFULL )
            return false;
    }
    outValue = val;
    return true;
}

bool parseIpv4( const std::string &text, std::uint32_t &out )
{
    if ( text.empty() )
        return false;
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while ( true )
    {
        const std::size_t dot = text.find( '.', pos );
        if ( dot == std::string::npos )
        {
            parts.push_back( text.substr( pos ) );
            break;
        }
        parts.push_back( text.substr( pos, dot - pos ) );
        pos = dot + 1;
        if ( parts.size() > 4 )
            return false;
    }
    if ( parts.empty() || parts.size() > 4 )
        return false;

    std::uint64_t values[4] = {};
    for ( std::size_t i = 0; i < parts.size(); ++i )
    {
        if ( !parseNumericPart( parts[i], values[i] ) )
            return false;
    }

    std::uint32_t result = 0;
    switch ( parts.size() )
    {
        case 1:
            if ( values[0] > 0xFFFFFFFFULL )
                return false;
            result = static_cast<std::uint32_t>( values[0] );
            break;
        case 2:
            if ( values[0] > 0xFFULL || values[1] > 0xFFFFFFULL )
                return false;
            result = static_cast<std::uint32_t>( ( values[0] << 24 ) | values[1] );
            break;
        case 3:
            if ( values[0] > 0xFFULL || values[1] > 0xFFULL || values[2] > 0xFFFFULL )
                return false;
            result = static_cast<std::uint32_t>( ( values[0] << 24 ) | ( values[1] << 16 ) | values[2] );
            break;
        case 4:
            if ( values[0] > 0xFFULL || values[1] > 0xFFULL || values[2] > 0xFFULL || values[3] > 0xFFULL )
                return false;
            result = static_cast<std::uint32_t>( ( values[0] << 24 ) | ( values[1] << 16 ) | ( values[2] << 8 ) | values[3] );
            break;
        default:
            return false;
    }
    out = result;
    return true;
}

bool parseIpv6( const std::string &text, unsigned char out[16] )
{
    const std::size_t cc = text.find( "::" );
    if ( cc != std::string::npos && text.find( "::", cc + 2 ) != std::string::npos )
        return false; // at most one compression
    const std::string head = cc == std::string::npos ? text : text.substr( 0, cc );
    const std::string tail = cc == std::string::npos ? std::string() : text.substr( cc + 2 );

    std::uint16_t groups[8] = {};
    int headCount = 0;
    int tailCount = 0;

    auto parseSection = [ & ]( const std::string &section, std::uint16_t *groups_, int &count,
                               int maxCount, bool allowV4Tail ) {
        if ( section.empty() )
            return true;
        std::size_t pos = 0;
        while ( true )
        {
            if ( count >= maxCount )
                return false;
            const std::size_t colon = section.find( ':', pos );
            const std::string token = colon == std::string::npos ? section.substr( pos )
                                                                 : section.substr( pos, colon - pos );
            if ( allowV4Tail && colon == std::string::npos && token.find( '.' ) != std::string::npos )
            {
                std::uint32_t v4 = 0;
                if ( !parseIpv4( token, v4 ) )
                    return false;
                if ( count + 2 > maxCount )
                    return false;
                groups_[count++] = static_cast<std::uint16_t>( v4 >> 16 );
                groups_[count++] = static_cast<std::uint16_t>( v4 & 0xFFFFu );
                return true;
            }
            if ( token.empty() || token.size() > 4 )
                return false;
            std::uint32_t group = 0;
            for ( const char c : token )
            {
                if ( !std::isxdigit( static_cast<unsigned char>( c ) ) )
                    return false;
                const std::uint32_t digit = c <= '9' ? static_cast<std::uint32_t>( c - '0' )
                                                     : static_cast<std::uint32_t>( ( c | 0x20 ) - 'a' + 10 );
                group = group * 16 + digit;
            }
            groups_[count++] = static_cast<std::uint16_t>( group );
            if ( colon == std::string::npos )
                return true;
            pos = colon + 1;
        }
    };

    const bool headAllowV4Tail = ( cc == std::string::npos );
    if ( !parseSection( head, groups, headCount, 8, headAllowV4Tail ) )
        return false;
    if ( !parseSection( tail, groups + headCount, tailCount, 8 - headCount, true ) )
        return false;

    const int total = headCount + tailCount;
    if ( cc == std::string::npos )
    {
        if ( total != 8 )
            return false;
    }
    else if ( total > 7 )
    {
        return false;
    }
    else
    {
        for ( int i = 0; i < tailCount; ++i )
        {
            groups[7 - i] = groups[headCount + tailCount - 1 - i];
            groups[headCount + tailCount - 1 - i] = 0;
        }
    }

    for ( int i = 0; i < 8; ++i )
    {
        out[i * 2] = static_cast<unsigned char>( groups[i] >> 8 );
        out[i * 2 + 1] = static_cast<unsigned char>( groups[i] & 0xFF );
    }
    return true;
}

bool isPrivateIpv4( std::uint32_t v4 )
{
    if ( ( v4 & 0xFF000000u ) == 0x0A000000u ) // 10.0.0.0/8
        return true;
    if ( ( v4 & 0xFFF00000u ) == 0xAC100000u ) // 172.16.0.0/12
        return true;
    if ( ( v4 & 0xFFFF0000u ) == 0xC0A80000u ) // 192.168.0.0/16
        return true;
    if ( ( v4 & 0xFFFF0000u ) == 0xA9FE0000u ) // 169.254.0.0/16 link-local
        return true;
    if ( ( v4 & 0xFF000000u ) == 0x7F000000u ) // 127.0.0.0/8 loopback
        return true;
    if ( ( v4 & 0xFF000000u ) == 0x00000000u ) // 0.0.0.0/8
        return true;
    if ( ( v4 & 0xFFC00000u ) == 0x64400000u ) // 100.64.0.0/10 CGNAT
        return true;
    if ( ( v4 & 0xF0000000u ) == 0xE0000000u ) // 224.0.0.0/4 multicast
        return true;
    if ( ( v4 & 0xF0000000u ) == 0xF0000000u ) // 240.0.0.0/4 reserved & broadcast
        return true;
    return false;
}

std::string percentDecode( const std::string &input )
{
    std::string out;
    out.reserve( input.size() );
    for ( std::size_t i = 0; i < input.size(); ++i )
    {
        if ( input[i] == '%' && i + 2 < input.size() &&
             std::isxdigit( static_cast<unsigned char>( input[i + 1] ) ) &&
             std::isxdigit( static_cast<unsigned char>( input[i + 2] ) ) )
        {
            const auto hexVal = []( char c ) -> unsigned int {
                if ( c >= '0' && c <= '9' ) return static_cast<unsigned int>( c - '0' );
                if ( c >= 'a' && c <= 'f' ) return static_cast<unsigned int>( c - 'a' + 10 );
                if ( c >= 'A' && c <= 'F' ) return static_cast<unsigned int>( c - 'A' + 10 );
                return 0;
            };
            const unsigned int val = ( hexVal( input[i + 1] ) << 4 ) | hexVal( input[i + 2] );
            out.push_back( static_cast<char>( val ) );
            i += 2;
        }
        else
        {
            out.push_back( input[i] );
        }
    }
    return out;
}

} // namespace

void setEnabled( bool offline )
{
    g_offline.store( offline );
}

bool enabled()
{
    return g_offline.load();
}

bool enabledFromEnv()
{
    return envFlagEnabled( "SICNU_OFFLINE" );
}

bool isRemoteTarget( const std::string &source )
{
    if ( source.empty() )
        return false;

    const auto startsWith = [&source]( const char *prefix )
    {
        const std::size_t n = std::strlen( prefix );
        return source.size() >= n && source.compare( 0, n, prefix ) == 0;
    };
    const auto startsWithCi = [&source]( const char *prefix )
    {
        // The network scheme prefixes are ASCII; compare case-insensitively.
        std::string lower( source.substr( 0, std::strlen( prefix ) ) );
        for ( char &c : lower )
            c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
        return lower == prefix;
    };

    if ( startsWithCi( "http://" ) || startsWithCi( "https://" )
         || startsWithCi( "ftp://" ) || startsWithCi( "ftps://" ) )
        return true;

    static const char *const kNetworkVsi[] = {
        "/vsicurl/",          "/vsicurl_streaming/",  "/vsis3/",
        "/vsis3_streaming/",  "/vsigs/",              "/vsigs_streaming/",
        "/vsiaz/",            "/vsiaz_streaming/",    "/vsiadls/",
        "/vsiadls_streaming/","/vsihdfs/",            "/vsiwebhdfs/",
        "/vsioss/",           "/vsioss_streaming/",   "/vsiswift/",
    };
    for ( const char *prefix : kNetworkVsi )
    {
        if ( startsWith( prefix ) )
            return true;
    }

    // Wrapping handler: remote only when its inner target is — the range
    // cache legitimately fronts local files too.
    if ( startsWith( "/vsirangecache/" ) )
        return isRemoteTarget( source.substr( strlen( "/vsirangecache/" ) ) );
    return false;
}

std::string refusalMessage( const std::string &source )
{
    return "offline mode (--offline / SICNU_OFFLINE): refusing remote "
           "request without network access: " + source;
}

bool isPrivateHost( const std::string &hostOrUrl )
{
    if ( hostOrUrl.empty() )
        return true;

    std::string input = hostOrUrl;
    while ( input.rfind( "/vsi", 0 ) == 0 )
    {
        const std::size_t slash = input.find( '/', 1 );
        if ( slash == std::string::npos )
            break;
        input = input.substr( slash + 1 );
    }

    std::string host;
    const std::size_t schemePos = input.find( "://" );
    if ( schemePos != std::string::npos )
    {
        std::string afterScheme = input.substr( schemePos + 3 );
        const std::size_t atPos = afterScheme.find( '@' );
        const std::size_t firstSlash = afterScheme.find( '/' );
        if ( atPos != std::string::npos && ( firstSlash == std::string::npos || atPos < firstSlash ) )
            afterScheme = afterScheme.substr( atPos + 1 );
        const std::size_t term = afterScheme.find_first_of( "/?#" );
        host = ( term == std::string::npos ) ? afterScheme : afterScheme.substr( 0, term );
    }
    else
    {
        if ( !input.empty() && input.front() == '/' )
            return false;
        const std::size_t term = input.find_first_of( "/?#" );
        host = ( term == std::string::npos ) ? input : input.substr( 0, term );
    }

    host = percentDecode( host );

    if ( !host.empty() && host.front() == '[' )
    {
        const std::size_t close = host.find( ']' );
        host = ( close == std::string::npos ) ? host.substr( 1 ) : host.substr( 1, close - 1 );
    }
    else if ( host.find( ':' ) != std::string::npos && host.find( ':' ) == host.rfind( ':' ) )
    {
        host = host.substr( 0, host.find( ':' ) );
    }

    const std::size_t zonePos = host.find( '%' );
    if ( zonePos != std::string::npos )
    {
        host = host.substr( 0, zonePos );
    }

    std::string lowered;
    lowered.reserve( host.size() );
    for ( const char c : host )
        lowered += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

    if ( lowered.empty() )
        return true;
    if ( lowered == "localhost" || ( lowered.size() > 10 && lowered.compare( lowered.size() - 10, 10, ".localhost" ) == 0 ) )
        return true;
    if ( lowered == "metadata.google.internal" )
        return true;

    std::uint32_t v4 = 0;
    if ( parseIpv4( host, v4 ) )
        return isPrivateIpv4( v4 );

    unsigned char v6[16] = {};
    if ( parseIpv6( host, v6 ) )
    {
        bool ipv4Mapped = v6[10] == 0xFF && v6[11] == 0xFF;
        for ( int i = 0; i < 10 && ipv4Mapped; ++i )
            ipv4Mapped = ( v6[i] == 0 );
        if ( ipv4Mapped )
        {
            const std::uint32_t embedded =
                ( static_cast<std::uint32_t>( v6[12] ) << 24 ) | ( static_cast<std::uint32_t>( v6[13] ) << 16 ) |
                ( static_cast<std::uint32_t>( v6[14] ) << 8 ) | static_cast<std::uint32_t>( v6[15] );
            return isPrivateIpv4( embedded );
        }

        // IPv4-compatible ::a.b.c.d (96 zero bits)
        bool ipv4Compatible = true;
        for ( int i = 0; i < 12; ++i )
            ipv4Compatible = ipv4Compatible && ( v6[i] == 0 );
        if ( ipv4Compatible )
        {
            const std::uint32_t embedded =
                ( static_cast<std::uint32_t>( v6[12] ) << 24 ) | ( static_cast<std::uint32_t>( v6[13] ) << 16 ) |
                ( static_cast<std::uint32_t>( v6[14] ) << 8 ) | static_cast<std::uint32_t>( v6[15] );
            if ( embedded == 0 )
                return true; // :: (unspecified / 0.0.0.0 equivalent, routes to loopback on POSIX)
            if ( embedded > 1 )
                return isPrivateIpv4( embedded );
        }

        bool loopback = true;
        for ( int i = 0; i < 15; ++i )
            loopback = loopback && ( v6[i] == 0 );
        loopback = loopback && ( v6[15] == 1 );
        if ( loopback )
            return true;

        // Multicast ff00::/8
        if ( v6[0] == 0xFF )
            return true;

        // NAT64 Well-Known Prefix 64:ff9b::/96 (RFC 6052)
        bool nat64 = ( v6[0] == 0x00 && v6[1] == 0x64 && v6[2] == 0xFF && v6[3] == 0x9B );
        for ( int i = 4; i < 12 && nat64; ++i )
            nat64 = ( v6[i] == 0 );
        if ( nat64 )
        {
            const std::uint32_t embedded =
                ( static_cast<std::uint32_t>( v6[12] ) << 24 ) | ( static_cast<std::uint32_t>( v6[13] ) << 16 ) |
                ( static_cast<std::uint32_t>( v6[14] ) << 8 ) | static_cast<std::uint32_t>( v6[15] );
            if ( embedded == 0 )
                return true;
            return isPrivateIpv4( embedded );
        }

        if ( v6[0] == 0xFE && ( v6[1] & 0xC0 ) == 0x80 )
            return true;
        if ( ( v6[0] & 0xFE ) == 0xFC )
            return true;
        return false;
    }

    return false;
}

void applyGdalNetworkDeny()
{
    // Must run single-threaded at startup: GDAL global config is not synchronized.
    // The g_offline flag itself is atomic and may be observed from worker threads.
    // An extension no real source can carry: GDAL treats every network /vsi*
    // path as non-existing without issuing a single request (measured against
    // GDAL 3.13: open fails in ~10 ms, no DNS, no TCP).
    CPLSetConfigOption( "CPL_VSIL_CURL_ALLOWED_EXTENSIONS", kDenyExtension );
    // Library-internal network attempts that bypass our layers entirely:
    // PROJ coordinate-grid downloads (default-off, but pinned) and GDAL's
    // AWS credential chain (which can hit the EC2 IMDS on /vsis3/ opens).
    CPLSetConfigOption( "PROJ_NETWORK", "OFF" );
    CPLSetConfigOption( "AWS_NO_SIGN_REQUEST", "YES" );
}

void clearGdalNetworkDeny()
{
    CPLSetConfigOption( "CPL_VSIL_CURL_ALLOWED_EXTENSIONS", nullptr );
    CPLSetConfigOption( "PROJ_NETWORK", nullptr );
    CPLSetConfigOption( "AWS_NO_SIGN_REQUEST", nullptr );
}

} // namespace sicnu::geo::offline
