// mini_cog_server.h — minimal range-capable HTTP server for tests that need
// a remote raster endpoint (Phase A benchmarks, Phase F remote cache tests).
// Plain blocking POSIX sockets on a dedicated std::thread: no Qt event loop,
// so it serves while the test's main thread blocks on task completion.
#pragma once

#include <QString>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QFile>

#include <atomic>
#include <climits>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

namespace sicnu_test
{

namespace
{
void sendAll( int fd, const char *data, size_t size )
{
    size_t sent = 0;
    while ( sent < size )
    {
        const ssize_t n = ::send( fd, data + sent, size - sent, MSG_NOSIGNAL );
        if ( n <= 0 )
            return;
        sent += static_cast<size_t>( n );
    }
}

} // namespace

class MiniCogServer
{
  public:
    explicit MiniCogServer( const QString &filePath ) : m_filePath( filePath ) {}
    ~MiniCogServer() { stop(); }

    bool start()
    {
        m_listenFd = ::socket( AF_INET, SOCK_STREAM, 0 );
        if ( m_listenFd < 0 )
            return false;
        int reuse = 1;
        ::setsockopt( m_listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        addr.sin_port = 0;
        if ( ::bind( m_listenFd, reinterpret_cast<sockaddr *>( &addr ), sizeof( addr ) ) < 0
             || ::listen( m_listenFd, 8 ) < 0 )
        {
            ::close( m_listenFd );
            m_listenFd = -1;
            return false;
        }
        socklen_t len = sizeof( addr );
        ::getsockname( m_listenFd, reinterpret_cast<sockaddr *>( &addr ), &len );
        m_port = ntohs( addr.sin_port );
        m_thread = std::thread( [this] { acceptLoop(); } );
        return true;
    }

    int port() const { return m_port; }
    quint64 requests() const { return m_requests.load(); }
    quint64 rangeRequests() const { return m_rangeRequests.load(); }

    void stop()
    {
        if ( m_listenFd < 0 )
            return;
        ::shutdown( m_listenFd, SHUT_RDWR );
        ::close( m_listenFd );
        m_listenFd = -1;
        if ( m_thread.joinable() )
            m_thread.join();
    }

  private:
    void acceptLoop()
    {
        while ( true )
        {
            const int fd = ::accept( m_listenFd, nullptr, nullptr );
            if ( fd < 0 )
                return; // listener closed → stop
            serveConnection( fd );
            ::close( fd );
        }
    }

    void serveConnection( int fd )
    {
        // Serve sequential requests on one connection until EOF/timeout.
        std::string buf;
        while ( true )
        {
            // Read one request head.
            size_t headEnd = buf.find( "\r\n\r\n" );
            while ( headEnd == std::string::npos )
            {
                char chunk[4096];
                const ssize_t n = ::recv( fd, chunk, sizeof( chunk ), 0 );
                if ( n <= 0 )
                    return;
                buf.append( chunk, static_cast<size_t>( n ) );
                headEnd = buf.find( "\r\n\r\n" );
            }
            const std::string request = buf.substr( 0, headEnd );
            buf.erase( 0, headEnd + 4 );
            m_requests.fetch_add( 1, std::memory_order_relaxed );

            qint64 rangeStart = -1, rangeEnd = -1;
            size_t pos = 0;
            while ( true )
            {
                const size_t lineEnd = request.find( '\n', pos );
                const std::string line =
                    request.substr( pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos );
                pos = lineEnd + 1;
                if ( line.compare( 0, 13, "Range: bytes=" ) == 0 )
                {
                    const std::string spec = line.substr( 13 );
                    const size_t dash = spec.find( '-' );
                    rangeStart = std::atoll( spec.substr( 0, dash ).c_str() );
                    const std::string endPart =
                        dash == std::string::npos ? std::string() : spec.substr( dash + 1 );
                    rangeEnd = endPart.empty() ? std::numeric_limits<qint64>::max()
                                               : std::atoll( endPart.c_str() );
                    m_rangeRequests.fetch_add( 1, std::memory_order_relaxed );
                }
                if ( lineEnd == std::string::npos )
                    break;
            }

            QFile file( m_filePath );
            if ( !file.open( QIODevice::ReadOnly ) )
                return;
            const qint64 total = file.size();
            qint64 start = 0, end = total - 1;
            const bool partial = rangeStart >= 0;
            if ( partial )
            {
                if ( rangeStart >= total )
                    return;
                start = rangeStart;
                end = std::min( rangeEnd, total - 1 );
            }
            const qint64 length = end - start + 1;
            std::string head;
            head += partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
            head += "Content-Type: image/tiff\r\n";
            head += "Content-Length: " + std::to_string( length ) + "\r\n";
            if ( partial )
                head += "Content-Range: bytes " + std::to_string( start ) + "-"
                        + std::to_string( end ) + "/" + std::to_string( total ) + "\r\n";
            head += "Connection: close\r\n";
            head += "Accept-Ranges: bytes\r\n\r\n";
            sendAll( fd, head.data(), head.size() );
            file.seek( start );
            constexpr qint64 kChunk = 256 * 1024;
            qint64 remaining = length;
            char sink[256 * 1024];
            while ( remaining > 0 )
            {
                const qint64 want = std::min( kChunk, remaining );
                const qint64 got = file.read( sink, want );
                if ( got <= 0 )
                    break;
                sendAll( fd, sink, static_cast<size_t>( got ) );
                remaining -= got;
            }
            // HTTP/1.0-style close per request keeps the server stateless;
            // GDAL/curl reconnects for further ranges.
            return;
        }
    }

    QString m_filePath;
    std::thread m_thread;
    int m_listenFd = -1;
    int m_port = 0;
    std::atomic<quint64> m_requests{ 0 };
    std::atomic<quint64> m_rangeRequests{ 0 };
};

} // namespace sicnu_test
