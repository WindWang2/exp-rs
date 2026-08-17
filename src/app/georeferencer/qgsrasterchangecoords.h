/***************************************************************************
     qgsrasterchangecoords.h
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

#ifndef QGSRASTERCHANGECOORDS_H
#define QGSRASTERCHANGECOORDS_H


#include "qgspointxy.h"
#include "qgsrectangle.h"

#include <QVector>

class  QgsRasterChangeCoords
{
  public:
    QgsRasterChangeCoords() = default;
    void loadRaster( const QString &fileRaster );
    bool hasExistingGeoreference() const { return mHasExistingGeoreference; }
    bool getGeoTransform( double *gt ) const
    {
      if ( !mHasExistingGeoreference || !gt )
        return false;
      for ( int i = 0; i < 6; ++i )
        gt[i] = mGeoTransform[i];
      return true;
    }
    QVector<QgsPointXY> getPixelCoords( const QVector<QgsPointXY> &mapCoords ) const;

    /**
     * Transforms a rectangle extent of the source image from source coordinates to source pixels or vice versa.
     */
    QgsRectangle transformExtent( const QgsRectangle &rect, bool toPixel ) const;

    QgsPointXY toColumnLine( const QgsPointXY &pntMap ) const;
    QgsPointXY toXY( const QgsPointXY &pntPixel ) const;

  private:
    bool mHasExistingGeoreference = false;
    double mGeoTransform[6] = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    double mUL_X = 0.;
    double mUL_Y = 0.;
    double mResX = 1.;
    double mResY = 1.;

    friend class TestQgsGeoreferencer;
};

#endif // QGSRASTERCHANGECOORDS_H
