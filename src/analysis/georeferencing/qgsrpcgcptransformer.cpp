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

std::unique_ptr<QgsGcpTransformerInterface> QgsRpcGcpTransformer::clone() const
{
  auto c = std::make_unique<QgsRpcGcpTransformer>( mSrc, mDem );
  c->mDemPath = mDemPath;
  c->mZOffset = mZOffset;
  c->mUseGcpRefinement = mUseGcpRefinement;
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
  return GDALRPCTransform;
}

void *QgsRpcGcpTransformer::GDALTransformerArgs() const
{
  return mTransformArg;
}
