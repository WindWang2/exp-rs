/***************************************************************************
  geospatial/cog/cog_validator.h
  Geospatial I/O Foundation 4.0 — COG structural validation.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Structural checks (read-only, metadata-only — never a full pixel scan):
    * tiled layout with power-of-two block sizes (512 default)
    * overviews present down to ≈ one tile, and the overviews are tiled too
    * compression declared; float data should carry a predictor
    * nodata/metadata survivability report for fidelity audits
  Byte-order/IFD-layout conformance is validated implicitly by the COG driver
  at creation; this validator audits produced files structurally afterwards.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_COG_VALIDATOR_H
#define SICNU_GEOSPATIAL_COG_VALIDATOR_H

#include "geospatial/common.h"

#include <json/json.h>

#include <string>

namespace sicnu::geo
{

struct CogValidationReport
{
    bool isCog = false;         ///< overall verdict
    std::string path;
    Json::Value checks;         ///< array of {check, verdict, detail}
    Json::Value toJson() const;
};

/// Validates that path is a usable Cloud Optimized GeoTIFF.
CogValidationReport validateCog( const std::string &path );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_COG_VALIDATOR_H
