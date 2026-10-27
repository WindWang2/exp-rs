/***************************************************************************
 * geospatial/remote/process_network_policy.h — R15-B (R4-03) process-wide
 * private-network policy for entry points (CLI / workers).
 *
 * engageProcessPrivateNetworkBlocking() turns the policy on for the whole
 * process and (once) installs the GDAL-level hooks that cover the remote
 * paths NOT owned by our own transport:
 *   - /vsicurl/ and /vsicurl_streaming/ opens/stats are re-routed through
 *     the guarded range cache (/vsirangecache/: host check, per-hop guard,
 *     DNS pinning); the /vsicurl? option form is refused;
 *   - /vsiwebhdfs/ refuses private hosts (host check; no pinning);
 *   - CPLHTTPFetch (GDAL HTTP/WMS-style drivers) refuses private hosts
 *     before dispatch (host check; redirects inside libcurl are not
 *     visible here). probeRemote runs on the guarded httpFetchStatus
 *     transport instead, so its redirect hops are checked per hop.
 * httpFetch* and the range cache read offline::blockPrivateNetworks()
 * directly. Library defaults are unchanged: nothing is installed unless an
 * entry point engages the policy.
 ***************************************************************************/
#ifndef SICNU_GEOSPATIAL_REMOTE_PROCESS_NETWORK_POLICY_H
#define SICNU_GEOSPATIAL_REMOTE_PROCESS_NETWORK_POLICY_H

namespace sicnu::geo::network_policy
{

/// Engage the policy (idempotent). Installs the GDAL hooks on first use;
/// must run before worker threads start using GDAL (startup wiring).
void engageProcessPrivateNetworkBlocking();

/// Disengage (hooks stay installed and pass through). Test seam.
void disengageProcessPrivateNetworkBlocking();

/// True while engaged (same as offline::blockPrivateNetworks()).
bool processPrivateNetworkBlockingEngaged();

} // namespace sicnu::geo::network_policy

#endif
