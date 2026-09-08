/***************************************************************************
  geospatial/cog/cog_presets.h
  Geospatial I/O Foundation 4.0 — safe COG creation presets.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Presets are the ONLY sanctioned way to produce COGs through this layer.
  Categorical scientific products default to lossless — lossy (JPEG) is an
  explicit opt-in through the visualization preset.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_COG_PRESETS_H
#define SICNU_GEOSPATIAL_COG_PRESETS_H

#include "geospatial/common.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::geo
{

struct CogPresetResult
{
    std::vector<std::string> creationOptions; ///< COG driver creation options
    std::vector<std::string> warnings;        ///< non-fatal advisories (surfaced, never swallowed)
    Json::Value toJson() const;
};

/// BigTIFF policy: every preset uses BIGTIFF=IF_SAFER so >4GB outputs never
/// produce a silently invalid TIFF.
enum class CogPreset
{
    LosslessScientific, ///< DEFLATE + horizontal predictor; bit-exact pixels
    Visualization,      ///< JPEG/YCbCr — LOSSY, display products only
    Categorical,        ///< LZW, no predictor; class maps / QA rasters
    ContinuousFloat,    ///< float predictor (3); float32/64 scientific grids
    Sar                 ///< DEFLATE, no photometric; amplitude/intensity rasters
};

/// Builds the COG driver creation options for a preset.
/// expectedDtypeName: "Byte"/"UInt16"/"Float32"/... used to refuse lossy or
/// mismatched presets (e.g. Categorical on Float32 → GeoError(FidelityLoss)).
CogPresetResult cogPresetOptions( CogPreset preset, const std::string &expectedDtypeName );

CogPresetResult cogPresetOptionsForJson( const Json::Value &presetSpec, const std::string &expectedDtypeName );

const char *cogPresetName( CogPreset preset );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_COG_PRESETS_H
