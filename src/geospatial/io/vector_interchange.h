/***************************************************************************
  geospatial/io/vector_interchange.h
  Geospatial I/O, COG & Interchange 11.0 — capability-gated vector interchange.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  io:convert_format used to route vectors by a hard-coded driver-name list —
  GeoParquet or CSV needed a code change, and an unavailable driver looked
  identical to an unbuildable one. This module replaces the list with the
  platform's capability authorities:

  * GDAL driver presence + DCAP_VECTOR + DCAP_CREATE decide whether a target
    driver can be WRITTEN here (fail-closed with a typed reason otherwise);
  * FormatRegistry decides whether the format is a CERTIFIED profile
    (round-trip pinned by tests) — informational, not a gate;
  * capability reports are JSON, redacted, and honest about what is absent.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_VECTOR_INTERCHANGE_H
#define SICNU_GEOSPATIAL_IO_VECTOR_INTERCHANGE_H

#include "geospatial/common.h"

#include <string>

namespace sicnu::geo::io
{

/// Refusal reason codes: "driver_missing", "not_vector", "not_create_capable".
struct VectorTargetCheck
{
    bool usable = false;
    bool certifiedProfile = false; ///< backed by a FormatRegistry vector profile
    /// Driver is ALSO raster-capable (DCAP_RASTER): netCDF/PDF/MBTiles/...
    /// Routing for such drivers must consult the input's kind.
    bool alsoRaster = false;
    std::string profileId;         ///< canonical profile id when certified
    std::string reasonCode;        ///< set when !usable
    std::string message;
    Json::Value toJson() const;
};

/// Whether `driver` (GDAL short name) can receive vector writes through the
/// staged VectorWriter pipeline in this process. Read-only capability query.
VectorTargetCheck checkVectorWriteTarget( const std::string &driver );

/// Full interchange report for the agent/CLI surface: for every vector
/// driver the registry knows (plus the canonical interchange set) — present?
/// create-capable? certified? Redacted, bounded.
Json::Value vectorInterchangeCapabilities();

/// True when `source` opens read-only as a RASTER dataset (header-only
/// detection; the handle is closed immediately, no pixel reads). Used to
/// route dual-capability drivers (netCDF, PDF, ...) by the input's kind.
bool inputOpensAsRaster( const std::string &source );

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_VECTOR_INTERCHANGE_H
