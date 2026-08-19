/***************************************************************************
     qgsimagewarper.h
     --------------------------------------
   Date                 : Sun Sep 16 12:03:20 AKDT 2007
    Copyright            : (C) 2007 by Gary E. Sherman
    Email                : sherman at mrcc dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGSIMAGEWARPER_H
#define QGSIMAGEWARPER_H

#include <gdalwarper.h>
#include <vector>

#include "qgscoordinatereferencesystem.h"
#include "qgsogrutils.h"

#include <QCoreApplication>
#include <QString>

class QgsGeorefTransform;
class QgsFeedback;


/**
 * \brief Wraps GDAL's warp pipeline for image georeferencing.
 *
 * Modifications from upstream QGIS:
 *  - Construction injects a QgsFeedback*; the heavy QgsImageWarperTask wrapper
 *    is dropped (callers run warpFile on a worker thread directly).
 *  - warpFile() returns a structured WarpResult with explicit failure modes
 *    (DiskFull / InputUnavailable / SingularTransform / Cancelled / GdalError)
 *    in addition to the legacy Result enum overload.
 *  - Progress callback wires the injected QgsFeedback for cooperative cancel.
 */
class QgsImageWarper
{
    Q_DECLARE_TR_FUNCTIONS( QgsImageWarper )
    Q_GADGET

  public:
    QgsImageWarper();
    explicit QgsImageWarper( QgsFeedback *feedback );

    enum class ResamplingMethod : int
    {
      NearestNeighbour = GRA_NearestNeighbour,
      Bilinear = GRA_Bilinear,
      Cubic = GRA_Cubic,
      CubicSpline = GRA_CubicSpline,
      Lanczos = GRA_Lanczos
    };
    Q_ENUM( ResamplingMethod )

    //! Task results (legacy enum, preserved for backward compatibility)
    enum class Result
    {
      Success,                  //!< Warping completed successfully
      Canceled,                 //!< Task was canceled before completion
      InvalidParameters,        //!< Invalid transform parameters
      SourceError,              //!< Error reading source
      TransformError,           //!< Error creating GDAL transformer
      DestinationCreationError, //!< Error creating destination file
      WarpFailure,              //!< Failed warping source
    };
    Q_ENUM( Result )

    //! Structured status for the WarpResult-returning overload.
    enum class WarpStatus
    {
      Ok,
      DiskFull,
      InputUnavailable,
      SingularTransform,
      Cancelled,
      GdalError
    };
    Q_ENUM( WarpStatus )

    //! Structured outcome of a warp call.
    struct WarpResult
    {
      WarpStatus status = WarpStatus::Ok;
      QString errorMessage;
      qint64 outputBytes = 0;
      int durationMs = 0;
    };

    /**
     * Legacy upstream signature — kept so existing callers (none in-tree yet)
     * still compile if/when ported.  Uses the explicit feedback argument.
     */
    Result warpFile(
      const QString &input,
      const QString &output,
      const QgsGeorefTransform &georefTransform,
      ResamplingMethod resampling,
      bool useZeroAsTrans,
      const QStringList &options,
      const QgsCoordinateReferenceSystem &crs,
      QgsFeedback *feedback,
      double destResX = 0.0,
      double destResY = 0.0,
      int backgroundValue = 0
    );

    /**
     * Structured-result overload used by SICNU GEO RS.
     *
     * Uses the QgsFeedback* injected via the constructor.  Performs:
     *   - pre-flight GDALOpen check -> WarpStatus::InputUnavailable
     *   - timing via QElapsedTimer
     *   - cancellation cleanup (deletes partial output)
     *   - CPLGetLastErrorMsg() classification -> DiskFull / GdalError
     *
     * \a outputSize is provided for API forward-compat; today it is ignored
     * (resolution is derived from \a destResX/Y like the legacy overload).
     */
    WarpResult warpFile(
      const QString &input,
      const QString &output,
      const QgsGeorefTransform *georefTransform,
      ResamplingMethod resampling,
      bool useZeroAsTrans,
      bool zeroIsTransparent,
      const QgsCoordinateReferenceSystem &crs,
      const QSize &outputSize = QSize(),
      double destResX = 0.0,
      double destResY = 0.0,
      int backgroundValue = 0
    );

  private:
    struct TransformChain
    {
        GDALTransformerFunc GDALTransformer;
        void *GDALTransformerArg = nullptr;
        double adfGeotransform[6];
        double adfInvGeotransform[6];
    };

    //! \sa addGeoToPixelTransform
    static int GeoToPixelTransform( void *pTransformerArg, int bDstToSrc, int nPointCount, double *x, double *y, double *z, int *panSuccess );

    /**
     * \brief Appends a transform from geocoordinates to pixel/line coordinates to the given GDAL transformer.
     *
     * The resulting transform is the functional composition of the given GDAL transformer and the
     * inverse geo transform.
     * \sa destroyGeoToPixelTransform
     * \returns Argument to use with the static GDAL callback \ref GeoToPixelTransform
     */
    void *addGeoToPixelTransform( GDALTransformerFunc GDALTransformer, void *GDALTransformerArg, double *padfGeotransform ) const;
    void destroyGeoToPixelTransform( void *GeoToPixelTransformArg ) const;

    bool openSrcDSAndGetWarpOpt(
      const QString &input, ResamplingMethod resampling, const GDALTransformerFunc &pfnTransform, gdal::dataset_unique_ptr &hSrcDS, gdal::warp_options_unique_ptr &psWarpOptions
    ) const;

    bool createDestinationDataset(
      const QString &outputName,
      GDALDatasetH hSrcDS,
      gdal::dataset_unique_ptr &hDstDS,
      uint resX,
      uint resY,
      double *adfGeoTransform,
      bool useZeroAsTrans,
      const QStringList &options,
      const QgsCoordinateReferenceSystem &crs,
      int backgroundValue = 0
    );

    //! \brief GDAL progress callback, used to forward progress to the feedback object and honour cancel.
    static int CPL_STDCALL updateWarpProgress( double dfComplete, const char *pszMessage, void *pProgressArg );

    GDALResampleAlg toGDALResampleAlg( ResamplingMethod method ) const;

    QgsFeedback *mFeedback = nullptr;
};


#endif
