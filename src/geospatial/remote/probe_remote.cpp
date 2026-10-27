/***************************************************************************
  geospatial/remote/probe_remote.cpp
  Remote Sensing I/O Foundation 5.0 — bounded remote reachability probe.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Implementation notes (proven against GDAL 3.10+/curl 8.x on this stack):
  * The probe runs on the shared guarded transport (httpFetchStatus), NOT
    on a raw CPLHTTPFetch: the pinned GDAL follows redirects
    unconditionally inside one CPLHTTPFetch call, so a first-hop egress
    check made before the fetch never sees the redirect target (#1637
    sub-4 / #1456 transport note). httpFetchStatus under the process
    private-network policy installs the per-hop guard (and DNS pinning)
    automatically — every redirect hop is checked before dispatch.
  * Status code, filtered response headers, body and size-guard state
    arrive structured; reachability is derived from the whole answer, not
    the status line alone (guarded builds may report 0).
  * The probe range goes through the structured range option and stays
    double-bounded by maxResponseBytes so even a range-ignoring origin
    cannot push more than maxProbeBytes into this process.
 ***************************************************************************/

#include "geospatial/remote/probe_remote.h"

#include "geospatial/remote/http_fetch.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/util/resource_uri.h"

#include <algorithm>
#include <cctype>

namespace sicnu::geo
{

namespace
{

bool containsIgnoreCase( const std::string &haystack, const char *needle )
{
  if ( haystack.empty() )
    return false;
  std::string lowered = haystack;
  std::transform( lowered.begin(), lowered.end(), lowered.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return lowered.find( needle ) != std::string::npos;
}

} // namespace

Json::Value RemoteProbeResult::toJson() const
{
  Json::Value json;
  json["reachable"] = reachable;
  json["http_status"] = httpStatus;
  json["accepts_ranges"] = acceptsRanges;
  json["has_size"] = hasSize;
  if ( hasSize )
    json["size_bytes"] = static_cast<Json::UInt64>( sizeBytes );
  json["content_type"] = contentType;
  json["effective_url"] = effectiveUrl;
  return json;
}

RemoteProbeResult probeRemote( const std::string &url, const RemoteProbeOptions &options )
{
  const ResourceUri uri = ResourceUri::parse( url );
  const bool remoteSpelling =
    uri.kind == ResourceKind::RemoteHttp ||
    ( uri.kind == ResourceKind::VsiRemote && !uri.remoteUrl().empty() );
  if ( !remoteSpelling )
  {
    Json::Value details;
    details["url"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "probeRemote: not a remote http(s) resource", details );
  }
  const std::string target =
    uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();

  if ( offline::enabled() )
    throw GeoError( ErrorCode::NetworkError, offline::refusalMessage( uri.display() ) );
  // Fail-closed fast path with the probe's own diagnostics; under the
  // process policy the guarded transport repeats the check on EVERY hop
  // (redirect targets included), which a raw CPLHTTPFetch cannot do.
  if ( offline::blockPrivateNetworks() && offline::isPrivateHost( target ) )
  {
    Json::Value details;
    details["url"] = uri.display();
    throw GeoError( ErrorCode::PermissionDenied,
                    "probeRemote: private / loopback / link-local hosts are blocked", details );
  }

  const std::uintmax_t probeBytes =
    options.maxProbeBytes == 0 ? 1 : std::min<std::uintmax_t>( options.maxProbeBytes, 1024 * 1024 );
  HttpFetchOptions fetchOptions;
  fetchOptions.timeoutSeconds = options.timeoutSeconds;
  fetchOptions.connectTimeoutSeconds = options.connectTimeoutSeconds;
  fetchOptions.maxRetries = options.maxRetries;
  fetchOptions.maxResponseBytes = probeBytes;
  fetchOptions.range = "bytes=0-" + std::to_string( probeBytes - 1 );

  // httpFetchStatus: transport failures (dispatch, timeout, per-hop policy
  // refusals) throw typed GeoErrors; HTTP error statuses come back
  // structured so the probe's own 404/410 vs >=400 mapping stays intact.
  const HttpFetchResult result = httpFetchStatus( target, fetchOptions );

  RemoteProbeResult probe;
  probe.httpStatus = result.httpStatus;
  // effectiveUrl stays empty: the result's displayUrl is the REQUESTED url
  // in redacted form, not the post-redirect target; the field is reserved
  // for transports that expose it. Redaction is applied by consumers via
  // ResourceUri::display().

  bool declaredLength = false;
  std::uintmax_t declaredLengthBytes = 0;
  bool rangeRewritten = false; // origin answered with Content-Range
  if ( const std::string value = result.headerValue( "content-range" ); !value.empty() )
  {
    rangeRewritten = true;
    probe.acceptsRanges = true; // a Content-Range answer is the strongest proof
    // "bytes 0-1023/4195070" → full size after '/'
    const std::size_t slash = value.rfind( '/' );
    if ( slash != std::string::npos )
    {
      try
      {
        declaredLengthBytes = std::stoull( value.substr( slash + 1 ) );
        declaredLength = true;
      }
      catch ( const std::exception & )
      {
      }
    }
  }
  if ( const std::string value = result.headerValue( "accept-ranges" ); !value.empty() && !rangeRewritten )
    probe.acceptsRanges = containsIgnoreCase( value, "bytes" );
  if ( !rangeRewritten )
  {
    if ( const std::string value = result.headerValue( "content-length" ); !value.empty() )
    {
      // Content-Range carries the authoritative full size; a plain
      // Content-Length on a ranged answer would only be the slice size.
      try
      {
        declaredLengthBytes = std::stoull( value );
        declaredLength = true;
      }
      catch ( const std::exception & )
      {
      }
    }
  }
  if ( const std::string value = result.headerValue( "content-type" ); !value.empty() )
    probe.contentType = value;

  const bool fetched = !result.body.empty();

  // Reachability: a body, an explicit status, or a declared length.
  probe.reachable = fetched || probe.httpStatus > 0 || declaredLength;
  if ( !probe.reachable )
    throw GeoError( ErrorCode::NetworkError, "probeRemote: no usable answer from origin: " + uri.display() );
  // 404/410 must be tested BEFORE the generic >=400 branch, or the
  // documented NotFound mapping is unreachable.
  if ( probe.httpStatus == 404 || probe.httpStatus == 410 )
    throw GeoError( ErrorCode::NotFound, "probeRemote: remote resource is gone: " + uri.display() );
  if ( probe.httpStatus >= 400 )
  {
    Json::Value details;
    details["status"] = probe.httpStatus;
    throw GeoError( ErrorCode::NetworkError, "probeRemote: remote origin reported an error: " + uri.display(), details );
  }
  if ( !fetched && declaredLength && declaredLengthBytes == 0 )
  {
    // No status code exposed by this build, but an empty declared
    // length with no body is an origin error (5xx) or an empty object —
    // never a usable asset.
    throw GeoError( ErrorCode::NetworkError, "probeRemote: remote origin reported an error: " + uri.display() );
  }

  // Range support comes from a rewritten Content-Range (a real 206) or the
  // explicit Accept-Ranges header — already parsed above. A truncated 200
  // from a range-ignoring origin must NOT be read as range support.
  if ( declaredLength )
  {
    probe.hasSize = true;
    probe.sizeBytes = declaredLengthBytes;
  }

  return probe;
}

} // namespace sicnu::geo
