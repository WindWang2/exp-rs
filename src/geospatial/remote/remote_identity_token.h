/***************************************************************************
  geospatial/remote/remote_identity_token.h
  Cloud-Native Geospatial Data Fabric 8.0 — fail-closed remote identity token
  for execution-cache reuse (execution identity bridge, task B).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract (fail-closed by design):
  * `remoteIdentityToken()` probes a remote http(s) resource (bounded, never
    a content download — the probe fetches at most `probeBytes`, default
    1 KiB) and derives a STABLE identity token:
        "ri1:" + hex(SHA-256(canonical identity basis))
  * The token is non-empty ONLY when the resource proves byte-level
    freshness: a STRONG ETag (RFC 7232 §2.3). Weak ETags, Last-Modified-only
    and size-only origins return "" — the input is then uncacheable, never
    "probably the same".
  * The token CHANGES when the content identity changes (the strong ETag is
    part of the basis) and is otherwise stable across processes/sessions.
  * Credential safety: the basis carries the canonical URL with
    credential-shaped query values REMOVED (ResourceUri redaction rules) —
    re-issuing a signed URL therefore produces the SAME token for the same
    content (no false invalidation), and no credential ever reaches the
    token, logs or reports.
  * Token reuse is a caching decision, not correctness: callers must treat
    "" as "cannot identify → do not reuse".
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_REMOTE_IDENTITY_TOKEN_H
#define SICNU_GEOSPATIAL_REMOTE_IDENTITY_TOKEN_H

#include "geospatial/common.h"
#include "geospatial/remote/remote_source_validator.h"

#include <string>

namespace sicnu::geo
{

/// Prefix of every identity token produced here ("ri1:v1:<64 hex chars>").
inline constexpr const char *kRemoteIdentityTokenPrefix = "ri1";

struct RemoteIdentityTokenOptions
{
    int timeoutSeconds = 10;
    int connectTimeoutSeconds = 5;
    int maxRetries = 1;
    /// Bounded metadata fetch (never a content download); hard-capped at the
    /// probe layer's 1 MiB ceiling.
    std::uintmax_t probeBytes = 1024;
};

/// Probes @p url and derives the fail-closed identity token ("" when the
/// resource cannot be identified with byte-level freshness). Network/offline
/// failures are folded into "" — the caller only sees "unidentifiable".
/// Throws GeoError(InvalidArgument) only for a non-remote URL (caller
/// contract violation, mirroring RemoteSourceValidator::probe).
std::string remoteIdentityToken( const std::string &url,
                                 const RemoteIdentityTokenOptions &options = {} );

/// Derives the token from an ALREADY-CAPTURED identity (no extra network
/// probe) — the single-probe form for callers that just probed the resource.
/// Same fail-closed contract as remoteIdentityToken().
std::string remoteIdentityTokenFromIdentity( const std::string &url,
                                             const RemoteSourceIdentity &identity );

/// The identity basis behind a token (canonical URL sans credential-shaped
/// query values + validator + size), exposed for diagnostics/tests. Never
/// contains credentials. Returns "" when the identity is not provable.
std::string remoteIdentityBasis( const std::string &url,
                                 const RemoteIdentityTokenOptions &options = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_REMOTE_IDENTITY_TOKEN_H
