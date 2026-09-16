/***************************************************************************
  geospatial/io/metadata_patch.h
  Geospatial I/O, COG & Interchange 11.0 — validated metadata write-back.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Canonical metadata is written at CREATE time (RasterWriter) and read by
  inspectRaster; between those two sat no way to correct scale/offset/unit/
  band role/wavelength/product stamps on an EXISTING dataset. This module is
  that seam, with the failure semantics the platform requires:

  * whitelist only: every patchable field is named here; anything else is an
    InvalidArgument refusal BEFORE the dataset is opened for update;
  * validate-then-apply: all patches are parsed/checked first (numeric fields
    must parse, ISO-8601 instants must parse, band indices must exist), so a
    bad batch never half-applies;
  * update-capability gate: drivers without update support (or read-only
    media) fail with a typed error — never a silent no-op, never a staged
    full rewrite of a possibly huge file (documented crash-window trade-off,
    DECISIONS D-004);
  * read-back verification: every applied field is re-read through a fresh
    read-only open and must round-trip exactly;
  * provenance continuity: when a finalize manifest sidecar exists, its
    digest is recomputed and a patch history entry appended — verifyDataset
    keeps working after a patch.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_METADATA_PATCH_H
#define SICNU_GEOSPATIAL_IO_METADATA_PATCH_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <string>
#include <vector>

namespace sicnu::geo::io
{

struct MetadataPatch
{
    /// 0 = dataset-scoped stamp; 1-based band index for band fields.
    int band = 0;
    /// Band-scoped fields:
    ///   scale, offset, unit, nodata, role, wavelength_nm, fwhm_nm,
    ///   color_interpretation
    /// Dataset-scoped fields:
    ///   sensor, platform, product_id, processing_level, acquisition_time
    std::string field;
    /// Value text; numeric fields parse strictly (scale/offset/nodata/
    /// wavelength_nm/fwhm_nm).
    std::string value;
};

struct MetadataPatchReport
{
    bool applied = false;
    std::string displayPath;
    std::vector<std::string> appliedFields;
    std::vector<std::string> warnings;
    bool manifestUpdated = false; ///< finalize manifest digest refreshed
    Json::Value toJson() const;
};

/// Applies the patches to `path`. Throws GeoError(InvalidArgument) for any
/// invalid patch (before opening for update), GeoError(OpenFailed) when the
/// driver cannot update, GeoError(WriteFailed) when apply or read-back
/// verification fails (dataset left closed; already-applied fields stay —
/// the report is only returned on FULL success).
MetadataPatchReport applyMetadataPatch( const std::string &path, const std::vector<MetadataPatch> &patches );

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_METADATA_PATCH_H
