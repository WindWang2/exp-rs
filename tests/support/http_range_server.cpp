/***************************************************************************
  tests/support/http_range_server.cpp — local range-capable HTTP fixture.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "http_range_server.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace sicnu::geo::testsupport
{

void initializeSockets()
{
#ifdef _WIN32
  static std::once_flag flag;
  std::call_once( flag, [] {
    WSADATA data;
    WSAStartup( MAKEWORD( 2, 2 ), &data );
  } );
#endif
}

void shutdownSocket( SocketHandle socket )
{
#ifdef _WIN32
  if ( socket != kInvalidSocket )
    closesocket( socket );
#else
  if ( socket != kInvalidSocket )
    ::close( socket );
#endif
}

HttpRangeServer::HttpRangeServer( std::vector<unsigned char> payload, ServerBehavior behavior )
  : mPayload( std::move( payload ) ), mBehavior( behavior )
{
  initializeSockets();
  mListener = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  address.sin_port = 0; // ephemeral
  if ( ::bind( mListener, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) != 0 ||
       ::listen( mListener, 4 ) != 0 )
  {
    shutdownSocket( mListener );
    mListener = kInvalidSocket;
    return;
  }
  socklen_t length = sizeof( address );
  if ( ::getsockname( mListener, reinterpret_cast<sockaddr *>( &address ), &length ) == 0 )
    mPort = ntohs( address.sin_port );
  mThread = std::thread( [this] { serveLoop(); } );
}

HttpRangeServer::~HttpRangeServer()
{
  mStop.store( true );
  shutdownSocket( mListener );
  if ( mThread.joinable() )
    mThread.join();
}

std::string HttpRangeServer::url() const
{
  return "http://127.0.0.1:" + std::to_string( mPort ) + "/fixture.tif";
}

void HttpRangeServer::setEtag( const std::string &etag )
{
  std::lock_guard<std::mutex> lock( mConfigMutex );
  mConfig.etag = etag;
  mConfig.hasConfig = true;
}

void HttpRangeServer::setLastModified( const std::string &lastModified )
{
  std::lock_guard<std::mutex> lock( mConfigMutex );
  mConfig.lastModified = lastModified;
  mConfig.hasConfig = true;
}

void HttpRangeServer::replacePayload( std::vector<unsigned char> payload, const std::string &etag,
                                      const std::string &lastModified )
{
  std::lock_guard<std::mutex> lock( mConfigMutex );
  mConfig.payload = std::move( payload );
  if ( !etag.empty() || !lastModified.empty() )
  {
    mConfig.etag = etag;
    mConfig.lastModified = lastModified;
  }
  mConfig.hasConfig = true;
}

HttpRangeServer::ValidatorConfig HttpRangeServer::snapshotConfig() const
{
  std::lock_guard<std::mutex> lock( mConfigMutex );
  return mConfig;
}

namespace
{

/// Fixture-grade RFC 7232 weak ETag comparison (strip W/, opaque equality).
bool fixtureWeakEtagMatch( const std::string &a, const std::string &b )
{
  const auto strip = [] ( const std::string &etag ) {
    return etag.rfind( "W/", 0 ) == 0 ? etag.substr( 2 ) : etag;
  };
  return strip( a ) == strip( b );
}

/// Extracts the value of a request header ("Name: value"), or "".
std::string requestHeaderValue( const std::string &request, const char *headerName )
{
  const std::size_t position = request.find( headerName );
  if ( position == std::string::npos )
    return std::string();
  std::size_t valueStart = position + std::strlen( headerName );
  while ( valueStart < request.size() && ( request[valueStart] == ' ' || request[valueStart] == '\t' ) )
    ++valueStart;
  const std::size_t end = request.find( "\r\n", valueStart );
  return request.substr( valueStart, end == std::string::npos ? std::string::npos : end - valueStart );
}

} // namespace

void HttpRangeServer::serveLoop()
{
  while ( !mStop.load() )
  {
    SocketHandle client = ::accept( mListener, nullptr, nullptr );
    if ( client == kInvalidSocket )
    {
      if ( mStop.load() )
        break;
      continue;
    }
    handleConnection( client );
    shutdownSocket( client );
  }
}

void HttpRangeServer::handleConnection( SocketHandle client )
{
  mRequestCount.fetch_add( 1 );

  // Read the request head (bounded).
  std::string request;
  char buffer[2048];
  while ( request.find( "\r\n\r\n" ) == std::string::npos && request.size() < sizeof( buffer ) )
  {
    const int received = ::recv( client, buffer, sizeof( buffer ), 0 );
    if ( received <= 0 )
      break;
    request.append( buffer, static_cast<std::size_t>( received ) );
  }

  const bool isHead = request.rfind( "HEAD ", 0 ) == 0;

  // Only the fixture path is served; auxiliary probes (.aux.xml, .properties,
  // ...) get a 404 so byte accounting measures the asset itself.
  {
    const std::size_t sp1 = request.find( ' ' );
    const std::size_t sp2 = request.find( ' ', sp1 == std::string::npos ? 0 : sp1 + 1 );
    const std::string path = request.substr( sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1 );
    if ( path != "/fixture.tif" )
    {
      std::fprintf( stderr, "[SRV] 404 for path %s\n", path.c_str() );
      respond( client, 404, "Not Found", {}, nullptr, 0, false, true );
      return;
    }
  }
  std::string rangeHeader;
  {
    const std::size_t rangePosition = request.find( "Range:" );
    if ( rangePosition != std::string::npos )
    {
      const std::size_t end = request.find( "\r\n", rangePosition );
      rangeHeader = request.substr( rangePosition, end == std::string::npos ? std::string::npos : end - rangePosition );
    }
  }
  const std::string ifNoneMatch = requestHeaderValue( request, "If-None-Match:" );
  const std::string ifModifiedSince = requestHeaderValue( request, "If-Modified-Since:" );

  if ( mBehavior == ServerBehavior::ServerError )
  {
    respond( client, 500, "Internal Server Error", {}, nullptr, 0, false, true );
    return;
  }
  if ( mBehavior == ServerBehavior::Slow )
  {
    std::this_thread::sleep_for( std::chrono::milliseconds( 3000 ) );
  }

  // 7.0: validator fixtures (mutable mid-test; the mutex-guarded config
  // wins once a fixture API has been used).
  const ValidatorConfig config = snapshotConfig();
  const bool useFixtureConfig = config.hasConfig && !config.payload.empty();
  const unsigned char *payloadData = useFixtureConfig ? config.payload.data() : mPayload.data();
  const std::size_t payloadSize = useFixtureConfig ? config.payload.size() : mPayload.size();

  std::map<std::string, std::string> headers;
  // A server that ignores Range requests must not advertise range support.
  if ( mBehavior != ServerBehavior::NoRange )
    headers["Accept-Ranges"] = "bytes";
  headers["Content-Type"] = "image/tiff";
  if ( !config.etag.empty() )
    headers["ETag"] = config.etag;
  if ( !config.lastModified.empty() )
    headers["Last-Modified"] = config.lastModified;

  // RFC 7232 conditional answers. If-None-Match uses the weak comparison;
  // If-Modified-Since revalidates on fixture-grade date equality.
  const bool conditional = !ifNoneMatch.empty() || !ifModifiedSince.empty();
  if ( conditional && mBehavior != ServerBehavior::ServerError )
  {
    bool matched = false;
    if ( !ifNoneMatch.empty() && !config.etag.empty() )
      matched = fixtureWeakEtagMatch( ifNoneMatch, config.etag );
    else if ( ifNoneMatch.empty() && !ifModifiedSince.empty() && !config.lastModified.empty() )
      matched = ifModifiedSince == config.lastModified;
    if ( matched )
    {
      mNotModifiedResponses.fetch_add( 1 );
      {
        std::lock_guard<std::mutex> lock( mLogMutex );
        if ( mRequestLog.size() < 64 )
          mRequestLog.push_back( "304 " + ( ifNoneMatch.empty() ? ifModifiedSince : ifNoneMatch ) );
      }
      // A bare 304: no body, no Content-Length (RFC 7232 §4.1).
      respond( client, 304, "Not Modified", {}, nullptr, 0, isHead, true );
      return;
    }
  }

  const unsigned char *body = payloadData;
  std::size_t bodySize = payloadSize;

  // Range support: honor "bytes=a-b" unless the fixture forbids it.
  long long rangeStart = 0;
  long long rangeEnd = static_cast<long long>( payloadSize ) - 1;
  bool ranged = false;
  if ( mBehavior != ServerBehavior::NoRange && !rangeHeader.empty() )
  {
    const std::size_t eq = rangeHeader.find( '=' );
    const std::size_t dash = rangeHeader.find( '-' );
    if ( eq != std::string::npos && dash != std::string::npos )
    {
      try
      {
        rangeStart = std::stoll( rangeHeader.substr( eq + 1, dash - eq - 1 ) );
        if ( rangeStart < 0 )
          rangeStart = 0; // reject suffix ranges ("bytes=-N") — out-of-bounds reads
        const std::string endText = rangeHeader.substr( dash + 1 );
        if ( !endText.empty() )
          rangeEnd = std::stoll( endText );
        rangeEnd = std::min<long long>( rangeEnd, static_cast<long long>( payloadSize ) - 1 );
        if ( rangeStart <= rangeEnd )
          ranged = true;
      }
      catch ( const std::exception & )
      {
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock( mLogMutex );
    std::string signature = ( isHead ? std::string( "HEAD " ) : std::string( "GET " ) ) + rangeHeader;
    if ( !ifNoneMatch.empty() )
      signature += " | INM: " + ifNoneMatch;
    if ( !ifModifiedSince.empty() )
      signature += " | IMS: " + ifModifiedSince;
    if ( mRequestLog.size() < 64 )
      mRequestLog.push_back( signature );
  }
  if ( ranged )
  {
    mRangedResponses.fetch_add( 1 );
    headers["Content-Range"] = "bytes " + std::to_string( rangeStart ) + "-" + std::to_string( rangeEnd ) +
                               "/" + std::to_string( payloadSize );
    body = payloadData + rangeStart;
    bodySize = static_cast<std::size_t>( rangeEnd - rangeStart + 1 );
    respond( client, 206, "Partial Content", headers, body, bodySize, isHead,
             mBehavior != ServerBehavior::Truncated );
    return;
  }

  mFullResponses.fetch_add( 1 );
  respond( client, 200, "OK", headers, body, bodySize, isHead,
           mBehavior != ServerBehavior::Truncated );
}

void HttpRangeServer::respond( SocketHandle client, int status, const std::string &statusText,
                               const std::map<std::string, std::string> &extraHeaders,
                               const unsigned char *body, std::size_t bodySize, bool isHead, bool /*close*/ )
{
  std::string response = "HTTP/1.1 " + std::to_string( status ) + " " + statusText + "\r\n";
  if ( status >= 400 )
    std::fprintf( stderr, "[SRV] answering %d\n", status );
  for ( const auto &entry : extraHeaders )
    response += entry.first + ": " + entry.second + "\r\n";
  // RFC 7232 §4.1: a 304 MUST NOT carry a body (and our client-side 304
  // shape detection relies on the absence of entity framing).
  if ( status == 304 )
  {
    response += "Connection: close\r\n\r\n";
    std::size_t sent = 0;
    while ( sent < response.size() )
    {
      const int written = ::send( client, response.data() + sent,
                                  static_cast<int>( response.size() - sent ), 0 );
      if ( written <= 0 )
        return;
      sent += static_cast<std::size_t>( written );
    }
    return;
  }
  response += "Content-Length: " + std::to_string( bodySize ) + "\r\n";
  response += "Connection: close\r\n\r\n";

  std::size_t sent = 0;
  while ( sent < response.size() )
  {
    const int written = ::send( client, response.data() + sent,
                                static_cast<int>( response.size() - sent ), 0 );
    if ( written <= 0 )
      return;
    sent += static_cast<std::size_t>( written );
  }
  if ( isHead )
    return; // HEAD announces the entity length; no body is sent
  mBytesServed.fetch_add( bodySize );
  // Truncated behavior: announce the full body but reset mid-transfer.
  const std::size_t effective = mBehavior == ServerBehavior::Truncated && bodySize > 8 ? 4 : bodySize;
  sent = 0;
  while ( sent < effective )
  {
    const int written = ::send( client, reinterpret_cast<const char *>( body ) + sent,
                                static_cast<int>( effective - sent ), 0 );
    if ( written <= 0 )
      return;
    sent += static_cast<std::size_t>( written );
  }
}

std::vector<std::string> HttpRangeServer::requestLog() const
{
  std::lock_guard<std::mutex> lock( mLogMutex );
  return mRequestLog;
}

} // namespace sicnu::geo::testsupport
