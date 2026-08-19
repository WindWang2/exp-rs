// rs_segment_map.h — Phase 10B Task 10B.1: label image data model.
//
// Wraps a uint32 label image (row-major) produced by a segmentation algorithm
// (OTB MeanShift, connected components, etc.). Provides per-pixel label
// access, segment enumeration, and pixel coordinate queries.
#pragma once

#include "qgis_analysis_export.h"

#include <QMap>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QVector>

class QGIS_ANALYSIS_EXPORT RsSegmentMap
{
  public:
    RsSegmentMap() = default;

    /// Construct from raw label data (row-major, size = width * height).
    RsSegmentMap( QVector<quint32> labels, int width, int height );

    /// Load from a uint32 GeoTIFF label image written by OTB or similar.
    /// Returns an empty map on failure.
    static RsSegmentMap fromGeoTIFF( const QString &path );

    /// Write the label image to a UInt32 GeoTIFF with LZW compression.
    /// Geotransform/projection are copied from refPath; the reference grid size
    /// must match this map. 0 is written as GDAL NoData (background/unclassified).
    /// Fail-closed: on any error the message is set in error (when non-null) and
    /// any incomplete output file is removed.
    bool toGeoTIFF( const QString &path, const QString &refPath, QString *error = nullptr ) const;

    int width() const { return mWidth; }
    int height() const { return mHeight; }

    /// Label at (row, col). Returns 0 for out-of-bounds.
    quint32 labelAt( int row, int col ) const;

    /// All unique segment IDs (excluding 0 = nodata).
    QSet<quint32> uniqueLabels() const;

    /// Number of unique segments (excluding nodata 0).
    int segmentCount() const;

    /// Pixel coordinates belonging to a segment.
    /// Builds coords only for the requested segment (lazy), not a full map cache.
    QVector<QPoint> pixelCoords( quint32 segmentId ) const;

    /// Number of pixels in a segment. Uses a size-only cache (no QPoint storage).
    int pixelCount( quint32 segmentId ) const;

    /// Raw label buffer (read-only).
    const QVector<quint32> &labels() const { return mLabels; }

    bool isEmpty() const { return mLabels.isEmpty(); }

  private:
    QVector<quint32> mLabels;
    int mWidth = 0;
    int mHeight = 0;

    /// Size-only cache: segmentId → pixel count (cheap, built once).
    mutable QMap<quint32, int> mSizeCache;
    mutable bool mSizeCacheBuilt = false;
    /// Per-segment coords, filled lazily only when pixelCoords() is called.
    mutable QMap<quint32, QVector<QPoint>> mCoordsCache;
    mutable bool mCoordsIndexBuilt = false;

    void ensureSizeCache() const;
    void ensureCoordsIndex() const;
    QVector<QPoint> buildCoordsForSegment( quint32 segmentId ) const;
};
