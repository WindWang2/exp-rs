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
#include <vector>

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

/// Outcome of resolveForPinning().
enum class HostPinStatus
{
    /// The host is an IP literal (or a loopback/metadata NAME): there is
    /// nothing to pin because isPrivateHost() already judged the literal.
    NoResolutionNeeded,
    /// Every resolved address passed the egress policy; @p entries carries
    /// the CURLOPT_RESOLVE entry (see resolveForPinning).
    Ok,
    /// At least one resolved address is private/loopback/metadata: refuse.
    PrivateAddress,
    /// Resolution failed or produced nothing usable. Fail CLOSED — a host
    /// that cannot be resolved to a proven-public address must not be
    /// fetched (#1485). The old isPrivateHost() path silently returned
    /// "public" for exactly this case.
    Unresolved,
};

/// Resolves @p host and returns the CURLOPT_RESOLVE entry that pins libcurl
/// to the addresses the egress policy just accepted. Pinning closes the
/// DNS-rebinding window between the policy check and libcurl's own, separate
/// resolution: without it a short-TTL record (or an attacker-controlled
/// resolver) can answer the check with a public address and the connect with
/// 169.254.169.254 (#1485).
///
/// @p host must be a BARE host — no scheme, brackets, port or path (httpFetch
/// passes the parsed ResourceUri host). @p port is the numeric port the
/// transport will actually connect to; the entry is keyed by host+port, so a
/// mismatched port is silently ignored by libcurl.
///
/// On Ok, @p entries receives exactly one entry of the form
/// "host:port:addr[,addr]..." (libcurl's documented CURLOPT_RESOLVE syntax,
/// verified against curl 8.21 lib/dnscache.c Curl_loadhostpairs). The caller
/// must keep the strings alive until the transfer is set up and free the
/// slist afterwards.
HostPinStatus resolveForPinning( const std::string &host, int port,
                                 std::vector<std::string> &entries,
                                 std::string &errorMessage );

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
