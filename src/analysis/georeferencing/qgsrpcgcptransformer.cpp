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
  return new QgsRpcGcpTransformer( mSrc, mDem );
}

bool QgsRpcGcpTransformer::updateParametersFromGcps( const QVector<QgsPointXY> & /*source*/,
                                                     const QVector<QgsPointXY> & /*destination*/,
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
  if ( !mDem.isEmpty() && QFileInfo::exists( mDem ) )
  {
    opts = CSLSetNameValue( opts, "RPC_DEM", mDem.toUtf8().constData() );
    opts = CSLSetNameValue( opts, "RPC_DEMINTERPOLATION", "bilinear" );
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
