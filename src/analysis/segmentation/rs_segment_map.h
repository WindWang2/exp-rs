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

    int width() const { return mWidth; }
    int height() const { return mHeight; }

    /// Label at (row, col). Returns 0 for out-of-bounds.
    quint32 labelAt( int row, int col ) const;

    /// All unique segment IDs (excluding 0 = nodata).
    QSet<quint32> uniqueLabels() const;

    /// Number of unique segments (excluding nodata 0).
    int segmentCount() const;

    /// Pixel coordinates belonging to a segment.
    QVector<QPoint> pixelCoords( quint32 segmentId ) const;

    /// Number of pixels in a segment (O(1) with cache, no copy).
    int pixelCount( quint32 segmentId ) const;

    /// Raw label buffer (read-only).
    const QVector<quint32> &labels() const { return mLabels; }

    bool isEmpty() const { return mLabels.isEmpty(); }

  private:
    QVector<quint32> mLabels;
    int mWidth = 0;
    int mHeight = 0;

    // ISSUE 13 fix: lazy-built cache for pixelCoords()
    mutable QMap<quint32, QVector<QPoint>> mCoordsCache;
    void buildCoordsCache() const;
};
