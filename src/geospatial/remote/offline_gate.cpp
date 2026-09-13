/***************************************************************************
 * geospatial/remote/offline_gate.cpp — process-wide offline gate (goal D7)
 ***************************************************************************/
#include "offline_gate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <cpl_conv.h>

namespace sicnu::geo::offline {

namespace {

bool g_offline = false;

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

} // namespace

void setEnabled( bool offline )
{
    g_offline = offline;
}

bool enabled()
{
    return g_offline;
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

void applyGdalNetworkDeny()
{
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
