/***************************************************************************
 * geospatial/remote/offline_gate.h — process-wide offline gate (goal D7)
 *
 * Lowest-layer home of the offline flag so every networking primitive in the
 * geospatial layer can consult it without pulling in Qt or sicnu_data.
 * Engaged by the CLI `--offline` flag / SICNU_OFFLINE environment variable
 * (deployment wiring lives above this layer). When engaged, httpFetch and
 * friends refuse with a typed GeoError instead of attempting network I/O,
 * and the GDAL cloud-/vsi* deny (applyGdalNetworkDeny) makes every direct
 * /vsicurl//s3/... open fail fast.
 ***************************************************************************/
#ifndef SICNU_GEOSPATIAL_REMOTE_OFFLINE_GATE_H
#define SICNU_GEOSPATIAL_REMOTE_OFFLINE_GATE_H

#include <string>

namespace sicnu::geo::offline {

/// Engages/disengages the gate. Idempotent. The flag is atomic so concurrent
/// readers (worker threads) are well-defined; typical use is once at startup.
void setEnabled( bool offline );

/// True when remote requests must be refused. Thread-safe (atomic load).
bool enabled();

/// SICNU_OFFLINE with the repo's env-flag semantics ("1"/"true"/"yes"/"on",
/// case-insensitive, trimmed).
bool enabledFromEnv();

/// True for network targets: http(s)/ftp(s) URLs and the network GDAL /vsi*
/// handler family (/vsicurl/, /vsis3/, ...). Local handlers (/vsimem/,
/// /vsizip/, /vsitar/, /vsigzip/) are not remote.
bool isRemoteTarget( const std::string &source );

/// Typed refusal text naming the source and the engaging flag.
std::string refusalMessage( const std::string &source );

/// True when hostOrUrl targets a loopback, RFC 1918, CGNAT, link-local,
/// cloud metadata (169.254.169.254 / metadata.google.internal), or
/// IPv4-mapped IPv6 (::ffff:127.0.0.1, ::ffff:169.254.169.254) address.
/// Accepts bare host/IP, [host]:port, or URLs (http(s)://..., /vsicurl/..., etc.).
bool isPrivateHost( const std::string &hostOrUrl );

/// R15-B (#1505 T3 / #1485): DNS-rebinding defence for a guarded transport.
/// Resolves the host of the http(s) @p url ONCE and validates EVERY returned
/// address with the same private-network predicate. On success returns an
/// empty string and sets @p curlResolveEntry to a libcurl CURLOPT_RESOLVE
/// entry ("host:port:addr[,addr...]") that pins the connection to exactly
/// the validated set — empty when the host is already a literal address
/// (nothing to re-resolve). Returns a refusal reason when any address is
/// private or the name does not resolve (fail closed: an unresolvable
/// answer here followed by a successful one in the transport is itself a
/// rebinding shape). Proxies are out of scope (the proxy resolves).
std::string resolvePinnedPublicAddresses( const std::string &url, std::string *curlResolveEntry );

/// GDAL-level backstop: CPL_VSIL_CURL_ALLOWED_EXTENSIONS is set to an
/// impossible extension so every network /vsi* open fails fast ("does not
/// exist", no packet leaves the machine) even on open paths that bypass this
/// layer. Idempotent. Must run single-threaded at startup (GDAL global config
/// is not synchronized).
void applyGdalNetworkDeny();

/// R15-B (R4-03): process-wide private-network policy. When engaged, every
/// httpFetch* without its own redirect guard gets the private-network guard
/// (host check + per-hop guard + DNS pinning), and the range cache treats
/// blockPrivateNetworks as on. Library default: OFF (callers and tests that
/// never engage it see no change). Entry points (the CLI) engage it through
/// network_policy::engageProcessPrivateNetworkBlocking(), which also
/// installs the GDAL-level hooks. Atomic; safe to read from any thread.
void setBlockPrivateNetworks( bool block );
bool blockPrivateNetworks();
/// SICNU_BLOCK_PRIVATE_NETWORKS with the env-flag semantics of
/// enabledFromEnv() — how a parent CLI hands the policy to its workers.
bool blockPrivateNetworksFromEnv();

/// Inverse of applyGdalNetworkDeny (tests re-enabling networking).
void clearGdalNetworkDeny();

} // namespace sicnu::geo::offline

#endif // SICNU_GEOSPATIAL_REMOTE_OFFLINE_GATE_H
