/***************************************************************************
     qgsleastsquares.h
     --------------------------------------
    Date                 : Sun Sep 16 12:03:47 AKDT 2007
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
#ifndef QGSLEASTSQUARES_H
#define QGSLEASTSQUARES_H

#include "qgis_analysis_export.h"
#include "qgspointxy.h"

#include <QVector>
#include <stdexcept>

#define SIP_NO_FILE

/**
 * \ingroup analysis
 * \brief Utilities for calculation of least squares based transformations.
 *
 * \note Not available in Python bindings.
 * \since QGIS 3.20
*/
class QGIS_ANALYSIS_EXPORT QgsLeastSquares
{
  public:
    /**
     * Thrown when the GCP system is singular (collinear or otherwise degenerate)
     * and cannot be solved.
     */
    class SingularException : public std::runtime_error
    {
      public:
        SingularException()
          : std::runtime_error( "GCP system is singular (collinear or duplicate points)" ) {}
    };

    /**
     * Transforms the point at \a origin in-place, using a linear transformation calculated from the list of source and destination Ground Control Points (GCPs).
     * \a pixelXSize / \a pixelYSize are signed scales (mirroring is preserved; do not fabs).
     * \throws SingularException when source coordinates are collinear / degenerate.
     */
    static void linear( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, QgsPointXY &origin, double &pixelXSize, double &pixelYSize );

    /**
     * Transforms the point at \a origin in-place, using a helmert transformation calculated from the list of source and destination Ground Control Points (GCPs).
     * \throws SingularException when the 4×4 normal matrix is singular or the solution is non-finite.
     * \throws QgsNotSupportedException on QGIS built without GSL.
     */
    static void helmert( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, QgsPointXY &origin, double &pixelSize, double &rotation );

#if 0
    static void affine( QVector<QgsPointXY> mapCoords,
                        QVector<QgsPointXY> pixelCoords );

#endif

    /**
     * Calculates projective parameters from the list of source and destination Ground Control Points (GCPs).
     * \throws SingularException when coordinate normalization degenerates (zero mean distance).
     * \throws QgsNotSupportedException on QGIS built without GSL.
     */
    static void projective( const QVector<QgsPointXY> &sourceCoordinates, const QVector<QgsPointXY> &destinationCoordinates, double H[9] );
};

#endif // QGSLEASTSQUARES_H
