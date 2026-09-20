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
#include <set>
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
  Gone,          ///< every request answers 410 (the strict-fetch NotFound arm)
  Truncated,     ///< body cut short (connection reset mid-payload)
  Slow,          ///< answers after a delay (timeout probing)
  ResetRanged,   ///< 8.0: the FIRST ranged GET beyond the identity head
                 ///< window answers its headers then resets the connection
                 ///< (a transient mid-transfer reset); later requests are
                 ///< served normally — hostile origins would starve the
                 ///< fallback path too
  ShortRange,    ///< 9.0: every ranged GET answers WELL-FORMED 206 headers
                 ///< that claim the full requested window but sends only a
                 ///< few body bytes — a truncated transfer at the HTTP
                 ///< framing level (vs Truncated's connection reset). The
                 ///< cache must refuse to serve/cache such answers.
  LongRange,     ///< 9.0: the FIRST ranged GET beyond the identity head
                 ///< window answers well-formed 206 headers for its window
                 ///< but sends window + 1 KiB, the extra bytes being GARBAGE
                 ///< (not payload continuation). An over-long body that
                 ///< slips past the echoed-window gate would poison the
                 ///< NEXT block(s) with checksum-valid garbage.
  RangeNotSatisfiable, ///< 12.0: every ranged GET with start > 0 answers
                 ///< 416 with "Content-Range: bytes */<size>" — the shape a
                 ///< real origin produces when the object SHRANK below the
                 ///< requested offsets (or a broken middle tier lost its
                 ///< range table). The head window (start == 0) and
                 ///< non-ranged answers stay NORMAL so identity probes keep
                 ///< working; only body fetches hit the fault. The cache
                 ///< must degrade to its fallback and never cache the 416.
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

    // --- 8.0 concurrency fixture -----------------------------------------
    /// Serves up to @p maxConnections connections CONCURRENTLY (thread per
    /// connection, hard-bounded at 8; ≤1 keeps the legacy serial serve
    /// loop). Lets tests exercise concurrent readers of one cached resource.
    /// Call at construction time, before any request arrives.
    void setConcurrency( unsigned maxConnections );

    // --- 12.0 deterministic fault script ----------------------------------
    /// Installs a per-request fault script: request #n (0-based, counting
    /// every SERVED-path connection since installation) answers with
    /// script[n]'s behavior while n < script.size(), then falls back to the
    /// construction behavior.
    /// Serial-use contract: slot assignment relies on the single-threaded
    /// serve loop — with setConcurrency(>1), concurrent connections race for
    /// script slots (their arrival order is not deterministic).
    /// Lets tests stage DETERMINISTIC multi-fault sequences (500 → normal,
    /// 416 → normal, …) for retry/backoff coverage. Replaces any previous
    /// script. Scripted entries drive the connection-handling arms; the
    /// mid-body Truncated cut stays a construction-behavior arm.
    void setFaultScript( const std::vector<ServerBehavior> &script );

    /// True when the transient ResetRanged fault has actually fired (lets
    /// tests prove the reset path was exercised, not just the fallback).
    bool resetFired() const { return !mResetArmed.load(); }
    /// True when the ShortRange truncation fault has fired (transient, like
    /// ResetRanged — a permanently truncated origin would starve the
    /// /vsicurl/ fallback, which is itself a ranged reader).
    bool shortRangeFired() const { return !mShortRangeArmed.load(); }
    /// True when the LongRange over-long fault has fired (transient).
    bool longRangeFired() const { return !mLongRangeArmed.load(); }

  private:
    void serveLoop();
    void handleConnection( SocketHandle client );
    void respond( SocketHandle client, int status, const std::string &statusText,
                  const std::map<std::string, std::string> &extraHeaders,
                  const unsigned char *body, std::size_t bodySize, bool isHead, bool close );
    /// The behavior governing THIS connection: the fault script entry for
    /// the request index when scripted, else the construction behavior.
    ServerBehavior effectiveBehavior() const;

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
    /// 12.0 fault script (guarded — tests may install it between requests).
    /// Relative to SERVED-path requests (mServedRequests) at installation.
    mutable std::mutex mScriptMutex;
    std::vector<ServerBehavior> mScript;
    int mScriptBase = 0;
    std::atomic<int> mServedRequests{ 0 };
    SocketHandle mListener = kInvalidSocket;
    /// Connections currently owned by the serve loop / handler threads.
    /// Teardown SHUTDOWNS (never closes — the owning thread closes) every
    /// one of them: a client that opened a connection but never completed a
    /// request head (connection-pool preconnects do) must not hold a handler
    /// in recv() past fixture destruction.
    std::set<SocketHandle> mInFlight;
    /// Max live handler threads when concurrency > 1 (0 = serial loop).
    /// Atomic: serveLoop reads it while a test may still configure it.
    std::atomic<unsigned> mMaxConnections{ 0 };
    std::atomic<unsigned> mLiveHandlers{ 0 };
    /// ResetRanged is transient: armed until the first eligible request.
    std::atomic<bool> mResetArmed{ true };
    std::atomic<bool> mShortRangeArmed{ true };
    std::atomic<bool> mLongRangeArmed{ true };
    /// 8.0 concurrent-mode handler threads (joined by the destructor —
    /// they touch fixture state, so they must never outlive it).
    std::mutex mHandlerMutex;
    std::vector<std::thread> mHandlers;
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
/// Bounds how long a recv() may wait on @p socket (test fixtures must never
/// hang on a client that connects and stays silent). Best effort — failures
/// are ignored; the bounded recv still works without it.
void boundSocketWait( SocketHandle socket );

} // namespace sicnu::geo::testsupport
