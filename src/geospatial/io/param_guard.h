/***************************************************************************
  geospatial/io/param_guard.h
  Geospatial I/O, COG & Interchange 11.0 — operator-boundary path guards.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  The io:* operators accept raw path strings from agent workflows, the CLI and
  the GUI. Below the operator layer every module assumed a validated path;
  the boundary itself did not check. These guards close that gap on the
  ResourceUri authority (parse classification, credential redaction, Windows
  long-path spelling) without re-implementing any of it:

  * source paths must at least classify; unclassifiable input is refused with
    the parser's reason instead of failing later inside a driver open.
  * target paths must classify AND stay offline-safe: writes to network
    resources are a typed refusal (this platform writes through the staged
    local-atomic pipeline; remote mirrors are the fabric layer's job).
  * error surfaces and logs carry display() — the credential-redacted form —
    never the raw string.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_PARAM_GUARD_H
#define SICNU_GEOSPATIAL_IO_PARAM_GUARD_H

#include "geospatial/common.h"
#include "geospatial/util/resource_uri.h"

namespace sicnu::geo::io
{

/// A path/URI parameter that passed the boundary check.
struct CheckedPath
{
    std::string raw;        ///< as given — the string GDAL opens
    std::string canonical;  ///< identity form (normalized separators, VSI spelling kept)
    std::string display;    ///< redacted human/log form
    bool isLocalPayload = false;
    bool isRemote = false;
    /// Windows "\\?\" long-path spelling for local payloads (identity on
    /// other platforms). Callers that hit MAX_PATH pressure may open with
    /// this; sidecar math stays on `raw`/`canonical`.
    std::string longPathSpelling;
};

/// Validates a read-side path parameter. Throws GeoError(InvalidArgument)
/// when the string cannot be classified (reason carried in details).
CheckedPath checkSourcePath( const std::string &raw );

/// Validates a write-side path parameter. Everything checkSourcePath refuses
/// is refused here, plus: remote kinds (GeoError(Unsupported), reason
/// "remote_write_offline_policy"), virtual dataset and STAC asset kinds
/// (they name read-only projections, not storage targets).
CheckedPath checkTargetPath( const std::string &raw );

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_PARAM_GUARD_H
