/***************************************************************************
  geospatial/stac/stac_mapper.h
  Geospatial I/O Foundation 4.0 — STAC interoperability.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Read: STAC Item JSON → StacItem view → CanonicalMetadata enrichment
        (properties.datetime → acquisition, eo:cloud_cover → cloud cover,
         eo:bands → roles/wavelengths, proj:epsg → CRS, sar:* → polarizations).
  Write: CanonicalMetadata → STAC-compatible Item JSON (properties with the
         common + eo + projection extension vocabulary, bbox/geometry from the
         canonical extent, assets from the declared href).
  Lineage/provenance stays with the governance APIs; this module produces and
  consumes metadata only.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_STAC_MAPPER_H
#define SICNU_GEOSPATIAL_STAC_MAPPER_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::geo
{

struct StacAsset
{
    std::string href;
    std::string title;
    std::string mediaType;               ///< "image/tiff", "image/jp2", ...
    std::vector<std::string> roles;      ///< "data", "visual", "thumbnail", ...
    Json::Value toJson() const;
};

struct StacItem
{
    std::string id;
    std::string stacVersion;             ///< "1.0.0" typically
    std::string datetime;                ///< ISO-8601; STAC requires it (or start/end range)
    std::string startDatetime;
    std::string endDatetime;

    // 8.0 — normalized UTC instants, derived at parse time and NEVER emitted
    // by toJson() (the wire form stays the origin's verbatim RFC 3339).
    // Mixed offsets ("+02:00") normalize to Z; offset-less datetimes assume
    // UTC per the STAC spec and flag it; unparseable datetimes leave the UTC
    // form empty — never a guessed time.
    std::string datetimeUtc;
    std::string startDatetimeUtc;
    std::string endDatetimeUtc;
    bool datetimeNormalized = false;     ///< true when a UTC form differs from its verbatim source
    bool datetimeAssumedUtc = false;     ///< true when the effective datetime declared no offset

    std::string platform;
    std::string constellation;
    std::vector<std::string> instruments;
    std::string processingLevel;
    std::string modality;                ///< optical / sar / dem / ... when declared
    bool hasCloudCover = false;
    double cloudCover = 0.0;
    bool hasGsd = false;
    double gsd = 0.0;
    std::vector<std::string> polarizations;   ///< sar:polarizations
    std::string epsg;                          ///< proj:epsg ("" when absent)
    std::vector<double> bbox;                  ///< 4 or 6 values
    Json::Value geometry;                      ///< GeoJSON geometry object
    std::map<std::string, StacAsset> assets;
    Json::Value raw;                            ///< the full Item document

    /// 9.0 M4 — delivery provenance, set by the surface that produced the
    /// item (the search page URL with the rel="self" link preferred, or the
    /// absolute local path for parseFromFile). Relative asset hrefs resolve
    /// against it; it is PROVENANCE, never wire data — toJson() omits it.
    std::string sourceHref;

    /// Parses a STAC Item document. Structural violations (missing id,
    /// missing datetime, missing assets) throw GeoError(InvalidArgument).
    static StacItem parse( const Json::Value &item );
    static StacItem parseText( const std::string &jsonText );
    /// 9.0 M4: parses an Item document from a local file and stamps
    /// `sourceHref` with the file's absolute path so relative asset hrefs
    /// resolve (the local-file STAC pattern).
    static StacItem parseFromFile( const std::string &path );
    Json::Value toJson() const;                 ///< STAC-compatible Item document
};

/// Projects a parsed STAC item onto canonical product metadata. The primary
/// asset (first "data"-roled asset, else first asset) provides media type and
/// href. Geometric fields (size/geotransform) stay unset — no dataset is
/// opened; callers enrich by opening the asset with inspectRaster.
RasterMetadata stacItemToCanonical( const StacItem &item );

/// Generates a STAC-compatible Item from canonical metadata. Requires an
/// acquisition time or explicit datetime (STAC mandates it) and an extent
/// (geotransform-derived) for bbox/geometry; missing pieces are structured
/// errors, not silent omissions.
Json::Value canonicalToStacItem( const RasterMetadata &metadata, const std::string &assetHref,
                                 const std::string &itemId );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_STAC_MAPPER_H
