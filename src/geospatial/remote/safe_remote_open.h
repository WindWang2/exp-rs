/***************************************************************************
 * geospatial/remote/safe_remote_open.h — the single sanctioned entry for
 * opening a REMOTE raster asset (#1765 / RIO-4 + #1803 RIO-6).
 *
 * Every remote COG/GeoTIFF open must reach the network ONLY through the
 * range cache's guarded transport (per-hop egress policy, redirect guard,
 * DNS pinning — #1456/#1485/#1545). A raw "/vsicurl/<href>" handed to
 * GDALOpenEx bypasses ALL of it (CPLHTTPFetch follows redirects
 * unconditionally and consults its own resolver), which is exactly the hole
 * the STAC browser dialog had. This header is the stable backend API every
 * caller (dialogs, RemoteDatasetPool, CLI) uses instead:
 *
 *     const auto r = geo::remote::safeRemoteRasterPath( href );
 *     if ( !r ) { surface r.code() + r.message; }
 *     else      GDALOpenEx( r.path.c_str(), ... );
 *
 * Contract (fail-closed at every step, no silent fallbacks):
 *   1. scheme gate  — http/https only ("/vsicurl/" spellings are accepted
 *                     and unwrapped); anything else is a typed refusal.
 *   2. offline gate — offline mode refuses before any socket could open.
 *   3. private-net  — loopback / RFC 1918 / link-local / cloud-metadata
 *      deny-by-default (literal IPs and well-known private NAMES; no DNS
 *      needed, so the refusal itself never touches the network). Deeper
 *      defenses (per-hop redirect policy, DNS rebinding pin) are enforced
 *      by the range cache transport on every actual fetch.
 *   4. cache gate   — the range cache must be installed; the returned path
 *                     is its VSI prefix path. When the cache is not
 *                     installed this REFUSES (CacheUnavailable) instead of
 *                     degrading to an unguarded /vsicurl/ open.
 *
 * Qt-free; typed statuses, no exceptions for flow control.
 ***************************************************************************/
#ifndef SICNU_GEOSPATIAL_REMOTE_SAFE_REMOTE_OPEN_H
#define SICNU_GEOSPATIAL_REMOTE_SAFE_REMOTE_OPEN_H

#include <cstdint>
#include <string>

namespace sicnu::geo::remote
{

enum class SafeOpenStatus : std::uint8_t
{
  Ok = 0,
  SchemeRefused,     ///< not an http(s) remote URL (after /vsicurl/ unwrap)
  OfflineRefused,    ///< process offline mode engaged
  PrivateTarget,     ///< loopback / private / link-local / cloud-metadata
  CacheUnavailable   ///< range cache not installed — no guarded transport
};

struct SafeRemotePathResult
{
  SafeOpenStatus status = SafeOpenStatus::SchemeRefused;
  /// The guarded open path (range-cache VSI prefix + URL); empty on refusal.
  std::string path;
  /// Human-readable refusal reason (typed, actionable, no secrets).
  std::string message;

  bool ok() const { return status == SafeOpenStatus::Ok; }
  /// Stable machine code for logs/telemetry ("remote.open.*" vocabulary).
  const char *code() const;
};

/// Maps @p urlOrVsicurl (bare https URL or "/vsicurl/<url>" spelling) to
/// the guarded open path, or to a typed refusal. Never throws, never
/// performs network I/O of its own (the guarded transport does the
/// fetching, per request, under the egress policy).
SafeRemotePathResult safeRemoteRasterPath( const std::string &urlOrVsicurl );

/// Stable name of a status (tests / diagnostics).
const char *safeOpenStatusName( SafeOpenStatus status );

} // namespace sicnu::geo::remote

#endif // SICNU_GEOSPATIAL_REMOTE_SAFE_REMOTE_OPEN_H
