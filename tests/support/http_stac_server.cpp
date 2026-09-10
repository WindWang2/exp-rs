/***************************************************************************
  tests/support/http_stac_server.cpp — loopback JSON/STAC API fixture.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "http_stac_server.h"

#include "http_range_server.h"

#include <cstring>
#include <utility>

// Windows spells the both-directions shutdown "SD_BOTH"; POSIX "SHUT_RDWR"
// (kept in sync with http_range_server.cpp).
#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif

namespace sicnu::geo::testsupport
{

namespace
{

/// Reads a bounded request head + optional Content-Length body.
void readRequest( SocketHandle client, std::string &head, std::string &body )
{
  char buffer[4096];
  while ( head.find( "\r\n\r\n" ) == std::string::npos && head.size() < sizeof( buffer ) * 4 )
  {
    const int received = ::recv( client, buffer, sizeof( buffer ), 0 );
    if ( received <= 0 )
      return;
    head.append( buffer, static_cast<std::size_t>( received ) );
  }
  const std::size_t split = head.find( "\r\n\r\n" );
  body = head.substr( split + 4 );
  head = head.substr( 0, split );

  std::size_t contentLength = 0;
  const std::size_t cl = head.find( "Content-Length:" );
  if ( cl == std::string::npos )
    return;
  const std::size_t end = head.find( "\r\n", cl );
  try
  {
    contentLength = std::stoul( head.substr( cl + 15, end == std::string::npos ? std::string::npos
                                                                             : end - cl - 15 ) );
  }
  catch ( const std::exception & )
  {
    return;
  }
  while ( body.size() < contentLength && body.size() < 1024 * 1024 )
  {
    const int received = ::recv( client, buffer, sizeof( buffer ), 0 );
    if ( received <= 0 )
      break;
    body.append( buffer, static_cast<std::size_t>( received ) );
  }
}

void sendAll( SocketHandle client, const std::string &bytes )
{
  std::size_t sent = 0;
  while ( sent < bytes.size() )
  {
    const int written = ::send( client, bytes.data() + sent,
                                static_cast<int>( bytes.size() - sent ), 0 );
    if ( written <= 0 )
      return;
    sent += static_cast<std::size_t>( written );
  }
}

} // namespace

HttpStacServer::HttpStacServer()
{
  initializeSockets();
  mListener = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  address.sin_port = 0;
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

HttpStacServer::~HttpStacServer()
{
  mStop.store( true );
  shutdownSocket( mListener );
  // shutdown() alone does not reliably wake a thread blocked in accept()
  // (Linux returns ENOTCONN for listening sockets): wake it deterministically
  // with a loopback connect the server drains like any other request.
  if ( mPort != 0 )
  {
    SocketHandle wake = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( wake != kInvalidSocket )
    {
      sockaddr_in address {};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
      address.sin_port = htons( static_cast<uint16_t>( mPort ) );
      const bool connected = ::connect( wake, reinterpret_cast<sockaddr *>( &address ),
                                        sizeof( address ) ) == 0;
      ::shutdown( wake, SD_BOTH );
      shutdownSocket( wake );
      ( void ) connected;
    }
  }
  if ( mThread.joinable() )
    mThread.join();
}

std::string HttpStacServer::url() const
{
  return "http://127.0.0.1:" + std::to_string( mPort );
}

void HttpStacServer::setRoute( const std::string &pathAndQuery, const StacRoute &route )
{
  std::lock_guard<std::mutex> lock( mMutex );
  mRoutes[pathAndQuery] = route;
}

std::vector<StacRequestRecord> HttpStacServer::requests() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mRequests;
}

void HttpStacServer::serveLoop()
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
    // thread: bound the request-head receive window (see http_range_server).
    boundSocketWait( client );
    handleConnection( client );
    shutdownSocket( client );
  }
}

void HttpStacServer::handleConnection( SocketHandle client )
{
  std::string head;
  std::string body;
  readRequest( client, head, body );

  StacRequestRecord record;
  {
    const std::size_t sp1 = head.find( ' ' );
    const std::size_t sp2 = head.find( ' ', sp1 == std::string::npos ? 0 : sp1 + 1 );
    const std::string target =
      head.substr( sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1 );
    record.method = head.substr( 0, sp1 );
    const std::size_t q = target.find( '?' );
    record.path = q == std::string::npos ? target : target.substr( 0, q );
    record.query = q == std::string::npos ? std::string() : target.substr( q + 1 );
    record.body = body.substr( 0, 4096 );
  }
  {
    std::lock_guard<std::mutex> lock( mMutex );
    if ( mRequests.size() < 256 )
      mRequests.push_back( record );
  }

  StacRoute route;
  bool matched = false;
  {
    std::lock_guard<std::mutex> lock( mMutex );
    // Exact path+query match wins, then path-only match.
    const std::string withQuery = record.path + ( record.query.empty() ? "" : "?" + record.query );
    auto exact = mRoutes.find( withQuery );
    if ( exact != mRoutes.end() )
    {
      route = exact->second;
      matched = true;
    }
    else
    {
      auto pathOnly = mRoutes.find( record.path );
      if ( pathOnly != mRoutes.end() )
      {
        route = pathOnly->second;
        matched = true;
      }
    }
  }
  if ( !matched )
  {
    sendAll( client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" );
    return;
  }

  std::string response = "HTTP/1.1 " + std::to_string( route.status ) + " OK\r\n";
  if ( route.status >= 400 )
    response = "HTTP/1.1 " + std::to_string( route.status ) + " Error\r\n";
  response += "Content-Type: " + route.contentType + "\r\n";
  for ( const auto &entry : route.headers )
    response += entry.first + ": " + entry.second + "\r\n";
  response += "Content-Length: " + std::to_string( route.body.size() ) + "\r\n";
  response += "Connection: close\r\n\r\n";
  sendAll( client, response );
  sendAll( client, route.body );
}

} // namespace sicnu::geo::testsupport
