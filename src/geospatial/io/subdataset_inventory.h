/***************************************************************************
  geospatial/io/subdataset_inventory.h
  Geospatial I/O, COG & Interchange 11.0 — HDF/NetCDF/VRT subdataset
  inventory, safe-URI accounting and selection projection.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  io:inspect lists subdatasets as a side effect of raster inspection; there
  was no way to enumerate them (bounded, redacted, classified) or to project
  ONE selected subdataset into the canonical metadata model. This module is
  that operation surface:

  * inventory lists GDAL subdataset entries through the SUBDATASETS metadata
    domain, capped (truncated flag when the cap bites — never a silent cut);
  * every name is classified by the ResourceUri authority; log surfaces get
    display() (credential-redacted), never raw;
  * inspectSubdataset validates that the string IS a subdataset selector and
    then projects it through inspectRaster — the canonical model stays the
    only metadata truth.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_SUBDATASET_INVENTORY_H
#define SICNU_GEOSPATIAL_IO_SUBDATASET_INVENTORY_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <string>
#include <vector>

namespace sicnu::geo::io
{

/// Default inventory cap (mirrors InspectOptions::maxSubdatasets).
constexpr int kMaxSubdatasetEntries = 64;

struct SubdatasetEntry
{
    int index = 0;                ///< 1-based GDAL subdataset index
    std::string name;             ///< raw GDAL subdataset name (opens in GDAL)
    std::string description;      ///< GDAL-provided description
    std::string display;          ///< redacted display form of the name
    std::string kind;             ///< ResourceUri kind name for the selector
    std::string embeddedLocalPath; ///< embedded local payload when present
    Json::Value toJson() const;
};

struct SubdatasetInventory
{
    std::string source;
    std::string sourceDisplay;
    std::vector<SubdatasetEntry> entries;
    bool truncated = false; ///< true when the source declares more than the cap
    Json::Value toJson() const;
};

/// Enumerates the subdatasets of `source`. GeoError(OpenFailed) when the
/// source cannot be opened read-only; GeoError(InvalidArgument) when it
/// carries no SUBDATASETS domain (callers can treat that as "not multidim").
SubdatasetInventory inventorySubdatasets( const std::string &source, int maxEntries = kMaxSubdatasetEntries );

/// Projects ONE selected subdataset (by name as listed in the inventory) into
/// the canonical metadata model. GeoError(InvalidArgument) when the string is
/// not a subdataset selector (use the names from the inventory verbatim);
/// OpenFailed propagates from inspection.
RasterMetadata inspectSubdataset( const std::string &subdatasetName );

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_SUBDATASET_INVENTORY_H
