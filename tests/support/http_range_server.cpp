/***************************************************************************
  tests/support/http_range_server.cpp — local range-capable HTTP fixture.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "http_range_server.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

// Windows spells the both-directions shutdown "SD_BOTH"; POSIX "SHUT_RDWR".
#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif

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
#else
  // Writing to a socket whose peer already closed raises SIGPIPE and kills
  // the test process (observed as exit 141 when curl drops the connection
  // mid-response): the fixture checks send() return values itself — the
  // signal must not fatal-exit behind its back.
  std::signal( SIGPIPE, SIG_IGN );
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

void boundSocketWait( SocketHandle socket )
{
  if ( socket == kInvalidSocket )
    return;
#ifdef _WIN32
  const DWORD timeoutMs = 2000;
  ::setsockopt( socket, SOL_SOCKET, SO_RCVTIMEO,
                reinterpret_cast<const char *>( &timeoutMs ), sizeof( timeoutMs ) );
#else
  timeval timeout {};
  timeout.tv_sec = 2;
  timeout.tv_usec = 0;
  ::setsockopt( socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
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
  // shutdown() alone does not reliably wake a thread blocked in accept()
  // (Linux returns ENOTCONN for listening sockets): wake it deterministically
  // with a loopback connect. The dummy connection drains like any other —
  // its client end closes immediately, so the server's bounded recv sees EOF.
  if ( mPort != 0 )
  {
    SocketHandle wake = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( wake != kInvalidSocket )
    {
      sockaddr_in address {};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
      address.sin_port = htons( static_cast<uint16_t>( mPort ) );
      if ( ::connect( wake, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == 0 )
      {
        ::shutdown( wake, SD_BOTH );
        shutdownSocket( wake );
      }
      else
      {
        shutdownSocket( wake );
      }
    }
  }
  // Clients held by the serve loop or a handler thread (connected, request
  // head never completed) must not keep threads blocked past teardown:
  // SHUTDOWN each in-flight socket. POSIX does not wake a peer thread's
  // recv() when an fd is CLOSEd elsewhere, and closing here could hit an fd
  // number another component has already reused — only the owning thread
  // closes. The bounded receive window covers anything this misses.
  // (Copy the set and shutdown OUTSIDE the mutex: a finishing handler needs
  // the same mutex for its erase — never hold it across any blocking call.)
  {
    std::set<SocketHandle> inFlight;
    {
      std::lock_guard<std::mutex> lock( mHandlerMutex );
      inFlight = mInFlight;
    }
    for ( const SocketHandle client : inFlight )
      ::shutdown( client, SD_BOTH );
  }
  if ( mThread.joinable() )
    mThread.join();
  // Concurrent-mode handlers touch fixture state — join every one of them
  // before the members they reference start disappearing. Join OUTSIDE the
  // mutex: a handler's own completion path locks it (mInFlight.erase), so
  // joining under the lock is a guaranteed self-deadlock.
  {
    std::vector<std::thread> toJoin;
    {
      std::lock_guard<std::mutex> lock( mHandlerMutex );
      toJoin = std::move( mHandlers );
      mHandlers.clear();
    }
    for ( std::thread &handler : toJoin )
      if ( handler.joinable() )
        handler.join();
    {
      std::lock_guard<std::mutex> lock( mHandlerMutex );
      mInFlight.clear();
    }
  }
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
    // A client that connects and stays silent must not pin the server
    // thread: bound the request-head receive window (see boundSocketWait).
    boundSocketWait( client );
    if ( mMaxConnections.load() > 1 && mLiveHandlers.load() < mMaxConnections.load() )
    {
      // 8.0 concurrent mode: serve on a bounded side thread so several
      // readers can be in flight at once (real COG clients read in parallel).
      mLiveHandlers.fetch_add( 1 );
      {
        std::lock_guard<std::mutex> lock( mHandlerMutex );
        mInFlight.insert( client );
      }
      std::thread handler( [this, client] {
        handleConnection( client );
        {
          std::lock_guard<std::mutex> lock( mHandlerMutex );
          mInFlight.erase( client );
        }
        shutdownSocket( client ); // the owning thread closes its own socket
        mLiveHandlers.fetch_sub( 1 );
      } );
      std::lock_guard<std::mutex> lock( mHandlerMutex );
      // Handles accumulate per connection (tests issue a bounded handful)
      // and are joined in the destructor.
      mHandlers.push_back( std::move( handler ) );
      continue;
    }
    {
      std::lock_guard<std::mutex> lock( mHandlerMutex );
      mInFlight.insert( client );
    }
    handleConnection( client );
    {
      std::lock_guard<std::mutex> lock( mHandlerMutex );
      mInFlight.erase( client );
    }
    shutdownSocket( client );
  }
  // Serial mode drains its own connection; concurrent mode's detached
  // handlers own theirs. Both finish in bounded time: recv windows are
  // bounded and the destructor additionally closes the listener + the
  // in-flight client, so accepts and recvs cannot block indefinitely.
}

void HttpRangeServer::setConcurrency( unsigned maxConnections )
{
  if ( maxConnections > 1 )
    mMaxConnections = maxConnections > 8 ? 8 : maxConnections; // bounded by design
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
  // ...) get a 404 so byte accounting measures the asset itself. The query
  // string does not affect routing (a signed URL carries its credentials in
  // the query — the object path stays the same).
  {
    const std::size_t sp1 = request.find( ' ' );
    const std::size_t sp2 = request.find( ' ', sp1 == std::string::npos ? 0 : sp1 + 1 );
    std::string path = request.substr( sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1 );
    const std::size_t query = path.find( '?' );
    if ( query != std::string::npos )
      path = path.substr( 0, query );
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
    if ( mBehavior == ServerBehavior::ResetRanged && !isHead && rangeStart >= 1024 &&
         mResetArmed.exchange( false ) )
    {
      // Answer the headers, hand over a few body bytes, then kill the
      // connection hard (SO_LINGER 0 ⇒ RST) — a mid-transfer connection
      // reset, not a graceful short read. The reset fires ONCE (a transient
      // fault): a permanently hostile origin would starve the /vsicurl/
      // fallback too, and the cache's fallback guarantee needs a recoverable
      // origin. Identity probes fetch the head window (bytes=0-1023) and
      // HEAD answers never consume the fault (nothing is on the wire to
      // reset).
      {
        std::string head = "HTTP/1.1 206 Partial Content\r\n";
        for ( const auto &entry : headers )
          head += entry.first + ": " + entry.second + "\r\n";
        head += "Content-Length: " + std::to_string( bodySize ) + "\r\n";
        head += "Connection: close\r\n\r\n";
        std::size_t sent = 0;
        while ( sent < head.size() )
        {
          const int written = ::send( client, head.data() + sent,
                                      static_cast<int>( head.size() - sent ), 0 );
          if ( written <= 0 )
            break;
          sent += static_cast<std::size_t>( written );
        }
        const std::size_t bytesBeforeReset = std::min<std::size_t>( bodySize, 4 );
        if ( bytesBeforeReset > 0 )
          ::send( client, reinterpret_cast<const char *>( body ),
                  static_cast<int>( bytesBeforeReset ), 0 );
        mBytesServed.fetch_add( bytesBeforeReset );
        linger options {};
        options.l_onoff = 1;
        options.l_linger = 0;
        ::setsockopt( client, SOL_SOCKET, SO_LINGER,
                      reinterpret_cast<const char *>( &options ), sizeof( options ) );
        // serveLoop's shutdownSocket() now closes through the linger-0
        // setting — the peer sees a hard reset mid-body.
      }
      return;
    }
    if ( mBehavior == ServerBehavior::ShortRange && !isHead && rangeStart >= 1024 &&
         bodySize > 8 && mShortRangeArmed.exchange( false ) )
    {
      // 9.0 fault: announce the full window (Content-Length = bodySize, a
      // well-formed 206 with an honest-looking Content-Range) but send only
      // a few bytes. The recipient sees a COMPLETE-feeling HTTP answer with
      // a body shorter than the echoed window — the poison the truncation
      // gate exists for.
      std::string head = "HTTP/1.1 206 Partial Content\r\n";
      for ( const auto &entry : headers )
        head += entry.first + ": " + entry.second + "\r\n";
      head += "Content-Length: " + std::to_string( bodySize ) + "\r\n";
      head += "Connection: close\r\n\r\n";
      std::size_t sent = 0;
      while ( sent < head.size() )
      {
        const int written = ::send( client, head.data() + sent,
                                    static_cast<int>( head.size() - sent ), 0 );
        if ( written <= 0 )
          break;
        sent += static_cast<std::size_t>( written );
      }
      const std::size_t bytesSent = std::min<std::size_t>( bodySize, 4 );
      if ( bytesSent > 0 )
        ::send( client, reinterpret_cast<const char *>( body ),
                static_cast<int>( bytesSent ), 0 );
      mBytesServed.fetch_add( bytesSent );
      return;
    }
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
