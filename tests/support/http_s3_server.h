/***************************************************************************
  tests/support/http_s3_server.h — local S3-compatible endpoint fixture
  (fabric-10).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Header-only loopback HTTP server that speaks just enough of the S3 wire
  shape for GDAL's /vsis3/ stack against a custom endpoint
  (AWS_S3_ENDPOINT=http://127.0.0.1:<port>, AWS_HTTPS=NO,
  AWS_VIRTUAL_HOSTING=FALSE): HEAD/GET on "/<bucket>/<key>" answered with
  200/206/304 + ETag, everything else 404. This lets object-store seam
  tests run REAL GDAL S3 opens against loopback — no cloud, no SDK.
 ***************************************************************************/

#ifndef SICNU_TEST_SUPPORT_HTTP_S3_SERVER_H
#define SICNU_TEST_SUPPORT_HTTP_S3_SERVER_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <csignal>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif

namespace sicnu::geo::testsupport
{

namespace s3fixture
{

inline void s3InitSockets()
{
#ifdef _WIN32
  static std::once_flag flag;
  std::call_once( flag, [] {
    WSADATA data;
    WSAStartup( MAKEWORD( 2, 2 ), &data );
  } );
#else
  std::signal( SIGPIPE, SIG_IGN );
#endif
}

inline void s3CloseSocket( int socket )
{
#ifdef _WIN32
  if ( socket >= 0 )
    closesocket( socket );
#else
  if ( socket >= 0 )
    ::close( socket );
#endif
}

inline void s3BoundRecv( int socket )
{
  if ( socket < 0 )
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

} // namespace s3fixture

class HttpS3Server
{
  public:
    /// Binds an ephemeral loopback port and serves `payload` under
    /// /<bucket>/<key> until destruction.
    HttpS3Server( std::string bucket, std::string key, std::vector<unsigned char> payload,
                  std::string etag = {} )
      : mBucket( std::move( bucket ) ), mKey( std::move( key ) ),
        mPayload( std::move( payload ) ), mEtag( std::move( etag ) )
    {
      s3fixture::s3InitSockets();
      mListener = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
      sockaddr_in address {};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
      address.sin_port = 0;
      if ( ::bind( mListener, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) != 0 ||
           ::listen( mListener, 4 ) != 0 )
      {
        s3fixture::s3CloseSocket( mListener );
        mListener = -1;
        return;
      }
      socklen_t length = sizeof( address );
      if ( ::getsockname( mListener, reinterpret_cast<sockaddr *>( &address ), &length ) == 0 )
        mPort = ntohs( address.sin_port );
      mThread = std::thread( [this] { serveLoop(); } );
    }

    ~HttpS3Server()
    {
      mStop.store( true );
      if ( mPort != 0 )
      {
        int wake = ::socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
        if ( wake >= 0 )
        {
          sockaddr_in address {};
          address.sin_family = AF_INET;
          address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
          address.sin_port = htons( static_cast<uint16_t>( mPort ) );
          if ( ::connect( wake, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == 0 )
            ::shutdown( wake, SD_BOTH );
          s3fixture::s3CloseSocket( wake );
        }
      }
      s3fixture::s3CloseSocket( mListener );
      if ( mThread.joinable() )
        mThread.join();
      std::set<int> inFlight;
      {
        std::lock_guard<std::mutex> lock( mMutex );
        inFlight = mInFlight;
      }
      for ( const int client : inFlight )
        ::shutdown( client, SD_BOTH );
    }

    bool valid() const { return mPort != 0; }
    int port() const { return mPort; }
    /// The endpoint string for AWS_S3_ENDPOINT (no scheme → https assumed;
    /// GDAL needs the scheme for HTTP: "http://127.0.0.1:<port>").
    std::string endpoint() const { return "http://127.0.0.1:" + std::to_string( mPort ); }
    std::string url() const { return endpoint() + "/" + mBucket + "/" + mKey; }

    void setEtag( const std::string &etag )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      mEtag = etag;
    }
    void replacePayload( std::vector<unsigned char> payload, const std::string &etag )
    {
      std::lock_guard<std::mutex> lock( mMutex );
      mPayload = std::move( payload );
      if ( !etag.empty() )
        mEtag = etag;
    }
    std::uint64_t requestCount() const { return mRequestCount.load(); }
    std::uint64_t bytesServed() const { return mBytesServed.load(); }
    std::uint64_t notModified() const { return mNotModified.load(); }

  private:
    void serveLoop()
    {
      while ( !mStop.load() )
      {
        int client = ::accept( mListener, nullptr, nullptr );
        if ( client < 0 )
        {
          if ( mStop.load() )
            break;
          continue;
        }
        s3fixture::s3BoundRecv( client );
        {
          std::lock_guard<std::mutex> lock( mMutex );
          mInFlight.insert( client );
        }
        handleConnection( client );
        {
          std::lock_guard<std::mutex> lock( mMutex );
          mInFlight.erase( client );
        }
        s3fixture::s3CloseSocket( client );
      }
    }

    void handleConnection( int client )
    {
      mRequestCount.fetch_add( 1 );
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

      std::string path;
      {
        const std::size_t sp1 = request.find( ' ' );
        const std::size_t sp2 = request.find( ' ', sp1 == std::string::npos ? 0 : sp1 + 1 );
        path = request.substr( sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1 );
        const std::size_t query = path.find( '?' );
        if ( query != std::string::npos )
          path = path.substr( 0, query );
      }

      std::string etag;
      std::vector<unsigned char> payload;
      {
        std::lock_guard<std::mutex> lock( mMutex );
        etag = mEtag;
        payload = mPayload;
      }

      // Object path routing: only /<bucket>/<key> exists.
      const std::string objectPath = "/" + mBucket + "/" + mKey;
      if ( path != objectPath )
      {
        respond( client, 404, "Not Found", {}, nullptr, 0, isHead );
        return;
      }

      std::map<std::string, std::string> headers;
      headers["Accept-Ranges"] = "bytes";
      headers["Content-Type"] = "application/octet-stream";
      if ( !etag.empty() )
        headers["ETag"] = etag;

      // Conditional GET: fixture-grade exact If-None-Match equality.
      std::string ifNoneMatch;
      {
        const std::size_t position = request.find( "If-None-Match:" );
        if ( position != std::string::npos )
        {
          std::size_t start = position + std::strlen( "If-None-Match:" );
          while ( start < request.size() && request[start] == ' ' )
            ++start;
          const std::size_t end = request.find( "\r\n", start );
          ifNoneMatch = request.substr( start, end == std::string::npos ? std::string::npos : end - start );
        }
      }
      if ( !ifNoneMatch.empty() && !etag.empty() && ifNoneMatch == etag )
      {
        mNotModified.fetch_add( 1 );
        respond( client, 304, "Not Modified", {}, nullptr, 0, isHead );
        return;
      }

      const long long total = static_cast<long long>( payload.size() );
      long long rangeStart = 0;
      long long rangeEnd = total - 1;
      bool ranged = false;
      const std::size_t rangePosition = request.find( "Range:" );
      if ( rangePosition != std::string::npos )
      {
        const std::size_t end = request.find( "\r\n", rangePosition );
        const std::string rangeHeader = request.substr( rangePosition, end == std::string::npos ? std::string::npos : end - rangePosition );
        const std::size_t eq = rangeHeader.find( '=' );
        const std::size_t dash = rangeHeader.find( '-' );
        if ( eq != std::string::npos && dash != std::string::npos )
        {
          try
          {
            rangeStart = std::stoll( rangeHeader.substr( eq + 1, dash - eq - 1 ) );
            const std::string endText = rangeHeader.substr( dash + 1 );
            if ( !endText.empty() )
              rangeEnd = std::stoll( endText );
            rangeEnd = std::min<long long>( rangeEnd, total - 1 );
            if ( rangeStart >= 0 && rangeStart <= rangeEnd )
              ranged = true;
          }
          catch ( const std::exception & )
          {
          }
        }
      }

      if ( ranged )
      {
        headers["Content-Range"] = "bytes " + std::to_string( rangeStart ) + "-" + std::to_string( rangeEnd ) + "/" + std::to_string( total );
        respond( client, 206, "Partial Content", headers, payload.data() + rangeStart,
                 static_cast<std::size_t>( rangeEnd - rangeStart + 1 ), isHead );
        return;
      }
      respond( client, 200, "OK", headers, payload.data(), payload.size(), isHead );
    }

    void respond( int client, int status, const std::string &statusText,
                  const std::map<std::string, std::string> &extraHeaders,
                  const unsigned char *body, std::size_t bodySize, bool isHead )
    {
      std::string response = "HTTP/1.1 " + std::to_string( status ) + " " + statusText + "\r\n";
      for ( const auto &entry : extraHeaders )
        response += entry.first + ": " + entry.second + "\r\n";
      if ( status == 304 )
      {
        response += "Connection: close\r\n\r\n";
        sendAll( client, response );
        return;
      }
      response += "Content-Length: " + std::to_string( bodySize ) + "\r\n";
      response += "Connection: close\r\n\r\n";
      if ( !sendAll( client, response ) )
        return;
      if ( isHead )
        return;
      mBytesServed.fetch_add( bodySize );
      sendAll( client, std::string( reinterpret_cast<const char *>( body ), bodySize ) );
    }

    bool sendAll( int client, const std::string &bytes )
    {
      std::size_t sent = 0;
      while ( sent < bytes.size() )
      {
        const int written = ::send( client, bytes.data() + sent,
                                    static_cast<int>( bytes.size() - sent ), 0 );
        if ( written <= 0 )
          return false;
        sent += static_cast<std::size_t>( written );
      }
      return true;
    }

    std::string mBucket;
    std::string mKey;
    std::vector<unsigned char> mPayload;
    std::string mEtag;
    std::mutex mMutex;
    std::set<int> mInFlight;
    int mListener = -1;
    int mPort = 0;
    std::atomic<bool> mStop { false };
    std::thread mThread;
    std::atomic<std::uint64_t> mRequestCount { 0 };
    std::atomic<std::uint64_t> mBytesServed { 0 };
    std::atomic<std::uint64_t> mNotModified { 0 };
};

} // namespace sicnu::geo::testsupport

#endif // SICNU_TEST_SUPPORT_HTTP_S3_SERVER_H
