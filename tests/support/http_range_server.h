/***************************************************************************
  tests/support/http_range_server.h
  Foundation 5.0 — local, test-only HTTP server with Range support and
  failure injection (ADR 0139's proof harness).
  7.0 — validator fixtures: ETag / Last-Modified emission, conditional GET
  (RFC 7232) answers and mid-test content replacement.

  Loopback-only, one connection at a time, zero dependencies beyond the OS
  socket API. Serves one in-memory payload. Byte accounting proves that a
  COG window read over HTTP does not download the file.
 ***************************************************************************/

#pragma once

#include <atomic>
#include <mutex>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace sicnu::geo::testsupport
{

/// Behaviors a test can inject to exercise failure paths.
enum class ServerBehavior
{
  Normal,        ///< Range honored, full answers
  NoRange,       ///< Range header ignored (always 200 + whole file)
  ServerError,   ///< every request answers 500
  Truncated,     ///< body cut short (connection reset mid-payload)
  Slow           ///< answers after a delay (timeout probing)
};

class HttpRangeServer
{
  public:
    /// Binds an ephemeral loopback port and starts serving in a background
    /// thread. serveFileBytes is the payload handed to every response.
    HttpRangeServer( std::vector<unsigned char> payload, ServerBehavior behavior = ServerBehavior::Normal );
    ~HttpRangeServer();

    /// URL of the served file ("http://127.0.0.1:<port>/fixture.tif").
    std::string url() const;
    /// Bytes actually sent out (headers excluded) — the download accounting.
    std::uint64_t bytesServed() const { return mBytesServed.load(); }
    int requestCount() const { return mRequestCount.load(); }
    int port() const { return mPort; }
    int fullResponses() const { return mFullResponses.load(); }
    int rangedResponses() const { return mRangedResponses.load(); }
    /// 304 answers served after a conditional request matched.
    int notModifiedResponses() const { return mNotModifiedResponses.load(); }
    /// Last distinct request signatures (method + range + conditional
    /// headers), capped.
    std::vector<std::string> requestLog() const;

    // --- 7.0 validator fixtures -------------------------------------------
    /// Declares the ETag emitted on every 200/206 answer ("…" form may
    /// include a "W/" weak prefix). Empty disables emission (default).
    void setEtag( const std::string &etag );
    /// Declares the Last-Modified HTTP-date emitted on every answer.
    /// Empty disables emission (default).
    void setLastModified( const std::string &httpDate );
    /// Replaces the served payload (and optionally the validator headers) —
    /// the fixture-side "the object changed" event. The next conditional
    /// request therefore mismatches and must answer 200.
    void replacePayload( std::vector<unsigned char> payload, const std::string &etag,
                         const std::string &lastModified );

  private:
    void serveLoop();
    void handleConnection( SocketHandle client );
    void respond( SocketHandle client, int status, const std::string &statusText,
                  const std::map<std::string, std::string> &extraHeaders,
                  const unsigned char *body, std::size_t bodySize, bool isHead, bool close );

    struct ValidatorConfig
    {
      bool hasConfig = false;   ///< any fixture API was used at least once
      std::string etag;
      std::string lastModified;
      std::vector<unsigned char> payload;
    };

    std::vector<unsigned char> mPayload;   // construction payload (fallback)
    ValidatorConfig snapshotConfig() const;

    ServerBehavior mBehavior;
    SocketHandle mListener = kInvalidSocket;
    int mPort = 0;
    std::thread mThread;
    std::atomic<bool> mStop{ false };
    std::atomic<std::uint64_t> mBytesServed{ 0 };
    std::atomic<int> mRequestCount{ 0 };
    std::atomic<int> mFullResponses{ 0 };
    std::atomic<int> mRangedResponses{ 0 };
    std::atomic<int> mNotModifiedResponses{ 0 };
    mutable std::mutex mLogMutex;
    std::vector<std::string> mRequestLog;
    mutable std::mutex mConfigMutex;       // guards mConfig (validator fixtures)
    ValidatorConfig mConfig;
};

/// Windows socket stack needs per-process initialization.
void initializeSockets();
void shutdownSocket( SocketHandle socket );

} // namespace sicnu::geo::testsupport
