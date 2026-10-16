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

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#endif

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

    std::uint64_t parts[4] = {};
    int numParts = 0;
    std::size_t i = 0;

    while ( i < text.size() )
    {
        if ( numParts >= 4 )
            return false;

        int base = 10;
        if ( text[i] == '0' )
        {
            if ( i + 1 < text.size() && ( text[i + 1] == 'x' || text[i + 1] == 'X' ) )
            {
                base = 16;
                i += 2;
                if ( i == text.size() || text[i] == '.' )
                    return false;
            }
            else
            {
                base = 8;
            }
        }

        std::uint64_t val = 0;
        int digits = 0;
        while ( i < text.size() && text[i] != '.' )
        {
            const char c = text[i];
            int digit = -1;
            if ( base == 16 )
            {
                if ( c >= '0' && c <= '9' )
                    digit = c - '0';
                else if ( c >= 'a' && c <= 'f' )
                    digit = c - 'a' + 10;
                else if ( c >= 'A' && c <= 'F' )
                    digit = c - 'A' + 10;
            }
            else if ( base == 8 )
            {
                if ( c >= '0' && c <= '7' )
                    digit = c - '0';
            }
            else
            {
                if ( c >= '0' && c <= '9' )
                    digit = c - '0';
            }

            if ( digit < 0 )
                return false;

            val = val * base + static_cast<std::uint64_t>( digit );
            if ( val > 0xFFFFFFFFull )
                return false;

            ++digits;
            ++i;
        }

        if ( digits == 0 )
            return false;

        parts[numParts++] = val;

        if ( i < text.size() )
        {
            if ( text[i] != '.' )
                return false;
            ++i;
            if ( i == text.size() )
                return false;
        }
    }

    if ( numParts == 0 || numParts > 4 )
        return false;

    switch ( numParts )
    {
    case 1:
        if ( parts[0] > 0xFFFFFFFFull )
            return false;
        out = static_cast<std::uint32_t>( parts[0] );
        return true;
    case 2:
        if ( parts[0] > 0xFFull || parts[1] > 0xFFFFFFull )
            return false;
        out = static_cast<std::uint32_t>( ( parts[0] << 24 ) | parts[1] );
        return true;
    case 3:
        if ( parts[0] > 0xFFull || parts[1] > 0xFFull || parts[2] > 0xFFFFull )
            return false;
        out = static_cast<std::uint32_t>( ( parts[0] << 24 ) | ( parts[1] << 16 ) | parts[2] );
        return true;
    case 4:
        if ( parts[0] > 0xFFull || parts[1] > 0xFFull || parts[2] > 0xFFull || parts[3] > 0xFFull )
            return false;
        out = static_cast<std::uint32_t>( ( parts[0] << 24 ) | ( parts[1] << 16 ) | ( parts[2] << 8 ) | parts[3] );
        return true;
    default:
        return false;
    }
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
                               int maxGroups, bool allowV4Tail ) {
        if ( section.empty() )
            return true;
        std::size_t pos = 0;
        while ( true )
        {
            if ( count >= maxGroups )
                return false;
            const std::size_t colon = section.find( ':', pos );
            const std::string token = colon == std::string::npos ? section.substr( pos )
                                                                 : section.substr( pos, colon - pos );
            if ( allowV4Tail && colon == std::string::npos && token.find( '.' ) != std::string::npos )
            {
                std::uint32_t v4 = 0;
                if ( !parseIpv4( token, v4 ) )
                    return false;
                if ( count + 2 > maxGroups )
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
    if ( headCount >= 7 && !tail.empty() )
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

/// Private/loopback/metadata classification for a parsed IPv6 address.
/// Factored out of isPrivateHost() so the DNS pinning path (#1485) judges a
/// getaddrinfo result with exactly the same rule as a literal host - two
/// subtly different copies of this list is how an egress filter grows a hole.
bool isPrivateIpv6( const unsigned char v6[16] )
{
    bool unspecified = true;
    for ( int i = 0; i < 16; ++i )
        unspecified = unspecified && ( v6[i] == 0 );
    if ( unspecified )
        return true;

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

    // fe80::/10 link-local, fc00::/7 unique-local
    if ( v6[0] == 0xFE && ( v6[1] & 0xC0 ) == 0x80 )
        return true;
    if ( ( v6[0] & 0xFE ) == 0xFC )
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

    const std::size_t pct = host.find( '%' );
    if ( pct != std::string::npos )
        host = host.substr( 0, pct );

    std::string lowered;
    lowered.reserve( host.size() );
    for ( const char c : host )
        lowered += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

    while ( !lowered.empty() && lowered.back() == '.' )
        lowered.pop_back();

    if ( lowered.empty() )
        return true;
    if ( lowered == "localhost" ||
         lowered == "localhost.localdomain" ||
         lowered == "ip6-localhost" ||
         lowered == "ip6-loopback" ||
         ( lowered.size() > 10 && lowered.compare( lowered.size() - 10, 10, ".localhost" ) == 0 ) )
        return true;
    if ( lowered == "metadata.google.internal" || lowered == "instance-data" )
        return true;

    std::uint32_t v4 = 0;
    if ( parseIpv4( lowered, v4 ) )
        return isPrivateIpv4( v4 );

    unsigned char v6[16] = {};
    if ( parseIpv6( host, v6 ) )
        return isPrivateIpv6( v6 );

    // Unparsed hostname: perform DNS resolution to block private/loopback/cloud-metadata targets (DEF-GEO-01).
    struct addrinfo hints;
    std::memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if ( getaddrinfo( lowered.c_str(), nullptr, &hints, &res ) == 0 && res != nullptr )
    {
        bool privateFound = false;
        for ( struct addrinfo *p = res; p != nullptr; p = p->ai_next )
        {
            if ( p->ai_family == AF_INET )
            {
                auto *sa = reinterpret_cast<struct sockaddr_in *>( p->ai_addr );
                const std::uint32_t resolvedV4 = ntohl( sa->sin_addr.s_addr );
                if ( isPrivateIpv4( resolvedV4 ) )
                {
                    privateFound = true;
                    break;
                }
            }
            else if ( p->ai_family == AF_INET6 )
            {
                auto *sa6 = reinterpret_cast<struct sockaddr_in6 *>( p->ai_addr );
                const unsigned char *bytes = reinterpret_cast<const unsigned char *>( &sa6->sin6_addr );

                bool loopback = true;
                for ( int i = 0; i < 15; ++i )
                    loopback = loopback && ( bytes[i] == 0 );
                loopback = loopback && ( bytes[15] == 1 );
                if ( loopback )
                {
                    privateFound = true;
                    break;
                }

                if ( ( bytes[0] == 0xFE && ( bytes[1] & 0xC0 ) == 0x80 ) ||
                     ( ( bytes[0] & 0xFE ) == 0xFC ) )
                {
                    privateFound = true;
                    break;
                }

                bool ipv4Mapped = ( bytes[10] == 0xFF && bytes[11] == 0xFF );
                for ( int i = 0; i < 10 && ipv4Mapped; ++i )
                    ipv4Mapped = ( bytes[i] == 0 );
                if ( ipv4Mapped )
                {
                    const std::uint32_t emb = ( static_cast<std::uint32_t>( bytes[12] ) << 24 ) |
                                              ( static_cast<std::uint32_t>( bytes[13] ) << 16 ) |
                                              ( static_cast<std::uint32_t>( bytes[14] ) << 8 ) |
                                              static_cast<std::uint32_t>( bytes[15] );
                    if ( isPrivateIpv4( emb ) )
                    {
                        privateFound = true;
                        break;
                    }
                }
            }
        }
        freeaddrinfo( res );
        if ( privateFound )
            return true;
    }

    return false;
}

HostPinStatus resolveForPinning( const std::string &host, int port,
                                 std::vector<std::string> &entries,
                                 std::string &errorMessage )
{
    entries.clear();
    errorMessage.clear();

    std::string lowered;
    lowered.reserve( host.size() );
    for ( const char c : host )
        lowered += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    while ( !lowered.empty() && lowered.back() == '.' )
        lowered.pop_back();

    if ( lowered.empty() )
    {
        errorMessage = "empty host";
        return HostPinStatus::PrivateAddress;
    }

    // IP literals: isPrivateHost() already judges them verbatim, and there is
    // nothing for libcurl to resolve — pinning would only add a second,
    // redundant source of truth.
    std::uint32_t v4 = 0;
    unsigned char v6[16] = {};
    if ( parseIpv4( lowered, v4 ) || parseIpv6( lowered, v6 ) )
        return HostPinStatus::NoResolutionNeeded;

    // Names that are loopback/metadata by definition, before any resolution.
    if ( lowered == "localhost" ||
         lowered == "localhost.localdomain" ||
         lowered == "ip6-localhost" ||
         lowered == "ip6-loopback" ||
         lowered == "metadata.google.internal" ||
         lowered == "instance-data" ||
         ( lowered.size() > 10 && lowered.compare( lowered.size() - 10, 10, ".localhost" ) == 0 ) )
    {
        errorMessage = "host '" + lowered + "' is a private/loopback name";
        return HostPinStatus::PrivateAddress;
    }

    if ( port <= 0 || port > 65535 )
    {
        errorMessage = "port out of range for '" + lowered + "'";
        return HostPinStatus::Unresolved;
    }

    struct addrinfo hints;
    std::memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    if ( getaddrinfo( lowered.c_str(), nullptr, &hints, &res ) != 0 || res == nullptr )
    {
        if ( res != nullptr )
            freeaddrinfo( res );
        // FAIL CLOSED (#1485): an unresolvable host cannot be proven public.
        // Returning "public" here (the previous behaviour) meant a resolver
        // outage or an NXDOMAIN answer opened the egress gate.
        errorMessage = "DNS resolution failed for '" + lowered + "'";
        return HostPinStatus::Unresolved;
    }

    bool privateFound = false;
    std::string addresses;
    for ( struct addrinfo *p = res; p != nullptr && !privateFound; p = p->ai_next )
    {
        if ( p->ai_family == AF_INET )
        {
            auto *sa = reinterpret_cast<struct sockaddr_in *>( p->ai_addr );
            if ( isPrivateIpv4( ntohl( sa->sin_addr.s_addr ) ) )
            {
                privateFound = true;
                break;
            }
        }
        else if ( p->ai_family == AF_INET6 )
        {
            auto *sa6 = reinterpret_cast<struct sockaddr_in6 *>( p->ai_addr );
            if ( isPrivateIpv6( reinterpret_cast<const unsigned char *>( &sa6->sin6_addr ) ) )
            {
                privateFound = true;
                break;
            }
        }
        else
        {
            continue;
        }

        char text[NI_MAXHOST] = {};
        if ( getnameinfo( p->ai_addr, static_cast<socklen_t>( p->ai_addrlen ), text, sizeof( text ),
                          nullptr, 0, NI_NUMERICHOST ) != 0 )
            continue;
        if ( !addresses.empty() )
            addresses += ',';
        addresses += text;
    }
    freeaddrinfo( res );

    if ( privateFound )
    {
        errorMessage = "host '" + lowered + "' resolves to a private/loopback/metadata address";
        return HostPinStatus::PrivateAddress;
    }
    if ( addresses.empty() )
    {
        errorMessage = "DNS resolution produced no usable address for '" + lowered + "'";
        return HostPinStatus::Unresolved;
    }

    entries.push_back( lowered + ":" + std::to_string( port ) + ":" + addresses );
    return HostPinStatus::Ok;
}

namespace
{
/// Minimal authority/host/port extraction for resolvePinnedPublicAddresses()
/// (http_fetch.cpp keeps its own copies for the hop loop; these stay private
/// to this translation unit so the offline gate needs no new public surface).
std::string authorityOfUrl( const std::string &url )
{
  const std::size_t at = url.find( "://" );
  if ( at == std::string::npos )
    return std::string();
  const std::size_t end = url.find_first_of( "/?#", at + 3 );
  return url.substr( at + 3, end == std::string::npos ? std::string::npos : end - at - 3 );
}

std::string hostOfUrl( const std::string &url )
{
  std::string authority = authorityOfUrl( url );
  const std::size_t userAt = authority.rfind( '@' );
  if ( userAt != std::string::npos )
    authority = authority.substr( userAt + 1 );
  if ( !authority.empty() && authority.front() == '[' )
  {
    const std::size_t close = authority.find( ']' );
    return ( close == std::string::npos ) ? authority.substr( 1 )
                                          : authority.substr( 1, close - 1 );
  }
  const std::size_t colon = authority.rfind( ':' );
  return ( colon == std::string::npos ) ? authority : authority.substr( 0, colon );
}

int portOfUrl( const std::string &url )
{
  const std::size_t colonScheme = url.find( ':' );
  std::string scheme;
  if ( colonScheme != std::string::npos && colonScheme != 0 )
    scheme = url.substr( 0, colonScheme );
  for ( char &c : scheme )
    c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
  const int fallback = ( scheme == "https" ) ? 443 : 80;
  std::string authority = authorityOfUrl( url );
  const std::size_t userAt = authority.rfind( '@' );
  if ( userAt != std::string::npos )
    authority = authority.substr( userAt + 1 );
  std::size_t colon = std::string::npos;
  if ( !authority.empty() && authority.front() == '[' )
  {
    const std::size_t close = authority.find( ']' );
    if ( close == std::string::npos )
      return fallback;
    colon = authority.find( ':', close );
  }
  else
  {
    colon = authority.rfind( ':' );
  }
  if ( colon == std::string::npos )
    return fallback;
  const std::string digits = authority.substr( colon + 1 );
  if ( digits.empty() || digits.find_first_not_of( "0123456789" ) != std::string::npos )
    return fallback;
  const long value = std::strtol( digits.c_str(), nullptr, 10 );
  if ( value <= 0 || value > 65535 )
    return fallback;
  return static_cast<int>( value );
}
} // namespace

std::string resolvePinnedPublicAddresses( const std::string &url, std::string *curlResolveEntry )
{
    if ( curlResolveEntry != nullptr )
        curlResolveEntry->clear();

    std::vector<std::string> entries;
    std::string detail;
    const HostPinStatus status =
        resolveForPinning( hostOfUrl( url ), portOfUrl( url ), entries, detail );

    // A literal address has nothing to re-resolve: allow the hop, pin nothing.
    if ( status == HostPinStatus::NoResolutionNeeded )
        return std::string();

    if ( status == HostPinStatus::Ok )
    {
        if ( curlResolveEntry != nullptr && !entries.empty() )
            *curlResolveEntry = entries.front();
        return std::string();
    }

    // PrivateAddress / Unresolved — fail closed, same as the guarded hop path.
    return detail.empty() ? std::string( "host is not provably public" ) : detail;
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
