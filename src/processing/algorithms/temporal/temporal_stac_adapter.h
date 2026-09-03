// src/processing/algorithms/temporal/temporal_stac_adapter.h
// STAC → TemporalCollection ingestion seam (minimal reliable version).
//
// Parses a STAC search response's `features` array into the fields the
// platform actually needs — id, datetime, platform, eo:cloud_cover, assets
// (href + type + eo:bands where present) — and builds a TemporalCollection
// whose scenes reference the selected raster asset per item. Deliberately
// NOT a STAC-schema copy: unknown fields are ignored, missing optional
// fields degrade to "unknown" metadata, and the scientific contracts
// (acquisition time mandatory, per-scene metadata) stay the collection's.
//
// Ingestion is pure: it consumes ALREADY-FETCHED JSON (a search response
// document from a file, an HTTP response, or a fixture), so it runs
// headless and is testable without network. Bbox / datetime / collection /
// limit / property filtering is applied client-side over the parsed items,
// which also covers offline (saved) search results.
#pragma once

#include "temporal_collection.h"

#include <json/json.h>

#include <QString>
#include <QStringList>

#include <map>

namespace sicnu::temporal
{

/// One parsed STAC item: the fields the platform consumes.
struct StacItem
{
    QString id;
    QString datetime;      ///< properties.datetime (raw ISO string; may be empty)
    QString platform;      ///< properties.platform (may be empty)
    QString processingLevel; ///< properties."s2:processing_level"/"processing:level" (may be empty)
    double cloudCover = -1.0; ///< properties."eo:cloud_cover"; < 0 when absent
    /// Selected raster href (COG candidate): the asset this item contributes
    /// to a collection. Prefixed with /vsicurl/ when remote (http/https).
    QString rasterHref;
    QString rasterAssetKey;  ///< the STAC asset key it came from
    /// Band names from the raster asset's eo:bands (best effort).
    QStringList rasterBands;
    /// Footprint bounds extracted from geometry (hasGeometry = false when the
    /// item carries no geometry; bbox filters then keep the item).
    bool hasGeometry = false;
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    /// Stringified properties (bounded use: property filters + diagnostics).
    std::map<QString, QString> properties;
};

/// Parse one STAC item (a Feature object). Returns false + @a error when the
/// item lacks a usable raster asset href or a datetime.
bool parseStacItem( const Json::Value &feature, StacItem *out, QString *error );

/// Filter + sort parsed items client-side:
///   bbox       — "minx,miny,maxx,maxy"; items whose footprint does not
///                intersect are dropped (items without geometry are kept).
///   datetime   — ISO start/end range "start/end" (either side may be empty),
///                or a single instant (exact match on the date part).
///   limit      — maximum item count (<= 0 = no limit).
///   propertyFilter — "key=value" (string equality on properties, e.g.
///                    "eo:cloud_cover" is compared numerically when both
///                    sides parse as numbers; "platform=Sentinel-2A").
/// Sorting is chronological (datetime ascending; missing datetimes last).
QVector<StacItem> filterStacItems( const QVector<StacItem> &items, const QString &bbox,
                                   const QString &datetime, int limit,
                                   const QString &propertyFilter, QStringList *warnings );

/// Builds a TemporalCollection from parsed STAC items. Acquisition times come
/// from the items' datetime (mandatory — items without one are rejected at
/// parse time); platform/processing level ride on the scene refs.
/// Returns false + @a error when fewer than two usable scenes remain.
bool temporalCollectionFromStacItems( const QVector<StacItem> &items,
                                      const QString &name, TemporalCollection *out,
                                      QString *error );

/// Convenience: parse a full STAC search-response document ({features:[...]}
/// or a bare FeatureCollection) and build the collection in one call.
/// Returns false + @a error when the document has no features or fewer than
/// two usable scenes remain after filtering.
bool temporalCollectionFromStacSearch( const Json::Value &searchResponse,
                                       const QString &name, TemporalCollection *out,
                                       QString *error, QStringList *warnings = nullptr );

} // namespace sicnu::temporal
