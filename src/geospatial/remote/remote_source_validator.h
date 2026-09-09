/***************************************************************************
  geospatial/remote/remote_source_validator.h
  Cloud-Native Geospatial I/O 7.0 — remote source identity & validators
  (task A). RFC 7232 validator semantics over the bounded CPL fetch.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Model:
    * RemoteValidatorSet — the ETag / Last-Modified pair an origin declared,
      with weak/strong classification (RFC 7232 §2.3).
    * RemoteSourceIdentity — what a bounded probe knows about a remote
      resource: state (Fresh / Unknown / Offline), validator set, size,
      range support, timestamps. Reports carry the REDACTED display URL.
    * RemoteSourceValidator::probe() captures an identity;
      revalidate() asks whether the content changed since.

  Truthfulness contract:
    * A STRONG ETag equality proves unchanged content.
    * A WEAK ETag equality only proves semantically-equal representations —
      reported as Unchanged with an explicit "weak" note, never as a byte-
      level proof. A weak/Last-Modified-only mismatch IS a change.
    * With no validator at all, equality of size alone is Inconclusive —
      never dressed up as freshness.
    * Offline / timeout / HTTP failure are STATES + outcomes, not hidden
      success: revalidate() reports Inconclusive and the identity records
      what happened in lastError. Only caller-contract violations (a
      non-remote URL) throw.

  Threading: synchronous bounded calls without shared global state. GUI /
  agent callers MUST invoke from worker threads (same contract as the rest
  of the remote layer).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_REMOTE_SOURCE_VALIDATOR_H
#define SICNU_GEOSPATIAL_REMOTE_SOURCE_VALIDATOR_H

#include "geospatial/common.h"
#include "geospatial/remote/http_fetch.h"

#include <json/json.h>

#include <cstdint>
#include <string>

namespace sicnu::geo
{

/// ETag / Last-Modified validators an origin declared for a representation.
struct RemoteValidatorSet
{
    std::string etag;          ///< verbatim, including a "W/" weak prefix
    std::string lastModified;  ///< HTTP-date, verbatim

    bool hasEtag() const { return !etag.empty(); }
    /// Strong validator: an ETag without the "W/" prefix (RFC 7232 §2.3).
    bool hasStrongEtag() const;
    /// Weak validator: "W/"-prefixed ETag.
    bool hasWeakEtag() const;
    bool hasLastModified() const { return !lastModified.empty(); }
    bool hasAny() const { return hasEtag() || hasLastModified(); }

    /// RFC 7232 weak comparison: opaque-tag equality ignoring the W/ prefix.
    static bool etagWeakMatches( const std::string &a, const std::string &b );

    /// Classifies the pair: "strong_etag", "weak_etag", "last_modified",
    /// "weak_etag+last_modified", "none".
    std::string strength() const;

    Json::Value toJson() const;
    static RemoteValidatorSet fromJson( const Json::Value &json );
};

/// Liveness of the identity's knowledge about the remote resource.
enum class RemoteSourceState
{
    Unknown,  ///< never probed, probe refused, or the answer proved nothing
    Fresh,    ///< validator set captured/confirmed at lastCheckedAt
    Stale,    ///< a validator mismatch was observed and new metadata captured
    Offline   ///< last attempt could not reach the origin at all
};

const char *remoteSourceStateName( RemoteSourceState state );
RemoteSourceState remoteSourceStateFromName( const std::string &name ); ///< throws GeoError(InvalidArgument)

enum class RevalidationOutcome
{
    Unchanged,    ///< a validator proved (or weakly indicated) no change
    Changed,      ///< a validator mismatch proved the content changed
    Inconclusive  ///< nothing on the wire could prove either direction
};

const char *revalidationOutcomeName( RevalidationOutcome outcome );

struct RevalidationResult
{
    RevalidationOutcome outcome = RevalidationOutcome::Inconclusive;
    int httpStatus = 0;
    std::string checkedAt;   ///< ISO-8601 UTC
    /// Which signal decided: "etag_304", "etag_strong_equal", "etag_mismatch",
    /// "etag_weak_equal", "last_modified_equal", "last_modified_mismatch",
    /// "size_only_inconclusive", "offline", "http_error", "gone".
    std::string decidedBy;
    Json::Value toJson() const;
};

struct RemoteSourceIdentity
{
    std::string url;          ///< REDACTED display form (never raw credentials)
    RemoteSourceState state = RemoteSourceState::Unknown;
    RemoteValidatorSet validator;
    bool hasSize = false;
    std::uintmax_t sizeBytes = 0;
    bool acceptsRanges = false;
    std::string contentType;
    std::string probedAt;       ///< ISO-8601 UTC of the first successful probe
    std::string lastCheckedAt;  ///< ISO-8601 UTC of the last attempt
    std::string lastError;      ///< state-relevant transport/HTTP detail

    /// True when the current validator set can prove freshness on revalidation
    /// (a strong ETag). Weak validators only ever yield weak freshness.
    bool freshnessProvable() const { return validator.hasStrongEtag(); }

    Json::Value toJson() const;
    static RemoteSourceIdentity fromJson( const Json::Value &json );
};

struct RemoteValidatorOptions
{
    int timeoutSeconds = 10;        ///< whole-probe budget
    int connectTimeoutSeconds = 5;  ///< connection budget
    int maxRetries = 1;             ///< bounded CPL retries
    /// Ranged GET bound for the metadata fetch (headers ride any response);
    /// hard-capped at 1 MiB like probeRemote.
    std::uintmax_t probeBytes = 1024;
};

class RemoteSourceValidator
{
  public:
    /// Probes the resource and captures its identity. Network/HTTP failures
    /// are folded into the returned identity (state Offline/Unknown +
    /// lastError) — only a non-remote URL throws GeoError(InvalidArgument).
    static RemoteSourceValidator probe( const std::string &url,
                                        const RemoteValidatorOptions &options = {} );

    RemoteSourceValidator() = default;

    /// Re-arms a validator around an EXISTING identity (e.g. the one a cache
    /// entry stored). requestUrl is the canonical form used on the wire.
    static RemoteSourceValidator fromIdentity( const RemoteSourceIdentity &identity,
                                               const std::string &requestUrl );

    const RemoteSourceIdentity &identity() const { return mIdentity; }

    /// Asks the origin whether the content changed since the captured
    /// identity. Attempts a conditional GET (If-None-Match with weak
    /// comparison, else If-Modified-Since) and falls back to comparing the
    /// returned validator set. Never throws for transport failures — the
    /// outcome is Inconclusive and the identity records Offline/Unknown.
    /// A changed resource updates the identity (state Stale → metadata
    /// captured; a follow-up probe() re-arms freshness).
    RevalidationResult revalidate( const RemoteValidatorOptions &options = {} );

    /// Re-probes unconditionally and replaces the identity (fresh capture).
    RemoteSourceValidator refresh( const RemoteValidatorOptions &options = {} );

    /// True when the URL classifies as a remote http(s) resource.
    static bool isRemoteUrl( const std::string &url );

  private:
    explicit RemoteSourceValidator( RemoteSourceIdentity identity );
    void captureHeaders( const HttpFetchResult &fetch );
    RemoteSourceIdentity mIdentity;
    /// Canonical request URL (kept separate from the identity, which only
    /// ever carries the redacted display form — signed URLs must survive
    /// revalidation without leaking into reports).
    std::string mRequestUrl;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_REMOTE_SOURCE_VALIDATOR_H
