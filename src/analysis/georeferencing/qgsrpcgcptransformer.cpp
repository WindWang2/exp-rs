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

#include <cpl_conv.h>
#include <cpl_string.h>
#include <gdal.h>
#include <gdal_priv.h>

#include <QFileInfo>

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
    GDALDestroyRPCTransformer( mTransformArg );
    mTransformArg = nullptr;
  }
}

QgsGcpTransformerInterface *QgsRpcGcpTransformer::clone() const
{
  auto *c = new QgsRpcGcpTransformer( mSrc, mDem );
  c->mDemPath = mDemPath;
  c->mZOffset = mZOffset;
  c->mUseGcpRefinement = mUseGcpRefinement;
  return c;
}

void QgsRpcGcpTransformer::setRpcOptions( const QString &demPath, double zOffset, bool useRefine )
{
  mDemPath = demPath;
  // Keep the legacy mDem in sync — downstream code (Task 11.4.8) still reads it
  // via demPath() and the existing setDemPath() codepath.
  mDem = demPath;
  mZOffset = zOffset;
  mUseGcpRefinement = useRefine;
}

bool QgsRpcGcpTransformer::updateParametersFromGcps( const QVector<QgsPointXY> &source,
                                                     const QVector<QgsPointXY> &destination,
                                                     bool /*invertYAxis*/ )
{
  freeTransformer();

  if ( mSrc.isEmpty() )
    return false;

  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( mSrc.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return false;

  CSLConstList md = ds->GetMetadata( "RPC" );
  if ( !md )
  {
    GDALClose( ds );
    return false;
  }

  GDALRPCInfoV2 rpc;
  if ( !GDALExtractRPCInfoV2( md, &rpc ) )
  {
    GDALClose( ds );
    return false;
  }

  char **opts = nullptr;
  // RPC_HEIGHT is the constant-elevation fallback used when no DEM raster is
  // available; GDAL will let RPC_DEM take precedence when both are set, but
  // we still want the height term passed through so callers can dial it in
  // without supplying a DEM.  Push it BEFORE the RPC_DEM entries.
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

  // Task 11.5.5 — linear-bias GCP refinement.
  //
  // When enabled and at least 3 source/destination pairs are supplied, build
  // a temporary "base" RPC transformer using the current options, sample the
  // forward residual at each GCP, average the (destination - predicted)
  // bias in LON/LAT, and shift `rpc.dfLONG_OFF` / `rpc.dfLAT_OFF` by that
  // mean.  The final transformer is then created with the corrected RPC
  // structure but the SAME papszOptions (DEM/HEIGHT do not carry CRS bias).
  if ( mUseGcpRefinement && source.size() >= 3 && source.size() == destination.size() )
  {
    void *baseArg = GDALCreateRPCTransformerV2( &rpc, FALSE, 0.1, opts );
    if ( baseArg )
    {
      double meanLonOff = 0.0;
      double meanLatOff = 0.0;
      int n = 0;
      for ( int i = 0; i < source.size(); ++i )
      {
        double X = source[i].x();
        double Y = source[i].y();
        double Z = 0.0;
        int success = 0;
        if ( GDALRPCTransform( baseArg, FALSE, 1, &X, &Y, &Z, &success ) && success )
        {
          meanLonOff += ( destination[i].x() - X );
          meanLatOff += ( destination[i].y() - Y );
          ++n;
        }
      }
      GDALDestroyRPCTransformer( baseArg );
      if ( n >= 3 )
      {
        rpc.dfLONG_OFF += ( meanLonOff / n );
        rpc.dfLAT_OFF += ( meanLatOff / n );
      }
    }
  }

  mTransformArg = GDALCreateRPCTransformerV2( &rpc, FALSE, 0.1, opts );

  CSLDestroy( opts );
  GDALClose( ds );

  return mTransformArg != nullptr;
}

GDALTransformerFunc QgsRpcGcpTransformer::GDALTransformer() const
{
  if ( !mTransformArg )
    return nullptr;
  return GDALRPCTransform;
}

void *QgsRpcGcpTransformer::GDALTransformerArgs() const
{
  return mTransformArg;
}
