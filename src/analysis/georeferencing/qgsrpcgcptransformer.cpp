/***************************************************************************
    qgsrpcgcptransformer.cpp
     --------------------------------------
    Date                 : 2026-06-03
    Copyright            : (c) 2026 SICNU GEO RS
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsrpcgcptransformer.h"
#include "sicnu_logging.h"

#include "qgscoordinatetransform.h"
#include "qgsexception.h"

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal.h>
#include <gdal_priv.h>

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace
{
/// GDAL transformer argument for the refinement bias: the raw RPC arg plus a
/// constant post-translation in the RPC's WGS84 lon/lat output space.
///
/// The bias is deliberately NOT folded into `dfLONG_OFF`/`dfLAT_OFF`: GDAL's
/// RPC transform feeds those offsets into the polynomial input normalization,
/// so shifting them warps off-center pixels instead of translating the model
/// (#286).
struct RpcBiasWrapper
{
  void *rpcArg = nullptr;
  double lonBias = 0.0;
  double latBias = 0.0;
};

int rpcTransformWithBias( void *pTransformArg, int bDstToSrc, int nPointCount,
                          double *x, double *y, double *z, int *panSuccess )
{
  auto *w = static_cast<RpcBiasWrapper *>( pTransformArg );
  if ( bDstToSrc )
  {
    for ( int i = 0; i < nPointCount; ++i )
    {
      x[i] -= w->lonBias;
      y[i] -= w->latBias;
    }
    return GDALRPCTransform( w->rpcArg, TRUE, nPointCount, x, y, z, panSuccess );
  }
  const int ret = GDALRPCTransform( w->rpcArg, FALSE, nPointCount, x, y, z, panSuccess );
  for ( int i = 0; i < nPointCount; ++i )
  {
    if ( panSuccess && panSuccess[i] )
    {
      x[i] += w->lonBias;
      y[i] += w->latBias;
    }
  }
  return ret;
}
} // namespace

QgsRpcGcpTransformer::QgsRpcGcpTransformer( const QString &sourceRasterPath, const QString &demPath )
  : mSrc( sourceRasterPath ), mDem( demPath )
{
}

QgsRpcGcpTransformer::~QgsRpcGcpTransformer()
{
  freeTransformer();
}

void QgsRpcGcpTransformer::freeTransformer()
{
  if ( mTransformArg )
  {
    auto *w = static_cast<RpcBiasWrapper *>( mTransformArg );
    GDALDestroyRPCTransformer( w->rpcArg );
    delete w;
    mTransformArg = nullptr;
  }
}

std::unique_ptr<QgsGcpTransformerInterface> QgsRpcGcpTransformer::clone() const
{
  auto c = std::make_unique<QgsRpcGcpTransformer>( mSrc, mDem );
  c->mDemPath = mDemPath;
  c->mZOffset = mZOffset;
  c->mUseGcpRefinement = mUseGcpRefinement;
  c->mRefinementLonBias = mRefinementLonBias;
  c->mRefinementLatBias = mRefinementLatBias;
  c->mDestinationCrs = mDestinationCrs;
  return c;
}

bool QgsRpcGcpTransformer::setRpcOptions( const QString &sourceRasterPath, const QString &demPath, double zOffset, bool useRefine )
{
  mSrc = sourceRasterPath;
  mDemPath = demPath;
  // Keep the legacy mDem in sync — downstream code (Task 11.4.8) still reads it
  // via demPath() and the existing setDemPath() codepath.
  mDem = demPath;
  mZOffset = zOffset;
  mUseGcpRefinement = useRefine;
  return true;
}

bool QgsRpcGcpTransformer::updateParametersFromGcps( const QVector<QgsPointXY> &source,
                                                     const QVector<QgsPointXY> &destination,
                                                     bool /*invertYAxis*/ )
{
  freeTransformer();

  if ( mSrc.isEmpty() )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, "RPC transformer: source raster path is empty" );
    return false;
  }

  SICNU_LOG_INFO( SicnuLogTags::Georeferencing, QString( "Initializing RPC transformer: src=%1, GCPs=%2, refine=%3" )
      .arg( mSrc ).arg( source.size() ).arg( mUseGcpRefinement ) );

  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( mSrc.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, QString( "Failed to open source raster for RPC: %1" ).arg( mSrc ) );
    return false;
  }

  CSLConstList md = ds->GetMetadata( "RPC" );
  if ( !md )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, "No RPC metadata found in source raster" );
    GDALClose( ds );
    return false;
  }

  GDALRPCInfoV2 rpc;
  if ( !GDALExtractRPCInfoV2( md, &rpc ) )
  {
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, "Failed to extract RPC info from metadata" );
    GDALClose( ds );
    return false;
  }

  char **opts = nullptr;
  // RPC_HEIGHT provides a constant elevation term when no DEM is supplied,
  // or adds a vertical datum/height offset on top of the DEM-sampled elevation
  // if a DEM raster is present (per GDAL GDALRPCGetHeightAtLongLat convention).
  if ( mZOffset != 0.0 )
  {
    const QByteArray h = QString::number( mZOffset, 'f', 4 ).toUtf8();
    opts = CSLSetNameValue( opts, "RPC_HEIGHT", h.constData() );
  }
  if ( !mDem.isEmpty() && QFileInfo::exists( mDem ) )
  {
    opts = CSLSetNameValue( opts, "RPC_DEM", mDem.toUtf8().constData() );
    opts = CSLSetNameValue( opts, "RPC_DEMINTERPOLATION", "bilinear" );
  }

  // Task 11.5.5 - linear-bias GCP refinement.
  //
  // When enabled and at least 3 source/destination pairs are supplied, sample
  // the forward residual at each GCP, convert the destinations to the RPC's
  // WGS84 lon/lat space (#286: panel CRS may be projected), and take the
  // MEDIAN (destination - predicted) bias so a single misclicked GCP cannot
  // drag the whole model. The bias is applied as a post-translation via
  // RpcBiasWrapper - never by rewriting dfLONG_OFF/dfLAT_OFF, which also feed
  // the polynomial input normalization and would warp off-center pixels.
  if ( mUseGcpRefinement && source.size() >= 3 && source.size() == destination.size() )
  {
    void *baseArg = GDALCreateRPCTransformerV2( &rpc, FALSE, 0.1, opts );
    if ( baseArg )
    {
      const QgsCoordinateReferenceSystem wgs84( QStringLiteral( "EPSG:4326" ) );
      const QgsCoordinateTransform xform( mDestinationCrs, wgs84, QgsCoordinateTransformContext() );

      // Destination converted to the RPC's geographic space, or nullopt when
      // conversion fails (the GCP then stays out of the bias).
      auto wgsDst = [&]( int i ) -> std::optional<QgsPointXY>
      {
        if ( !mDestinationCrs.isValid() || mDestinationCrs == wgs84 )
          return destination[i];
        try
        {
          return xform.transform( destination[i] );
        }
        catch ( QgsCsException & )
        {
          return std::nullopt;
        }
      };

      std::vector<double> lonOff;
      std::vector<double> latOff;
      for ( int i = 0; i < source.size(); ++i )
      {
        const std::optional<QgsPointXY> dst = wgsDst( i );
        if ( !dst )
          continue;
        double X = source[i].x();
        double Y = source[i].y();
        double Z = 0.0;
        int success = 0;
        if ( GDALRPCTransform( baseArg, FALSE, 1, &X, &Y, &Z, &success ) && success )
        {
          lonOff.push_back( dst->x() - X );
          latOff.push_back( dst->y() - Y );
        }
      }
      GDALDestroyRPCTransformer( baseArg );

      const int n = static_cast<int>( lonOff.size() );
      if ( n >= 3 )
      {
        auto medianHypot = []( const std::vector<double> &lo,
                               const std::vector<double> &la ) -> double
        {
          std::vector<double> errs;
          errs.reserve( lo.size() );
          for ( size_t i = 0; i < lo.size(); ++i )
            errs.push_back( std::hypot( lo[i], la[i] ) );
          std::sort( errs.begin(), errs.end() );
          return errs[errs.size() / 2];
        };

        // Sorted copies for the median, unsorted residuals for the check.
        std::vector<double> lonSorted = lonOff;
        std::vector<double> latSorted = latOff;
        std::sort( lonSorted.begin(), lonSorted.end() );
        std::sort( latSorted.begin(), latSorted.end() );
        const double medLon = lonSorted[n / 2];
        const double medLat = latSorted[n / 2];

        // Apply only when the post-translation median residual improves.
        const double before = medianHypot( lonOff, latOff );
        std::vector<double> loShifted = lonOff;
        std::vector<double> laShifted = latOff;
        for ( size_t i = 0; i < loShifted.size(); ++i )
        {
          loShifted[i] -= medLon;
          laShifted[i] -= medLat;
        }
        const double after = medianHypot( loShifted, laShifted );
        if ( after < before )
        {
          mRefinementLonBias = medLon;
          mRefinementLatBias = medLat;
          SICNU_LOG_INFO( SicnuLogTags::Georeferencing,
                          QString( "RPC GCP refinement applied: lon=%1 lat=%2 (median residual %3 -> %4)" )
                            .arg( medLon, 0, 'f', 7 )
                            .arg( medLat, 0, 'f', 7 )
                            .arg( before, 0, 'g', 6 )
                            .arg( after, 0, 'g', 6 ) );
        }
        else
        {
          SICNU_LOG_INFO( SicnuLogTags::Georeferencing,
                          QString( "RPC GCP refinement skipped: median residual %1 -> %2" )
                            .arg( before, 0, 'g', 6 )
                            .arg( after, 0, 'g', 6 ) );
        }
      }
    }
  }

  void *rawArg = GDALCreateRPCTransformerV2( &rpc, FALSE, 0.1, opts );
  if ( rawArg )
  {
    auto *w = new RpcBiasWrapper();
    w->rpcArg = rawArg;
    w->lonBias = mRefinementLonBias;
    w->latBias = mRefinementLatBias;
    mTransformArg = w;
  }
  else
  {
    mTransformArg = nullptr;
  }


  CSLDestroy( opts );
  GDALClose( ds );

  if ( mTransformArg )
    SICNU_LOG_SUCCESS( SicnuLogTags::Georeferencing, "RPC transformer initialized successfully" );
  else
    SICNU_LOG_ERROR( SicnuLogTags::Georeferencing, "Failed to create RPC transformer" );

  return mTransformArg != nullptr;
}

GDALTransformerFunc QgsRpcGcpTransformer::GDALTransformer() const
{
  if ( !mTransformArg )
    return nullptr;
  return rpcTransformWithBias;
}

void *QgsRpcGcpTransformer::GDALTransformerArgs() const
{
  return mTransformArg;
}

bool QgsRpcGcpTransformer::transform( double &x, double &y, bool inverseTransform ) const
{
  if ( !mTransformArg )
    return false;

  double z = 0.0;
  int success = 0;
  return rpcTransformWithBias( mTransformArg, inverseTransform ? 1 : 0, 1, &x, &y, &z, &success ) && success;
}


