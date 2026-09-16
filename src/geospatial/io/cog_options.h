/***************************************************************************
  geospatial/io/cog_options.h
  Geospatial I/O, COG & Interchange 11.0 — explicit COG production options
  on top of the preset authority.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  cog_presets (Foundation 4.0) hard-pins BLOCKSIZE=512, OVERVIEWS=AUTO and
  NUM_THREADS=ALL_CPUS: safe defaults, no knobs. The COG driver reads
  creation options FIRST-MATCH-WINS, so appending a second "-co KEY=..."
  never overrides a preset value — production needs a real merge. This
  planner is that merge, explained:

  * explicit blocksize / overview policy / deterministic mode REPLACE the
    preset value in place (never appended), every key records where its
    value came from ("preset" | "override");
  * deterministic=true pins NUM_THREADS=1 and the DEFLATE level: byte-ident
    ical output for identical input *within one GDAL/libtiff build* (no
    cross-platform byte guarantee — libtiff internals may change);
  * arbitrary extra creation options are merged by the same replace-or-
    append rule with a warning per replaced key, so "explicit beats preset"
    is predictable;
  * legality (power-of-two blocksize, level range) is checked here, before
    any GDAL call; the COG validator remains the authority on whether the
    RESULT is a COG.

  NoData and alpha are deliberately NOT options here: they travel from the
  source dataset through the COG driver (declared-only policy); inventing
  nodata at production time would be a fidelity lie.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_COG_OPTIONS_H
#define SICNU_GEOSPATIAL_IO_COG_OPTIONS_H

#include "geospatial/cog/cog_presets.h"
#include "geospatial/common.h"

#include <string>
#include <vector>

namespace sicnu::geo::io
{

struct CogProductionOptions
{
    sicnu::geo::CogPreset preset = sicnu::geo::CogPreset::LosslessScientific;
    /// 0 = keep the preset blocksize. Otherwise power of two, 128..4096.
    int blocksize = 0;
    /// false → OVERVIEWS=NONE (no overviews inside the COG; the validator
    /// applies the small-image allowance or flags their absence).
    bool buildOverviews = true;
    /// Deterministic byte output: NUM_THREADS=1, DEFLATE level pinned.
    bool deterministic = false;
    /// DEFLATE level used when deterministic (1..9).
    int deflateLevel = 6;
    /// Caller creation options, merged replace-or-append AFTER the preset
    /// (each replaced key adds a warning).
    std::vector<std::string> extraCreationOptions;
    /// Expected source dtype name ("Byte"/"Float32"/...) for the preset's
    /// fidelity policy; forwarded to cogPresetOptions.
    std::string expectedDtypeName;
};

struct CogProductionPlan
{
    /// Full, merged creation-option list (each key appears exactly once).
    std::vector<std::string> creationOptions;
    std::vector<std::string> warnings;
    /// keys: preset, options[] {key, value, source, reason}, determinism
    /// {pinned, scope}, overviews {mode, levels_hint}
    Json::Value explanation;
    Json::Value toJson() const;
};

/// Builds the merged plan. Throws GeoError(InvalidArgument) for an illegal
/// blocksize or deflate level; GeoError(FidelityLoss) propagates from the
/// preset's dtype policy.
CogProductionPlan planCogProduction( const CogProductionOptions &options );

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_COG_OPTIONS_H
