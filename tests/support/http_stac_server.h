/***************************************************************************
  tests/support/http_stac_server.h
  Cloud-Native Geospatial I/O 7.0 — loopback JSON/STAC API fixture.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Route-table HTTP server for STAC client tests: tests register exact
  path(+query) routes answering status/content-type/body (+ extra headers),
  and every request is logged (method, path, query, body head) so search
  parameter encoding and pagination behavior can be asserted.
 ***************************************************************************/

#pragma once

#include "http_range_server.h" // SocketHandle / kInvalidSocket / socket init

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sicnu::geo::testsupport
{

struct StacRoute
{
  int status = 200;
  std::string contentType = "application/json";
  std::string body;
  std::map<std::string, std::string> headers;   // extra response headers
};

struct StacRequestRecord
{
  std::string method;
  std::string path;        // path without query
  std::string query;       // without '?'
  std::string body;        // request body head (bounded)
};

class HttpStacServer
{
  public:
    HttpStacServer();
    ~HttpStacServer();

    HttpStacServer( const HttpStacServer & ) = delete;
    HttpStacServer &operator=( const HttpStacServer & ) = delete;

    /// Base URL ("http://127.0.0.1:<port>").
    std::string url() const;
    int port() const { return mPort; }

    /// Registers (or replaces) an exact match on "<path>[?<query>]"; a route
    /// without '?' matches any query. Unmatched requests answer 404.
    void setRoute( const std::string &pathAndQuery, const StacRoute &route );

    /// Ordered request log (method/path/query/body), capped.
    std::vector<StacRequestRecord> requests() const;

  private:
    void serveLoop();
    void handleConnection( SocketHandle client );

    SocketHandle mListener = kInvalidSocket;
    int mPort = 0;
    std::thread mThread;
    std::atomic<bool> mStop{ false };
    mutable std::mutex mMutex;
    std::map<std::string, StacRoute> mRoutes;
    std::vector<StacRequestRecord> mRequests;
};

} // namespace sicnu::geo::testsupport
