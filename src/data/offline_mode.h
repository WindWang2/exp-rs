/***************************************************************************
 * src/data/offline_mode.h — process-wide offline gate (goal D7)
 *
 * The offline classroom bundle (packaging/OFFLINE_BUNDLE.md) runs on lab
 * machines with no network. When the gate is engaged — by the CLI `--offline`
 * flag or the SICNU_OFFLINE environment variable — every remote input path
 * refuses with a typed diagnostic instead of attempting network I/O.
 * Startup makes zero network calls in either mode; this gate only governs
 * later remote opens (STAC, /vsicurl/ raster sources).
 ***************************************************************************/
#pragma once

#include <QString>

namespace sicnu::data::offline {

/// Engages/disengages the gate. Idempotent; not thread-safe (set once at
/// startup, before workers exist).
void setEnabled( bool offline );

/// True when remote opens must be refused.
bool enabled();

/// SICNU_OFFLINE with env_flag semantics ("1"/"true"/"yes"/"on",
/// case-insensitive and trimmed). Mirrors src/agent/env_flag.h so the flag
/// cannot drift between entry points.
bool enabledFromEnv();

/// True for sources that GDAL resolves over the network: http(s)/ftp(s) URLs
/// and the network /vsi* handler family (/vsicurl/, /vsis3/, ...). Local
/// handlers (/vsimem/, /vsizip/, /vsitar/, /vsigzip/) are NOT remote.
bool isRemoteTarget( const QString &source );

/// Typed refusal text for diagnostics and CLI errors. Names the source and
/// the flag so an operator knows both what was refused and why.
QString refusalMessage( const QString &source );

/// GDAL-level backstop: makes every network /vsi* source (vsicurl, s3, gs,
/// azure, hdfs, ...) fail fast — "does not exist", no packet leaves the
/// machine — even on open paths that bypass the pool. Verified against the
/// CPL_VSIL_CURL_ALLOWED_EXTENSIONS contract; local /vsi handlers (/vsimem/,
/// /vsizip/, ...) are unaffected. Idempotent; call after setEnabled( true ).
void applyGdalNetworkDeny();

/// Inverse of applyGdalNetworkDeny, for tests that re-enable networking in
/// the same process.
void clearGdalNetworkDeny();

} // namespace sicnu::data::offline
