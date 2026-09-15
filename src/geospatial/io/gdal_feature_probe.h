/***************************************************************************
  geospatial/io/gdal_feature_probe.h
  Geospatial I/O, COG & Interchange 11.0 — runtime GDAL feature report.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  The repo gates GDAL version drift through util/gdal_compat.h (compile-time
  macros) and driver drift through runtime DCAP checks; nothing reported the
  SELECTED configuration in one place. This probe is that report: the GDAL
  release string, every compat macro's active state, and the presence/
  capability of the canonical interchange driver set. Read-only, bounded,
  redacted (driver names only — no paths, no env values).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_GDAL_FEATURE_PROBE_H
#define SICNU_GEOSPATIAL_IO_GDAL_FEATURE_PROBE_H

#include "geospatial/common.h"

namespace sicnu::geo::io
{

/// Build the feature report. Keys:
///   gdal_release          — GDALVersionInfo("RELEASE")
///   version_macros        — one entry per SICNU_GDAL_* macro (true/false)
///   drivers               — canonical interchange set: present, vector,
///                           create-capable (per runtime DCAP metadata)
Json::Value gdalFeatureReport();

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_GDAL_FEATURE_PROBE_H
