/***************************************************************************
     qgsrasterchangecoords.cpp
     --------------------------------------
    Date                 : 25-June-2011
    Copyright            : (C) 2011 by Luiz Motta
    Email                : motta.luiz at gmail.com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsrasterchangecoords.h"

#include <gdal.h>

#include "qgsogrutils.h"
#include "qgspoint.h"

#include <QFile>

void QgsRasterChangeCoords::loadRaster( const QString &fileRaster )
{
  GDALAllRegister();
  const gdal::dataset_unique_ptr hDS( GDALOpen( fileRaster.toUtf8().constData(), GA_ReadOnly ) );
  double adfGeoTransform[6];
  if ( GDALGetProjectionRef( hDS.get() ) && GDALGetGeoTransform( hDS.get(), adfGeoTransform ) == CE_None )
  {
    mHasExistingGeoreference = true;
    for ( int i = 0; i < 6; ++i )
      mGeoTransform[i] = adfGeoTransform[i];
    mUL_X = adfGeoTransform[0];
    mUL_Y = adfGeoTransform[3];
    mResX = adfGeoTransform[1];
    mResY = adfGeoTransform[5];
  }
  else
  {
    mHasExistingGeoreference = false;
  }
}

QVector<QgsPointXY> QgsRasterChangeCoords::getPixelCoords( const QVector<QgsPointXY> &mapCoords ) const
{
  const int size = mapCoords.size();
  QVector<QgsPointXY> pixelCoords( size );
  for ( int i = 0; i < size; i++ )
  {
    pixelCoords[i] = toColumnLine( mapCoords.at( i ) );
  }
  return pixelCoords;
}

QgsRectangle QgsRasterChangeCoords::transformExtent( const QgsRectangle &rect, bool toPixel ) const
{
  if ( !mHasExistingGeoreference )
    return rect;

  QgsRectangle rectReturn;
  const QgsPointXY p1( rect.xMinimum(), rect.yMinimum() );
  const QgsPointXY p2( rect.xMaximum(), rect.yMaximum() );

  auto func = toPixel ? &QgsRasterChangeCoords::toColumnLine : &QgsRasterChangeCoords::toXY;
  rectReturn.set( ( this->*func )( p1 ), ( this->*func )( p2 ) );

  return rectReturn;
}

QgsPointXY QgsRasterChangeCoords::toColumnLine( const QgsPointXY &pntMap ) const
{
  if ( !mHasExistingGeoreference )
    return QgsPointXY( pntMap.x(), pntMap.y() );

  const double det = mGeoTransform[1] * mGeoTransform[5] - mGeoTransform[2] * mGeoTransform[4];
  if ( std::abs( det ) < 1e-18 )
  {
    // Fallback to axis-aligned approximation for degenerate geotransforms.
    const double col = ( pntMap.x() - mUL_X ) / mResX;
    const double line = ( mUL_Y - pntMap.y() ) / mResY;
    return QgsPointXY( col, line );
  }
  const double dx = pntMap.x() - mGeoTransform[0];
  const double dy = pntMap.y() - mGeoTransform[3];
  const double col = ( mGeoTransform[5] * dx - mGeoTransform[2] * dy ) / det;
  const double row = ( -mGeoTransform[4] * dx + mGeoTransform[1] * dy ) / det;
  return QgsPointXY( col, row );
}

QgsPointXY QgsRasterChangeCoords::toXY( const QgsPointXY &pntPixel ) const
{
  if ( !mHasExistingGeoreference )
    return QgsPointXY( pntPixel.x(), pntPixel.y() );

  const double x = mGeoTransform[0] + mGeoTransform[1] * pntPixel.x() + mGeoTransform[2] * pntPixel.y();
  const double y = mGeoTransform[3] + mGeoTransform[4] * pntPixel.x() + mGeoTransform[5] * pntPixel.y();
  return QgsPointXY( x, y );
}
