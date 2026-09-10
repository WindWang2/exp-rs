/***************************************************************************
  geospatial/remote/remote_source_validator.cpp — identity & revalidation.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/remote/remote_source_validator.h"

#include "geospatial/util/resource_uri.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <sstream>

namespace sicnu::geo
{

namespace
{

std::string nowIso8601Utc()
{
  const auto now = std::chrono::system_clock::now();
  const auto itt = std::chrono::system_clock::to_time_t( now );
  std::tm tmBuf{};
  // gmtime returns a shared static buffer — never safe across threads.
#ifdef _WIN32
  gmtime_s( &tmBuf, &itt );
#else
  gmtime_r( &itt, &tmBuf );
#endif
  std::ostringstream ss;
  ss << std::put_time( &tmBuf, "%Y-%m-%dT%H:%M:%SZ" );
  return ss.str();
}

std::string stripWeakPrefix( const std::string &etag )
{
  const std::string prefix = "W/";
  if ( etag.size() >= prefix.size() && etag.compare( 0, prefix.size(), prefix ) == 0 )
    return etag.substr( prefix.size() );
  return etag;
}

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

bool containsIgnoreCase( const std::string &haystack, const char *needle )
{
  return !haystack.empty() && lowerAscii( haystack ).find( needle ) != std::string::npos;
}

struct CapturedHeaders
{
  std::string etag;
  std::string lastModified;
  std::string contentType;
  bool hasContentLength = false;
  std::uintmax_t contentLength = 0;
  bool hasContentRange = false;
  std::uintmax_t rangeTotal = 0;
  bool acceptsRanges = false;
};

CapturedHeaders parseEntityHeaders( const HttpFetchResult &fetch )
{
  CapturedHeaders captured;
  captured.etag = fetch.headerValue( "etag" );
  captured.lastModified = fetch.headerValue( "last-modified" );
  captured.contentType = fetch.headerValue( "content-type" );
  captured.acceptsRanges = containsIgnoreCase( fetch.headerValue( "accept-ranges" ), "bytes" );

  const std::string contentRange = fetch.headerValue( "content-range" );
  if ( !contentRange.empty() )
  {
    // "bytes 0-1023/4195070" → declared total after '/'; "*" means unknown.
    const std::size_t slash = contentRange.rfind( '/' );
    if ( slash != std::string::npos )
    {
      const std::string total = contentRange.substr( slash + 1 );
      if ( !total.empty() && total != "*" )
      {
        try
        {
          captured.rangeTotal = std::stoull( total );
          captured.hasContentRange = true;
        }
        catch ( const std::exception & )
        {
        }
      }
    }
  }
  // Content-Range carries the authoritative full size; a plain
  // Content-Length on a ranged answer would only be the slice size
  // (same rule probe_remote 5.0 established).
  const std::string lengthText = fetch.headerValue( "content-length" );
  if ( !captured.hasContentRange && !lengthText.empty() )
  {
    try
    {
      captured.contentLength = std::stoull( lengthText );
      captured.hasContentLength = true;
    }
    catch ( const std::exception & )
    {
    }
  }
  return captured;
}

/// Applies captured response headers onto the identity (sizes, range support,
/// content type, validator set). Existing non-empty validator entries are
/// only replaced when the answer declares a replacement — a 304-shaped
/// answer carries no entity headers and must not wipe the stored set.
void applyCaptured( RemoteSourceIdentity &identity, const CapturedHeaders &captured,
                    bool answerCarriesEntityHeaders )
{
  if ( answerCarriesEntityHeaders )
  {
    identity.validator.etag = captured.etag;
    identity.validator.lastModified = captured.lastModified;
    if ( captured.hasContentRange )
    {
      identity.hasSize = true;
      identity.sizeBytes = captured.rangeTotal;
    }
    else if ( captured.hasContentLength )
    {
      identity.hasSize = true;
      identity.sizeBytes = captured.contentLength;
    }
  }
  if ( captured.acceptsRanges )
    identity.acceptsRanges = true;
  if ( !captured.contentType.empty() )
    identity.contentType = captured.contentType;
}

} // namespace

bool RemoteValidatorSet::hasStrongEtag() const
{
  return !etag.empty() && etag.rfind( "W/", 0 ) != 0;
}

bool RemoteValidatorSet::hasWeakEtag() const
{
  return !etag.empty() && etag.rfind( "W/", 0 ) == 0;
}

bool RemoteValidatorSet::etagWeakMatches( const std::string &a, const std::string &b )
{
  if ( a.empty() || b.empty() )
    return false;
  return stripWeakPrefix( a ) == stripWeakPrefix( b );
}

std::string RemoteValidatorSet::strength() const
{
  std::string result;
  if ( hasStrongEtag() )
    result = "strong_etag";
  else if ( hasWeakEtag() )
    result = "weak_etag";
  if ( hasLastModified() )
  {
    if ( !result.empty() )
      result += "+last_modified";
    else
      result = "last_modified";
  }
  return result.empty() ? "none" : result;
}

Json::Value RemoteValidatorSet::toJson() const
{
  Json::Value json;
  if ( !etag.empty() )
    json["etag"] = etag;
  if ( !lastModified.empty() )
    json["last_modified"] = lastModified;
  json["strength"] = strength();
  return json;
}

RemoteValidatorSet RemoteValidatorSet::fromJson( const Json::Value &json )
{
  RemoteValidatorSet set;
  if ( json.isObject() )
  {
    set.etag = json["etag"].isString() ? json["etag"].asString() : std::string();
    set.lastModified = json["last_modified"].isString() ? json["last_modified"].asString() : std::string();
  }
  return set;
}

const char *remoteSourceStateName( RemoteSourceState state )
{
  switch ( state )
  {
    case RemoteSourceState::Unknown: return "unknown";
    case RemoteSourceState::Fresh: return "fresh";
    case RemoteSourceState::Stale: return "stale";
    case RemoteSourceState::Offline: return "offline";
  }
  return "unknown";
}

RemoteSourceState remoteSourceStateFromName( const std::string &name )
{
  const std::string lower = lowerAscii( name );
  if ( lower == "unknown" ) return RemoteSourceState::Unknown;
  if ( lower == "fresh" ) return RemoteSourceState::Fresh;
  if ( lower == "stale" ) return RemoteSourceState::Stale;
  if ( lower == "offline" ) return RemoteSourceState::Offline;
  Json::Value details;
  details["name"] = name;
  throw GeoError( ErrorCode::InvalidArgument, "remoteSourceStateFromName: unknown state", details );
}

const char *revalidationOutcomeName( RevalidationOutcome outcome )
{
  switch ( outcome )
  {
    case RevalidationOutcome::Unchanged: return "unchanged";
    case RevalidationOutcome::Changed: return "changed";
    case RevalidationOutcome::Inconclusive: return "inconclusive";
  }
  return "inconclusive";
}

Json::Value RevalidationResult::toJson() const
{
  Json::Value json;
  json["outcome"] = revalidationOutcomeName( outcome );
  json["http_status"] = httpStatus;
  json["checked_at"] = checkedAt;
  json["decided_by"] = decidedBy;
  return json;
}

Json::Value RemoteSourceIdentity::toJson() const
{
  Json::Value json;
  json["url"] = url;
  json["state"] = remoteSourceStateName( state );
  json["validator"] = validator.toJson();
  if ( hasSize )
    json["size_bytes"] = static_cast<Json::UInt64>( sizeBytes );
  json["accepts_ranges"] = acceptsRanges;
  if ( !contentType.empty() )
    json["content_type"] = contentType;
  if ( !probedAt.empty() )
    json["probed_at"] = probedAt;
  if ( !lastCheckedAt.empty() )
    json["last_checked_at"] = lastCheckedAt;
  if ( !lastError.empty() )
    json["last_error"] = lastError;
  json["freshness_provable"] = freshnessProvable();
  return json;
}

RemoteSourceIdentity RemoteSourceIdentity::fromJson( const Json::Value &json )
{
  RemoteSourceIdentity identity;
  if ( !json.isObject() )
  {
    Json::Value details;
    details["reason"] = "not an object";
    throw GeoError( ErrorCode::InvalidMetadata, "RemoteSourceIdentity::fromJson: invalid document", details );
  }
  identity.url = json["url"].isString() ? json["url"].asString() : std::string();
  identity.state = json["state"].isString()
                     ? remoteSourceStateFromName( json["state"].asString() )
                     : RemoteSourceState::Unknown;
  identity.validator = RemoteValidatorSet::fromJson( json["validator"] );
  if ( json.isMember( "size_bytes" ) )
  {
    identity.hasSize = true;
    identity.sizeBytes = json["size_bytes"].asUInt64();
  }
  identity.acceptsRanges = json["accepts_ranges"].isBool() && json["accepts_ranges"].asBool();
  identity.contentType = json["content_type"].isString() ? json["content_type"].asString() : std::string();
  identity.probedAt = json["probed_at"].isString() ? json["probed_at"].asString() : std::string();
  identity.lastCheckedAt = json["last_checked_at"].isString() ? json["last_checked_at"].asString() : std::string();
  identity.lastError = json["last_error"].isString() ? json["last_error"].asString() : std::string();
  return identity;
}

RemoteSourceValidator::RemoteSourceValidator( RemoteSourceIdentity identity )
  : mIdentity( std::move( identity ) )
{
}

RemoteSourceValidator RemoteSourceValidator::fromIdentity( const RemoteSourceIdentity &identity,
                                                           const std::string &requestUrl )
{
  RemoteSourceValidator validator( identity );
  validator.mRequestUrl = requestUrl;
  return validator;
}

bool RemoteSourceValidator::isRemoteUrl( const std::string &url )
{
  const ResourceUri uri = ResourceUri::parse( url );
  return uri.kind == ResourceKind::RemoteHttp ||
         ( uri.kind == ResourceKind::VsiRemote && !uri.remoteUrl().empty() );
}

void RemoteSourceValidator::captureHeaders( const HttpFetchResult &fetch )
{
  const CapturedHeaders captured = parseEntityHeaders( fetch );
  const bool entityHeaders =
    !captured.etag.empty() || !captured.lastModified.empty() || captured.hasContentRange ||
    captured.hasContentLength;
  applyCaptured( mIdentity, captured, entityHeaders );
}

RemoteSourceValidator RemoteSourceValidator::probe( const std::string &url,
                                                    const RemoteValidatorOptions &options )
{
  const ResourceUri uri = ResourceUri::parse( url );
  const bool remoteSpelling =
    uri.kind == ResourceKind::RemoteHttp ||
    ( uri.kind == ResourceKind::VsiRemote && !uri.remoteUrl().empty() );
  if ( !remoteSpelling )
  {
    Json::Value details;
    details["url"] = uri.display();
    throw GeoError( ErrorCode::InvalidArgument, "RemoteSourceValidator: not a remote http(s) resource", details );
  }

  RemoteSourceIdentity identity;
  identity.url = uri.display();
  identity.lastCheckedAt = nowIso8601Utc();

  HttpFetchOptions fetchOptions;
  fetchOptions.timeoutSeconds = options.timeoutSeconds;
  fetchOptions.connectTimeoutSeconds = options.connectTimeoutSeconds;
  fetchOptions.maxRetries = options.maxRetries;
  const std::uintmax_t probeBytes =
    options.probeBytes == 0 ? 1 : std::min<std::uintmax_t>( options.probeBytes, 1024 * 1024 );
  fetchOptions.maxResponseBytes = probeBytes;
  fetchOptions.range = "bytes=0-" + std::to_string( probeBytes - 1 );

  RemoteSourceValidator validator;
  const std::string requestUrl =
    uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();
  try
  {
    HttpFetchResult fetch = httpFetchStatus( requestUrl, fetchOptions );
    if ( fetch.httpStatus == 404 || fetch.httpStatus == 410 )
    {
      identity.state = RemoteSourceState::Unknown;
      identity.lastError = "not_found (" + std::to_string( fetch.httpStatus ) + ")";
    }
    else if ( fetch.httpStatus >= 400 )
    {
      identity.state = RemoteSourceState::Unknown;
      identity.lastError = "http_error (" + std::to_string( fetch.httpStatus ) + ")";
    }
    else
    {
      // An entity-less, status-less answer is not a usable identity — the
      // transport answered with literally nothing (broken origin).
      const bool entityEvidence = !fetch.body.empty() || !fetch.headers.empty();
      if ( !entityEvidence )
      {
        identity.state = RemoteSourceState::Offline;
        identity.lastError = "empty answer from origin";
      }
      else
      {
        // A usable answer: capture what the origin declared. A truncated
        // body is fine here — the probe reads headers, never content.
        validator.mIdentity = identity;
        validator.mRequestUrl = requestUrl;
        validator.captureHeaders( fetch );
        validator.mIdentity.probedAt = identity.lastCheckedAt;
        if ( fetch.sizeGuardHit && !validator.mIdentity.validator.hasAny() )
        {
          // A range-ignoring oversized origin aborted before validators
          // could be captured: the resource exists, but nothing here can
          // prove freshness. Truthful state, not a fake Fresh.
          validator.mIdentity.state = RemoteSourceState::Unknown;
          validator.mIdentity.lastError = "probe budget reached before validators were captured";
        }
        else
        {
          validator.mIdentity.state = RemoteSourceState::Fresh;
        }
        return validator;
      }
    }
  }
  catch ( const GeoError &error )
  {
    identity.state = RemoteSourceState::Offline;
    identity.lastError = ( std::string( error.code() == ErrorCode::Timeout ? "timeout: " : "offline: " ) +
                           error.what() )
                           .substr( 0, 256 );
  }
  validator.mIdentity = std::move( identity );
  validator.mRequestUrl = requestUrl;
  return validator;
}

RevalidationResult RemoteSourceValidator::revalidate( const RemoteValidatorOptions &options )
{
  RevalidationResult result;
  result.checkedAt = nowIso8601Utc();
  mIdentity.lastCheckedAt = result.checkedAt;
  if ( mRequestUrl.empty() )
  {
    // An identity restored from JSON (no live request URL): a revalidation
    // needs a probed validator object.
    result.decidedBy = "no_prior_identity";
    return result;
  }

  HttpFetchOptions fetchOptions;
  fetchOptions.timeoutSeconds = options.timeoutSeconds;
  fetchOptions.connectTimeoutSeconds = options.connectTimeoutSeconds;
  fetchOptions.maxRetries = options.maxRetries;
  const std::uintmax_t probeBytes =
    options.probeBytes == 0 ? 1 : std::min<std::uintmax_t>( options.probeBytes, 1024 * 1024 );
  fetchOptions.maxResponseBytes = probeBytes;
  fetchOptions.range = "bytes=0-" + std::to_string( probeBytes - 1 );

  const std::string priorEtag = mIdentity.validator.etag;
  const std::string priorLastModified = mIdentity.validator.lastModified;
  const bool hadSize = mIdentity.hasSize;
  const std::uintmax_t priorSize = mIdentity.sizeBytes;

  // RFC 7232 §2.3: If-None-Match is compared weakly, so a weak validator is
  // still usable here; Last-Modified is the declared fallback.
  if ( !priorEtag.empty() )
    fetchOptions.headers.push_back( "If-None-Match: " + priorEtag );
  else if ( !priorLastModified.empty() )
    fetchOptions.headers.push_back( "If-Modified-Since: " + priorLastModified );

  HttpFetchResult fetch;
  try
  {
    fetch = httpFetchStatus( mRequestUrl, fetchOptions );
  }
  catch ( const GeoError &error )
  {
    // Transport failure: freshness is simply unprovable this round.
    mIdentity.state = RemoteSourceState::Offline;
    mIdentity.lastError = std::string( error.what() ).substr( 0, 256 );
    mIdentity.lastCheckedAt = result.checkedAt;
    result.decidedBy = "offline";
    return result;
  }
  result.httpStatus = fetch.httpStatus;
  mIdentity.lastCheckedAt = result.checkedAt;

  if ( fetch.httpStatus == 404 || fetch.httpStatus == 410 )
  {
    mIdentity.state = RemoteSourceState::Unknown;
    mIdentity.lastError = "not_found (" + std::to_string( fetch.httpStatus ) + ")";
    result.outcome = RevalidationOutcome::Changed;
    result.decidedBy = "gone";
    return result;
  }
  if ( fetch.httpStatus >= 400 )
  {
    mIdentity.state = RemoteSourceState::Unknown;
    mIdentity.lastError = "http_error (" + std::to_string( fetch.httpStatus ) + ")";
    result.outcome = RevalidationOutcome::Inconclusive;
    result.decidedBy = "http_error";
    return result;
  }

  const CapturedHeaders captured = parseEntityHeaders( fetch );
  const bool entityHeaders =
    !captured.etag.empty() || !captured.lastModified.empty() || captured.hasContentRange ||
    captured.hasContentLength;

  // 304 shape: explicit status, or an entity-less answer to a conditional
  // request (no body, no entity headers, no declared length).
  if ( fetch.httpStatus == 304 ||
       ( fetch.httpStatus == 0 && fetch.body.empty() && !entityHeaders ) )
  {
    result.outcome = RevalidationOutcome::Unchanged;
    result.decidedBy = "etag_304";
    mIdentity.state = RemoteSourceState::Fresh;
    return result;
  }

  if ( entityHeaders )
    applyCaptured( mIdentity, captured, true );

  // 200-shaped answer: compare validators, strongest evidence first.
  if ( !priorEtag.empty() && !mIdentity.validator.etag.empty() )
  {
    if ( priorEtag == mIdentity.validator.etag )
    {
      result.outcome = RevalidationOutcome::Unchanged;
      result.decidedBy = mIdentity.validator.hasStrongEtag() ? "etag_strong_equal" : "etag_weak_equal";
    }
    else if ( RemoteValidatorSet::etagWeakMatches( priorEtag, mIdentity.validator.etag ) )
    {
      result.outcome = RevalidationOutcome::Unchanged;
      result.decidedBy = "etag_weak_equal";
    }
    else
    {
      result.outcome = RevalidationOutcome::Changed;
      result.decidedBy = "etag_mismatch";
    }
  }
  else if ( !priorLastModified.empty() && !mIdentity.validator.lastModified.empty() )
  {
    if ( priorLastModified == mIdentity.validator.lastModified )
    {
      if ( hadSize && mIdentity.hasSize && priorSize != mIdentity.sizeBytes )
      {
        result.outcome = RevalidationOutcome::Changed;
        result.decidedBy = "size_mismatch";
      }
      else
      {
        result.outcome = RevalidationOutcome::Unchanged;
        result.decidedBy = "last_modified_equal";
      }
    }
    else
    {
      result.outcome = RevalidationOutcome::Changed;
      result.decidedBy = "last_modified_mismatch";
    }
  }
  else if ( hadSize && mIdentity.hasSize )
  {
    // Size equality alone never proves freshness (content may have changed
    // in place) — truthfulness beats a comfortable answer.
    result.outcome = priorSize == mIdentity.sizeBytes ? RevalidationOutcome::Inconclusive
                                                      : RevalidationOutcome::Changed;
    result.decidedBy = priorSize == mIdentity.sizeBytes ? "size_only_inconclusive" : "size_mismatch";
  }
  else
  {
    result.outcome = RevalidationOutcome::Inconclusive;
    result.decidedBy = "no_comparable_validator";
  }

  // Truthful states: a proven change is Stale (new metadata captured), a
  // proven match is Fresh, and an inconclusive round proves nothing.
  switch ( result.outcome )
  {
    case RevalidationOutcome::Changed: mIdentity.state = RemoteSourceState::Stale; break;
    case RevalidationOutcome::Unchanged: mIdentity.state = RemoteSourceState::Fresh; break;
    case RevalidationOutcome::Inconclusive: mIdentity.state = RemoteSourceState::Unknown; break;
  }
  return result;
}

RemoteSourceValidator RemoteSourceValidator::refresh( const RemoteValidatorOptions &options )
{
  // Re-arms through probe(): a fresh capture replaces the whole identity.
  // mRequestUrl (the canonical form) survives via the probe URL itself.
  const std::string requestUrl = mRequestUrl.empty() ? mIdentity.url : mRequestUrl;
  const ResourceUri uri = ResourceUri::parse( requestUrl );
  const std::string target =
    uri.kind == ResourceKind::RemoteHttp ? uri.canonical() : uri.remoteUrl();
  RemoteSourceValidator refreshed = probe( target, options );
  if ( refreshed.mRequestUrl.empty() )
    refreshed.mRequestUrl = target;
  return refreshed;
}

} // namespace sicnu::geo
