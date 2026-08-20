/***************************************************************************
    qgsgeoreftransform.cpp - Encapsulates GCP-based parameter estimation and
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

#include "qgsgeoreftransform.h"
#include "qgscoordinatetransform.h"
#include "qgsexception.h"

#include <cassert>
#include <cmath>
#include <gdal.h>
#include <gdal_alg.h>
#include <limits>

namespace {

//! Build a configured transform for one fit pass (ADR 0057): fresh facade of
//! \a method with the source raster loaded for pixel-space residuals, and RPC
//! options pushed through the interface (no dynamic_cast needed).
std::unique_ptr<QgsGeorefTransform> makeConfiguredTransform(
  QgsGcpTransformerInterface::TransformMethod method,
  const QString &sourceRasterPath, const QString &demPath,
  double demZOffset, bool rpcRefinement,
  const QgsCoordinateReferenceSystem &targetCrs = QgsCoordinateReferenceSystem() )
{
  auto transform = std::make_unique<QgsGeorefTransform>( method );
  if ( !sourceRasterPath.isEmpty() )
    transform->loadRaster( sourceRasterPath );
  transform->setDestinationCrs( targetCrs );
  if ( QgsGcpTransformerInterface *impl = transform->gcpTransformer() )
  {
    impl->setRpcOptions( sourceRasterPath, demPath, demZOffset, rpcRefinement );
  }
  return transform;
}

//! Source-pixel residuals over parallel (src, dst) arrays (ADR 0020 decision
//! 2): back-transform each destination point into source pixel space,
//! residual = predicted - observed pixel. Failed back-transforms yield
//! rsGeorefInvalidResidual() and are not counted. Optionally accumulates the
//! squared-error sum and count for the RMS.
QVector<QPointF> sourcePixelResiduals( QgsGeorefTransform &xf,
                                       const QVector<QgsPointXY> &src,
                                       const QVector<QgsPointXY> &dst,
                                       double *sumSqOut = nullptr,
                                       int *countOut = nullptr )
{
  QVector<QPointF> res;
  res.reserve( src.size() );
  double sumSq = 0.0;
  int n = 0;
  for ( int i = 0; i < src.size(); ++i )
  {
    QgsPointXY predicted;
    if ( !xf.transformWorldToRaster( dst.at( i ), predicted ) )
    {
      res.append( rsGeorefInvalidResidual() );
      continue;
    }
    const QgsPointXY observed = xf.toSourcePixel( src.at( i ) );
    const QPointF r( predicted.x() - observed.x(), predicted.y() - observed.y() );
    res.append( r );
    sumSq += r.x() * r.x() + r.y() * r.y();
    ++n;
  }
  if ( sumSqOut )
    *sumSqOut = sumSq;
  if ( countOut )
    *countOut = n;
  return res;
}

//! Source-pixel RMS over (src, dst) pairs; -1 when no point transforms.
double pixelRms( QgsGeorefTransform &xf,
                 const QVector<QgsPointXY> &src,
                 const QVector<QgsPointXY> &dst )
{
  double sumSq = 0.0;
  int n = 0;
  sourcePixelResiduals( xf, src, dst, &sumSq, &n );
  return ( n > 0 ) ? std::sqrt( sumSq / static_cast<double>( n ) ) : -1.0;
}

} // namespace

QgsGeorefTransform::QgsGeorefTransform( const QgsGeorefTransform &other )
  : mGeorefTransformImplementation( other.mGeorefTransformImplementation
                                      ? other.mGeorefTransformImplementation->clone()
                                      : nullptr )
  , mTransformParametrisation( other.mTransformParametrisation )
  , mRasterChangeCoords( other.mRasterChangeCoords )
{
  // The inner implementation's clone() carries method-specific state,
  // including RPC source/DEM/Z-offset/refine options, so no per-type
  // reconstruction is needed here (ADR 0057).
}

QgsGeorefTransform::QgsGeorefTransform( TransformMethod parametrisation )
{
  setMethod( parametrisation );
}

QgsGeorefTransform::QgsGeorefTransform() = default;

QgsGeorefTransform::~QgsGeorefTransform() = default;

QgsGeorefTransform::TransformMethod QgsGeorefTransform::transformParametrisation() const
{
  return mTransformParametrisation;
}

void QgsGeorefTransform::setMethod( TransformMethod parametrisation )
{
  if ( parametrisation != mTransformParametrisation )
  {
    mGeorefTransformImplementation = QgsGcpTransformerInterface::create( parametrisation );
    mParametersInitialized = false;
    mTransformParametrisation = parametrisation;
  }
}

void QgsGeorefTransform::loadRaster( const QString &fileRaster )
{
  mRasterChangeCoords.loadRaster( fileRaster );
}

QgsPointXY QgsGeorefTransform::toSourceCoordinate( const QgsPointXY &pixel ) const
{
  return mRasterChangeCoords.toXY( pixel );
}

bool QgsGeorefTransform::providesAccurateInverseTransformation() const
{
  return ( mTransformParametrisation == TransformMethod::Linear || mTransformParametrisation == TransformMethod::Helmert || mTransformParametrisation == TransformMethod::PolynomialOrder1 );
}

bool QgsGeorefTransform::parametersInitialized() const
{
  return mParametersInitialized;
}

std::unique_ptr<QgsGeorefTransform> QgsGeorefTransform::cloneTransform() const
{
  auto res = std::make_unique<QgsGeorefTransform>( *this );
  // Re-fit so GDAL/RPC transformer args are live on the clone (required for
  // RpcPhysical validity which depends on source raster path + DEM options).
  res->updateParametersFromGcps( mSourceCoordinates, mDestinationCoordinates, mInvertYAxis );
  return res;
}

std::unique_ptr<QgsGcpTransformerInterface> QgsGeorefTransform::clone() const
{
  return cloneTransform();
}

bool QgsGeorefTransform::updateParametersFromGcps( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, bool invertYAxis )
{
  mSourceCoordinates = sourceCoordinates;
  mDestinationCoordinates = destinationCoordinates;
  mInvertYAxis = invertYAxis;

  if ( !mGeorefTransformImplementation )
  {
    return false;
  }
  if ( sourceCoordinates.size() != destinationCoordinates.size() ) // Defensive sanity check
  {
    throw( std::domain_error( "Internal error: GCP mapping is not one-to-one" ) );
  }
  if ( sourceCoordinates.size() < minimumGcpCount() )
  {
    return false;
  }
  if ( mRasterChangeCoords.hasExistingGeoreference() )
  {
    const QVector<QgsPointXY> sourcePixelCoordinates = mRasterChangeCoords.getPixelCoords( sourceCoordinates );
    mParametersInitialized = mGeorefTransformImplementation->updateParametersFromGcps( sourcePixelCoordinates, destinationCoordinates, invertYAxis );
  }
  else
  {
    mParametersInitialized = mGeorefTransformImplementation->updateParametersFromGcps( sourceCoordinates, destinationCoordinates, invertYAxis );
  }
  return mParametersInitialized;
}

int QgsGeorefTransform::minimumGcpCount() const
{
  return mGeorefTransformImplementation ? mGeorefTransformImplementation->minimumGcpCount() : 0;
}

QgsGcpTransformerInterface::TransformMethod QgsGeorefTransform::method() const
{
  return mGeorefTransformImplementation ? mGeorefTransformImplementation->method() : TransformMethod::InvalidTransform;
}

GDALTransformerFunc QgsGeorefTransform::GDALTransformer() const
{
  return mGeorefTransformImplementation ? mGeorefTransformImplementation->GDALTransformer() : nullptr;
}

void *QgsGeorefTransform::GDALTransformerArgs() const
{
  return mGeorefTransformImplementation ? mGeorefTransformImplementation->GDALTransformerArgs() : nullptr;
}

bool QgsGeorefTransform::transformRasterToWorld( const QgsPointXY &raster, QgsPointXY &world )
{
  if ( mTransformParametrisation == TransformMethod::RpcPhysical )
  {
    if ( !transformPrivate( raster, world, false ) )
      return false;
    const QgsCoordinateReferenceSystem wgs84( QStringLiteral( "EPSG:4326" ) );
    if ( mDestinationCrs.isValid() && mDestinationCrs != wgs84 )
    {
      try
      {
        const QgsCoordinateTransform xform( wgs84, mDestinationCrs, QgsCoordinateTransformContext() );
        world = xform.transform( world );
      }
      catch ( QgsCsException & )
      {
        return false;
      }
    }
    return true;
  }

  // flip y coordinate due to different CS orientation
  const QgsPointXY raster_flipped( raster.x(), -raster.y() );
  return transformPrivate( raster_flipped, world, false );
}

bool QgsGeorefTransform::transformWorldToRaster( const QgsPointXY &world, QgsPointXY &raster )
{
  if ( mTransformParametrisation == TransformMethod::RpcPhysical )
  {
    QgsPointXY w = world;
    const QgsCoordinateReferenceSystem wgs84( QStringLiteral( "EPSG:4326" ) );
    if ( mDestinationCrs.isValid() && mDestinationCrs != wgs84 )
    {
      try
      {
        const QgsCoordinateTransform xform( mDestinationCrs, wgs84, QgsCoordinateTransformContext() );
        w = xform.transform( w );
      }
      catch ( QgsCsException & )
      {
        return false;
      }
    }
    return transformPrivate( w, raster, true );
  }

  const bool success = transformPrivate( world, raster, true );
  // flip y coordinate due to different CS orientation
  raster.setY( -raster.y() );
  return success;
}

bool QgsGeorefTransform::transform( const QgsPointXY &src, QgsPointXY &dst, bool rasterToWorld )
{
  return rasterToWorld ? transformRasterToWorld( src, dst ) : transformWorldToRaster( src, dst );
}

bool QgsGeorefTransform::getLinearOriginScale( QgsPointXY &origin, double &scaleX, double &scaleY ) const
{
  if ( transformParametrisation() != TransformMethod::Linear )
  {
    return false;
  }
  if ( !mGeorefTransformImplementation || !parametersInitialized() )
  {
    return false;
  }
  QgsLinearGeorefTransform *transform = dynamic_cast<QgsLinearGeorefTransform *>( mGeorefTransformImplementation.get() );
  return transform && transform->getOriginScale( origin, scaleX, scaleY );
}

bool QgsGeorefTransform::getOriginScaleRotation( QgsPointXY &origin, double &scaleX, double &scaleY, double &rotation ) const
{
  if ( mTransformParametrisation == TransformMethod::Linear )
  {
    rotation = 0.0;
    QgsLinearGeorefTransform *transform = dynamic_cast<QgsLinearGeorefTransform *>( mGeorefTransformImplementation.get() );
    return transform && transform->getOriginScale( origin, scaleX, scaleY );
  }
  else if ( mTransformParametrisation == TransformMethod::Helmert )
  {
    double scale;
    QgsHelmertGeorefTransform *transform = dynamic_cast<QgsHelmertGeorefTransform *>( mGeorefTransformImplementation.get() );
    if ( !transform || !transform->getOriginScaleRotation( origin, scale, rotation ) )
    {
      return false;
    }
    scaleX = scale;
    scaleY = scale;
    return true;
  }
  return false;
}


bool QgsGeorefTransform::transformPrivate( const QgsPointXY &src, QgsPointXY &dst, bool inverseTransform ) const
{
  // Copy the source coordinate for inplace transform
  double x = src.x();
  double y = src.y();

  if ( !QgsGcpTransformerInterface::transform( x, y, inverseTransform ) )
    return false;

  dst.setX( x );
  dst.setY( y );
  return true;
}

int QgsGeorefTransform::enabledGcpCount( const QVector<QgsGcpPoint> &gcps )
{
  int n = 0;
  for ( const auto &g : gcps )
  {
    if ( g.isEnabled() )
      ++n;
  }
  return n;
}

void QgsGeorefTransform::collectEnabledGcps( const QVector<QgsGcpPoint> &gcps,
                                             QVector<QgsPointXY> &src,
                                             QVector<QgsPointXY> &dst,
                                             const QgsCoordinateReferenceSystem &targetCrs,
                                             const QgsCoordinateTransformContext &context,
                                             bool *ok )
{
  if ( ok )
    *ok = true;
  src.clear();
  dst.clear();
  for ( const auto &g : gcps )
  {
    if ( !g.isEnabled() )
      continue;
    src.append( g.sourcePoint() );
    if ( targetCrs.isValid() && g.destinationPointCrs().isValid() && g.destinationPointCrs() != targetCrs )
    {
      bool ptOk = true;
      QgsPointXY pt = g.transformedDestinationPoint( targetCrs, context, &ptOk );
      if ( !ptOk )
      {
        if ( ok )
          *ok = false;
      }
      dst.append( pt );
    }
    else
    {
      dst.append( g.destinationPoint() );
    }
  }
}

int QgsGeorefTransform::minimumGcpCountFor( TransformMethod method )
{
  std::unique_ptr<QgsGcpTransformerInterface> t( QgsGcpTransformerInterface::create( method ) );
  return t ? t->minimumGcpCount() : 0;
}

RsGeorefFitResult QgsGeorefTransform::fit( const QVector<QgsGcpPoint> &gcps,
                                           TransformMethod method,
                                           const QString &sourceRasterPath,
                                           const QString &demPath,
                                           double demZOffset,
                                           const QgsCoordinateReferenceSystem &targetCrsOverride,
                                           bool invertYAxis )
{
  RsGeorefFitResult fit;
  fit.enabledGcpCount = enabledGcpCount( gcps );
  fit.residuals = QVector<QPointF>( gcps.size(), rsGeorefInvalidResidual() );

  fit.minimumGcpCount = minimumGcpCountFor( method );

  if ( fit.enabledGcpCount < fit.minimumGcpCount )
  {
    fit.ready = false;
    fit.errorMessage = QStringLiteral( "Need at least %1 GCPs, have %2" )
                         .arg( fit.minimumGcpCount )
                         .arg( fit.enabledGcpCount );
    return fit;
  }

  QgsCoordinateReferenceSystem targetCrs;
  if ( targetCrsOverride.isValid() )
  {
    targetCrs = targetCrsOverride;
  }
  else
  {
    for ( const auto &g : gcps )
    {
      if ( g.isEnabled() && g.destinationPointCrs().isValid() )
      {
        targetCrs = g.destinationPointCrs();
        break;
      }
    }
  }

  bool collectOk = true;
  QVector<QgsPointXY> src;
  QVector<QgsPointXY> dst;
  collectEnabledGcps( gcps, src, dst, targetCrs, QgsCoordinateTransformContext(), &collectOk );

  if ( !collectOk )
  {
    fit.ready = false;
    fit.errorMessage = QStringLiteral( "Failed to reproject one or more GCPs to common CRS" );
    return fit;
  }

  // Match the shell: always invert Y for GCP parameter estimation.
  std::unique_ptr<QgsGeorefTransform> transform;
  bool fitOk = false;

  if ( method == QgsGcpTransformerInterface::TransformMethod::RpcPhysical
       && fit.enabledGcpCount >= 3 )
  {
    // RPC refinement (ported from the shell's recomputeFit): fit once without
    // GCP-bias refinement for the before/after diagnostic, then fit the live
    // transform with refinement enabled.
    {
      auto beforeXf = makeConfiguredTransform( method, sourceRasterPath, demPath, demZOffset, /*rpcRefinement=*/false, targetCrs );
      try
      {
        if ( beforeXf && beforeXf->updateParametersFromGcps( src, dst, invertYAxis ) )
          fit.refinementRmsBefore = pixelRms( *beforeXf, src, dst );
      }
      catch ( ... ) {}
    }
    transform = makeConfiguredTransform( method, sourceRasterPath, demPath, demZOffset, /*rpcRefinement=*/true, targetCrs );
    try
    {
      fitOk = transform && transform->updateParametersFromGcps( src, dst, invertYAxis );
    }
    catch ( ... )
    {
      fitOk = false;
    }
  }
  else
  {
    transform = makeConfiguredTransform( method, sourceRasterPath, demPath, demZOffset, /*rpcRefinement=*/false, targetCrs );
    try
    {
      fitOk = transform && transform->updateParametersFromGcps( src, dst, invertYAxis );
    }
    catch ( ... )
    {
      fitOk = false;
    }
  }

  if ( !fitOk )
  {
    fit.ready = false;
    fit.errorMessage = QStringLiteral( "Parameter estimation failed" );
    return fit;
  }

  // Per-point residuals in SOURCE PIXELS (ADR 0020 decision 2): the enabled
  // residuals are computed once and scattered back into the gcps-aligned
  // vector; disabled GCPs keep the sentinel from the initial fill.
  double sumSq = 0.0;
  int n = 0;
  const QVector<QPointF> enabledResiduals =
    sourcePixelResiduals( *transform, src, dst, &sumSq, &n );
  int enabledIdx = 0;
  for ( int i = 0; i < gcps.size(); ++i )
  {
    if ( !gcps.at( i ).isEnabled() )
      continue;
    const QPointF r = enabledResiduals.at( enabledIdx++ );
    if ( rsGeorefResidualIsValid( r ) )
      fit.residuals[i] = r;
  }
  fit.ready = true;
  fit.rms = ( n > 0 ) ? std::sqrt( sumSq / static_cast<double>( n ) ) : -1.0;
  fit.errorMessage.clear();
  return fit;
}
