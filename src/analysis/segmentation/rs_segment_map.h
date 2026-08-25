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

#include <cstdint>
#include <cstddef>

class QGIS_ANALYSIS_EXPORT RsSegmentMap
{
  public:
    RsSegmentMap() = default;

    /// Construct from raw label data (row-major, size = width * height).
    RsSegmentMap( QVector<quint32> labels, int64_t width, int64_t height )
        : mLabels( std::move( labels ) )
        , mWidth( width )
        , mHeight( height )
    {
    }

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

    int64_t width() const { return mWidth; }
    int64_t height() const { return mHeight; }

    /// Label at (row, col) with 64-bit bounds and offset safety (#480, #497). Returns 0 for out-of-bounds.
    /// Label access is int64-only: an (int,int) overload alongside this one
    /// makes every long long / qsizetype call ambiguous under LP64 (int64_t
    /// is long there), so int callers bind implicitly instead.
    inline quint32 labelAt( int64_t row, int64_t col ) const
    {
        if ( row < 0 || row >= mHeight || col < 0 || col >= mWidth )
            return 0;
        const size_t idx = static_cast<size_t>(row) * static_cast<size_t>(mWidth) + static_cast<size_t>(col);
        if ( idx >= static_cast<size_t>(mLabels.size()) )
            return 0;
        return mLabels[static_cast<qsizetype>( idx )];
    }

    /// All unique segment IDs (excluding 0 = nodata).
    QSet<quint32> uniqueLabels() const;

    /// Number of unique segments (excluding nodata 0).
    int segmentCount() const;

    /// Pixel coordinates belonging to a segment.
    /// Builds coords only for the requested segment (lazy), not a full map cache.
    QVector<QPoint> pixelCoords( quint32 segmentId ) const;

    /// Number of pixels in a segment. Uses a size-only cache (no QPoint storage).
    int pixelCount( quint32 segmentId ) const;

    /// 64-bit safe pixel count
    int64_t pixelCount64( quint32 segmentId ) const
    {
        return static_cast<int64_t>(pixelCount( segmentId ));
    }

    /// Raw label buffer (read-only).
    const QVector<quint32> &labels() const { return mLabels; }

    bool isEmpty() const { return mLabels.isEmpty(); }

    /// Total pixel count with 64-bit safety.
    size_t totalPixels() const
    {
        return static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
    }

  private:
    QVector<quint32> mLabels;
    int64_t mWidth = 0;
    int64_t mHeight = 0;

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
