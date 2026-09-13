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

/// Engages/disengages the gate. Idempotent; set once at startup.
void setEnabled( bool offline );

/// True when remote requests must be refused.
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

/// GDAL-level backstop: CPL_VSIL_CURL_ALLOWED_EXTENSIONS is set to an
/// impossible extension so every network /vsi* open fails fast ("does not
/// exist", no packet leaves the machine) even on open paths that bypass this
/// layer. Idempotent.
void applyGdalNetworkDeny();

/// Inverse of applyGdalNetworkDeny (tests re-enabling networking).
void clearGdalNetworkDeny();

} // namespace sicnu::geo::offline

#endif // SICNU_GEOSPATIAL_REMOTE_OFFLINE_GATE_H
