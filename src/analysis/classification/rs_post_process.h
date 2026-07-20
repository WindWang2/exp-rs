// rs_post_process.h — Classification post-process pure operators.
//
// Sieve / majority filter / clump / recode on integer class-label rasters
// (cv::Mat CV_32S preferred, CV_8U accepted). File helpers use GDAL for
// load/save and polygonize. No GUI dependencies.
//
// NOTE: loadLabelRaster currently loads the full raster into a contiguous
// cv::Mat. Majority/sieve/clump therefore operate on the entire image in
// memory. For multi-GB label rasters prefer windowed I/O in a future pass;
// majorityFilter supports optional cancel checks so long runs can abort.
#pragma once

#include "qgis_analysis_export.h"

#include <QMap>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include <opencv2/core.hpp>

class QGIS_ANALYSIS_EXPORT RsPostProcess
{
  public:
    /**
     * Remove connected components whose area (pixel count) is strictly less
     * than \a threshold. \a connectedness is 4 or 8. Small components are
     * replaced with the majority label among border neighbors, or 0 if none.
     */
    static bool sieve( const cv::Mat &src, cv::Mat &dst, int threshold, int connectedness,
                       QString *err = nullptr );

    /**
     * Sliding-window mode filter. \a kernelOdd must be odd and ≥ 3 (3/5/7).
     * Output type is CV_32S.
     * Optional \a isCanceled is polled every few rows; returns false with
     * err="Cancelled" when it reports true. Full-image in-memory path
     * (see file note above).
     */
    static bool majorityFilter( const cv::Mat &src, cv::Mat &dst, int kernelOdd,
                                QString *err = nullptr,
                                const std::function<bool()> &isCanceled = nullptr );

    /**
     * Multi-label connected-component labeling (equal class values, 4 or 8
     * connectivity). Output component ids are CV_32S starting at 1.
     */
    static bool clump( const cv::Mat &src, cv::Mat &dst, int connectedness,
                       QString *err = nullptr );

    /**
     * Remap class ids via \a map. Unmapped values are left unchanged.
     * Output type is CV_32S.
     */
    static bool recode( const cv::Mat &src, cv::Mat &dst, const QMap<int, int> &map,
                        QString *err = nullptr );

    /**
     * Load a single-band label raster via GDAL into CV_32S labels, plus
     * geotransform and WKT projection.
     */
    static bool loadLabelRaster( const QString &path, cv::Mat &labels, double gt[6],
                                 QString &wkt, QString *err = nullptr );

    /**
     * Save a single-band label raster (CV_32S or CV_8U) as GeoTIFF (or driver
     * inferred from extension) with optional palette color table.
     */
    static bool saveLabelRaster( const QString &path, const cv::Mat &labels,
                                 const double gt[6], const QString &wkt,
                                 const QVector<QRgb> &colorTable,
                                 const QStringList &creationOptions,
                                 QString *err = nullptr );

    /**
     * GDALPolygonize label raster to vector. Driver chosen by extension:
     * .gpkg → GPKG, otherwise ESRI Shapefile.
     */
    static bool polygonize( const QString &labelRasterPath, const QString &vectorPath,
                            const QString &classField, QString *err = nullptr );
};
