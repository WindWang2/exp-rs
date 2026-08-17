/***************************************************************************
    qgsgeoreftransform.h - Encapsulates GCP-based parameter estimation and
    reprojection for different transformation models.
     --------------------------------------
    Date                 : 18-Feb-2009
    Copyright            : (c) 2009 by Manuel Massing
    Email                : m.massing at warped-space.de
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSGEOREFTRANSFORM_H
#define QGSGEOREFTRANSFORM_H

#include <gdal_alg.h>


#include "qgsgcppoint.h"
#include "qgsgcptransformer.h"
#include "qgsrasterchangecoords.h"
#include "qgscoordinatetransformcontext.h"

#include <QPointF>
#include <QString>

#include <cmath>
#include <limits>

/**
 * \brief Transform class for different gcp-based transform methods.
 *
 * Select transform type via \ref selectTransformParametrisation.
 * Initialize and update parameters via \ref updateParametersFromGCPs.
 * An initialized instance then provides transform functions and GDALTransformer entry points
 * for warping and coordinate remapping.
 *
 * Delegates to concrete implementations of \ref QgsGeorefInterface. For exception safety,
 * this is preferred over using the subclasses directly.
 *
 * SICNU GEO RS (ADR 0057): also hosts the fit/residual engine — \ref fit()
 * performs enabled-GCP collection, min-count gating, the RPC before/after
 * double-fit, per-point source-pixel residuals, and RMS in one call.
 */

/**
 * Sentinel stored in RsGeorefFitResult::residuals for entries with no valid
 * residual (disabled GCPs, failed back-transform, or unfit points).
 */
inline QPointF rsGeorefInvalidResidual()
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  return QPointF( nan, nan );
}

inline bool rsGeorefResidualIsValid( const QPointF &r )
{
  return !std::isnan( r.x() ) && !std::isnan( r.y() );
}

/**
 * Outcome of a GCP parameter fit (ADR 0057 engine; formerly owned by the
 * georeferencing session header).
 *
 * Residual semantics (ADR 0020 decision 2): residuals live in source-image
 * PIXELS. Per enabled GCP, the destination point is back-transformed into
 * source pixel space (QgsGeorefTransform::transformWorldToRaster) and the
 * residual is the Euclidean delta against the observed source pixel
 * (QgsGeorefTransform::toSourcePixel). rms is the root-mean-square of the
 * per-point residual magnitudes over enabled GCPs.
 */
struct RsGeorefFitResult
{
  bool ready = false;
  double rms = -1.0; ///< source-pixel RMS over enabled GCPs
  int enabledGcpCount = 0;
  int minimumGcpCount = 0;
  QString errorMessage;
  /// Per-point residual (dx, dy) in source pixels, aligned with the input
  /// GCP ordering. Always sized to the input gcps().size(); disabled GCPs
  /// and points whose back-transform failed carry rsGeorefInvalidResidual().
  QVector<QPointF> residuals;
  /// RPC refinement diagnostic: source-pixel RMS of the unrefined RPC fit
  /// (before GCP-bias refinement). -1 when not applicable (non-RPC method,
  /// fewer than 3 enabled GCPs, or unrefined fit failed).
  double refinementRmsBefore = -1.0;
};

class  QgsGeorefTransform : public QgsGcpTransformerInterface
{
  public:
    explicit QgsGeorefTransform( TransformMethod parametrisation );
    QgsGeorefTransform();
    ~QgsGeorefTransform() override;

    //! copy constructor (deep-copies the inner transformer via its own clone())
    QgsGeorefTransform( const QgsGeorefTransform &other );

    /**
     * Switches the used transform type to the given parametrisation.
     */
    void setMethod( TransformMethod parametrisation );

    /**
     * Loads an existing raster image so that the source pixel to source layer conversion
     * can be correctly initialized.
     */
    void loadRaster( const QString &fileRaster );

    //! \returns Whether has image already has existing georeference
    bool hasExistingGeoreference() const { return mRasterChangeCoords.hasExistingGeoreference(); }

    /**
     * Returns the pixel coordinate from the source image given a layer coordinate from the source image.
     * \see toSourceCoordinate()
     */
    QgsPointXY toSourcePixel( const QgsPointXY &pntMap ) const { return mRasterChangeCoords.toColumnLine( pntMap ); }

    /**
     * Returns the layer coordinate from the source image given a pixel coordinate from the source image.
     * \see toSourcePixel()
     */
    QgsPointXY toSourceCoordinate( const QgsPointXY &pixel ) const;

    /**
     * Transforms a bounding box of the source image from source coordinates to source pixels or vice versa.
     */
    QgsRectangle transformSourceExtent( const QgsRectangle &rect, bool toPixel ) const { return mRasterChangeCoords.transformExtent( rect, toPixel ); }

    //! \brief The transform parametrisation currently in use.
    TransformMethod transformParametrisation() const;

    //! True for linear, Helmert, first order polynomial
    bool providesAccurateInverseTransformation() const;

    //! \returns whether the parameters of this transform have been initialized by \ref updateParametersFromGCPs
    bool parametersInitialized() const;

    /// Last invertYAxis flag passed to updateParametersFromGcps (for residual re-fit).
    bool invertYAxis() const { return mInvertYAxis; }

    std::unique_ptr<QgsGcpTransformerInterface> clone() const override;

    /**
     * Returns a fitted deep copy of this transform: same method, raster
     * georeference, RPC options, and GCP fit (GDAL/RPC transformer args are
     * re-created live). \ref clone() is implemented through this.
     */
    std::unique_ptr<QgsGeorefTransform> cloneTransform() const;
    bool updateParametersFromGcps( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, bool invertYAxis = false ) override;
    int minimumGcpCount() const override;
    TransformMethod method() const override;
    GDALTransformerFunc GDALTransformer() const override;
    void *GDALTransformerArgs() const override;

    /**
     * \brief Transform from pixel coordinates to georeferenced coordinates.
     *
     * \note Negative y-axis points down in raster CS.
     */
    bool transformRasterToWorld( const QgsPointXY &raster, QgsPointXY &world );

    /**
     * \brief Transform from referenced coordinates to raster coordinates.
     *
     * \note Negative y-axis points down in raster CS.
     */
    bool transformWorldToRaster( const QgsPointXY &world, QgsPointXY &raster );

    /**
     * \brief Transforms from raster to world if rasterToWorld is TRUE,
     * \brief or from world to raster when rasterToWorld is FALSE.
     *
     * \note Negative y-axis points down in raster CS.
     */
    bool transform( const QgsPointXY &src, QgsPointXY &dst, bool rasterToWorld );

    //! \brief Returns origin and scale if this is a linear transform, fails otherwise.
    bool getLinearOriginScale( QgsPointXY &origin, double &scaleX, double &scaleY ) const;

    //! \brief Returns origin, scale and rotation for linear and helmert transform, fails otherwise.
    bool getOriginScaleRotation( QgsPointXY &origin, double &scaleX, double &scaleY, double &rotation ) const;
    void setDestinationCrs( const QgsCoordinateReferenceSystem &crs ) override
    {
      mDestinationCrs = crs;
      if ( mGeorefTransformImplementation )
        mGeorefTransformImplementation->setDestinationCrs( crs );
    }

    QgsCoordinateReferenceSystem destinationCrs() const { return mDestinationCrs; }

    /**
     * \brief Returns a pointer to the underlying QgsGcpTransformerInterface implementation
     * (non-owning). Returns nullptr until \ref setMethod has been called.  RPC
     * configuration and DEM queries flow through the interface (ADR 0057:
     * setRpcOptions() / demPath()), so callers no longer need to downcast.
     */
    QgsGcpTransformerInterface *gcpTransformer() const { return mGeorefTransformImplementation.get(); }

    // SICNU GEO RS — fit/residual engine (ADR 0057). One seam for the fit
    // orchestration the session and shell previously hand-rolled.

    /**
     * Number of enabled GCPs in \a gcps (shared count helper for fit gating
     * and workflow mirroring).
     */
    static int enabledGcpCount( const QVector<QgsGcpPoint> &gcps );

    /**
     * Collects enabled GCPs from \a gcps into parallel source/destination
     * coordinate vectors. If \a targetCrs is valid, reprojects destination
     * points from their respective destinationPointCrs() to \a targetCrs.
     */
    static void collectEnabledGcps( const QVector<QgsGcpPoint> &gcps,
                                    QVector<QgsPointXY> &src, QVector<QgsPointXY> &dst,
                                    const QgsCoordinateReferenceSystem &targetCrs = QgsCoordinateReferenceSystem(),
                                    const QgsCoordinateTransformContext &context = QgsCoordinateTransformContext(),
                                    bool *ok = nullptr );

    /**
     * Minimum GCP count for \a method without constructing a full transform
     * facade (shared probe used by fit gating and shell validation).
     */
    static int minimumGcpCountFor( TransformMethod method );

    /**
     * One-shot fit/residual engine: collects enabled GCPs from \a gcps,
     * gates on the method minimum, performs the RPC before/after double-fit
     * when the method is RpcPhysical with >= 3 enabled GCPs, and computes
     * per-point source-pixel residuals + RMS (ADR 0020 decision 2).
     *
     * \a sourceRasterPath feeds both the pixel-space georeference
     * (QgsRasterChangeCoords) and, for RPC methods, the transformer itself
     * (coefficients come from the raster metadata). \a demPath and
     * \a demZOffset configure the RPC DEM/height terms.
     *
     * Returns a single RsGeorefFitResult; residuals align with \a gcps
     * ordering (disabled points carry rsGeorefInvalidResidual()).
     */
    static RsGeorefFitResult fit( const QVector<QgsGcpPoint> &gcps,
                                  TransformMethod method,
                                  const QString &sourceRasterPath,
                                  const QString &demPath,
                                  double demZOffset,
                                  bool invertYAxis = true );

  private:
    QgsGeorefTransform &operator=( const QgsGeorefTransform & ) = delete;

    bool transformPrivate( const QgsPointXY &src, QgsPointXY &dst, bool inverseTransform ) const;

    QVector<QgsPointXY> mSourceCoordinates;
    QVector<QgsPointXY> mDestinationCoordinates;
    bool mInvertYAxis = false;

    std::unique_ptr<QgsGcpTransformerInterface> mGeorefTransformImplementation;

    TransformMethod mTransformParametrisation = TransformMethod::InvalidTransform;
    bool mParametersInitialized = false;
    QgsRasterChangeCoords mRasterChangeCoords;
    QgsCoordinateReferenceSystem mDestinationCrs;

    friend class TestQgsGeoreferencer;
};

#endif //QGSGEOREFTRANSFORM_H
