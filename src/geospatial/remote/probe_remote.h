/***************************************************************************
  geospatial/remote/probe_remote.h
  Remote Sensing I/O Foundation 5.0 — bounded remote reachability probe
  (ADR 0139).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  The probe answers "reachable? size? range-capable?" with an explicit
  timeout and a byte-bounded fetch — never a content download. Requests go
  through GDAL's CPL HTTP layer (single network stack, ADR 0139); no
  credentials are ever accepted here or included in the report (response
  headers are filtered before returning).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_PROBE_REMOTE_H
#define SICNU_GEOSPATIAL_PROBE_REMOTE_H

#include "geospatial/common.h"

#include <json/json.h>

#include <cstdint>
#include <string>

namespace sicnu::geo
{

struct RemoteProbeOptions
{
    int timeoutSeconds = 10;          ///< whole-probe budget (CPL TIMEOUT)
    int connectTimeoutSeconds = 5;    ///< connection budget (CPL CONNECTTIMEOUT)
    int maxRetries = 1;               ///< CPL retry bound; 0 disables retries
    std::uintmax_t maxProbeBytes = 1; ///< probe fetch reads at most this many
                                      ///< bytes (a 1-byte ranged GET; 0 = HEAD-only)
};

struct RemoteProbeResult
{
    bool reachable = false;
    int httpStatus = 0;
    bool acceptsRanges = false;   ///< Accept-Ranges includes "bytes"
    bool hasSize = false;         ///< Content-Length parseable
    std::uintmax_t sizeBytes = 0;
    std::string contentType;      ///< declared media type ("" absent)
    std::string effectiveUrl;     ///< after redirects (redaction left to display())
    Json::Value toJson() const;
};

/// Probes a remote http(s) URL (or a /vsicurl/-spelled one). Throws
/// GeoError(InvalidArgument) for non-remote inputs,
/// GeoError(Timeout) when the budgets elapse, GeoError(NetworkError) when the
/// connection fails, and GeoError(NotFound) for a 404/410 origin.
RemoteProbeResult probeRemote( const std::string &url, const RemoteProbeOptions &options = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_PROBE_REMOTE_H
