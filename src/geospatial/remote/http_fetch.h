/***************************************************************************
  geospatial/remote/http_fetch.h
  Cloud-Native Geospatial I/O 7.0 — bounded HTTP fetch over the CPL network
  stack (ADR 0139: CPL is the single HTTP implementation of this layer).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One primitive shared by the remote source validator and the STAC client:

    * every request is double-bounded: a wall-clock budget (TIMEOUT /
      CONNECTTIMEOUT) and a response byte budget (CPL MAX_FILE_SIZE)
    * retries are a bounded count (CPL RETRIES), never an unbounded loop
    * responses are structured: status, filtered response headers, body
    * URLs are requested in canonical form and reported in the redacted
      display form — credentials never reach logs or reports
    * truncated bodies are reported truthfully (truncated flag / typed
      error for JSON) — a cut answer is never repackaged as a complete one

  Threading: synchronous, bounded, no shared global state. Callers with UI
  threads (GUI, agent tools) MUST invoke from a worker thread — the same
  contract as every other remote call in this layer.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_HTTP_FETCH_H
#define SICNU_GEOSPATIAL_HTTP_FETCH_H

#include "geospatial/common.h"

#include <json/json.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Default response byte budget: 8 MiB covers STAC pages and validator
/// probes; callers streaming real payloads must pass an explicit budget.
inline constexpr std::uintmax_t kDefaultHttpMaxResponseBytes = 8ull * 1024 * 1024;

struct HttpFetchOptions
{
    int timeoutSeconds = 15;        ///< whole-request budget (CPL TIMEOUT)
    int connectTimeoutSeconds = 5;  ///< connection budget (CPL CONNECTTIMEOUT)
    int maxRetries = 1;             ///< bounded retry count (CPL RETRIES)
    std::uintmax_t maxResponseBytes = kDefaultHttpMaxResponseBytes; ///< CPL MAX_FILE_SIZE
    /// Extra request headers, verbatim "Name: value" lines.
    std::vector<std::string> headers;
    /// Optional Range header value, e.g. "bytes=0-1023" (without the name).
    std::string range;
    /// Optional request body: when non-empty the request becomes a POST
    /// carrying exactly these bytes (Content-Type comes from headers).
    std::string postBody;
    /// Optional accepted media types for httpFetchJson() ("application/geo+json"
    /// style prefixes; a mismatch is a typed error, not a silent parse).
    std::vector<std::string> acceptContentTypes;
};

struct HttpFetchResult
{
    int httpStatus = 0;             ///< 0 when the transport did not report one
    bool reachedServer = false;     ///< an answer (any status) came back
    /// Response headers, lowercased names. First occurrence wins; the set is
    /// response headers only — request credentials are never echoed here.
    std::map<std::string, std::string> headers;
    std::vector<unsigned char> body;
    bool truncated = false;         ///< byte budget hit before the body ended
    /// The byte budget aborted the transfer (CPL "Maximum file size"
    /// guard). Headers may or may not have arrived; the body is partial.
    bool sizeGuardHit = false;
    std::string displayUrl;         ///< redacted form of the requested URL

    /// Case-insensitive header lookup (name must be lowercase).
    std::string headerValue( const std::string &lowerName ) const;

    Json::Value toJson() const;     ///< bounded report; never includes the body
};

/// Performs a bounded GET. Throws GeoError(InvalidArgument) for a non-remote
/// input, GeoError(Timeout) when the budget elapses, GeoError(NetworkError)
/// when the request cannot be dispatched or fails below HTTP, and
/// GeoError(NotFound) for a 404/410. HTTP >= 400 otherwise is a
/// GeoError(NetworkError) carrying the status; callers needing the raw status
/// of error answers use httpFetchStatus() instead.
HttpFetchResult httpFetch( const std::string &url, const HttpFetchOptions &options = {} );

/// Variant that does NOT throw on HTTP >= 400 (transport failures still
/// throw): lets revalidation code observe 304/412 shapes directly.
HttpFetchResult httpFetchStatus( const std::string &url, const HttpFetchOptions &options = {} );

/// Bounded GET + JSON parse. Media-type declaration is checked when the
/// origin declares one (strict when acceptContentTypes is non-empty).
/// Truncation, HTTP errors and parse failures are typed errors — a partial
/// JSON document is never returned as if complete.
Json::Value httpFetchJson( const std::string &url, const HttpFetchOptions &options = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_HTTP_FETCH_H
