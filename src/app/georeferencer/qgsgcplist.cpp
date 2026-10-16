/***************************************************************************
    qgsgcplist.cpp - SICNU .points v2 codec
     --------------------------------------
    Date                 : 2026-06-02 (SICNU port)
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "qgsgcplist.h"

#include "qgspointxy.h"

#include "durable_file_write.h"

#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cmath>

bool rsSaveGcpPointsFile( const QString &filePath, const QVector<QgsGcpPoint> &points,
                          const double *sourceGeoTransform )
{
  // APP-4 (R14-D WP-B): serialize the identical textual format into a buffer,
  // then publish through the shared crash-or-valid authority (temp + fsync +
  // atomic rename + permission preservation + symlink-referent following).
  // The old in-place truncate could silently destroy a manually-picked GCP
  // set on a full disk while returning true.
  QByteArray bytes;
  QTextStream out( &bytes, QIODevice::WriteOnly );
  out << "# QGEOS .points v2\n";
  out << "mapX,mapY,pixelX,pixelY,enable,dX,dY,residual,pointType,crs\n";
  for ( const QgsGcpPoint &p : points )
  {
    double srcX = p.sourcePoint().x();
    double srcY = p.sourcePoint().y();
    if ( sourceGeoTransform )
    {
      const double *gt = sourceGeoTransform;
      const double det = gt[1] * gt[5] - gt[2] * gt[4];
      if ( std::abs( det ) > 1e-15 )
      {
        const double dx = srcX - gt[0];
        const double dy = srcY - gt[3];
        srcX = ( gt[5] * dx - gt[2] * dy ) / det;
        srcY = ( gt[1] * dy - gt[4] * dx ) / det;
      }
    }

    // Residuals live in RsGeorefFitResult (ADR 0056); dX,dY,residual are
    // format-compat zeros (the loader never reads them).
    out << QString::number( p.destinationPoint().x(), 'f', 8 ) << ","
        << QString::number( p.destinationPoint().y(), 'f', 8 ) << ","
        << QString::number( srcX, 'f', 6 ) << ","
        << QString::number( -srcY, 'f', 6 ) << ","
        << ( p.isEnabled() ? 1 : 0 ) << ",0,0,0,"
        << p.pointType() << ","
        << p.destinationPointCrs().authid() << "\n";
  }
  out.flush();
#if defined( Q_OS_WIN )
  // The old lane opened the file with QIODevice::Text (LF->CRLF on Windows);
  // keep the published bytes byte-identical to what it produced.
  bytes.replace( '\n', "\r\n" );
#endif
  return sicnu::writeBytesDurably( filePath, bytes ).ok;
}

bool rsLoadGcpPointsFile( const QString &filePath, const QgsCoordinateReferenceSystem &destCrs,
                          QVector<QgsGcpPoint> &pointsOut,
                          const double *sourceGeoTransform )
{
  QFile pointFile( filePath );
  if ( !pointFile.open( QIODevice::ReadOnly | QIODevice::Text ) )
    return false;

  pointsOut.clear();

  QTextStream in( &pointFile );
  bool v2 = false;
  QString first = in.readLine();
  if ( first.startsWith( "# QGEOS .points v2" ) )
  {
    v2 = true;
    in.readLine(); // consume header row
  }
  // Else: first line was the v1 header — already consumed, fall through.

  while ( !in.atEnd() )
  {
    const QString line = in.readLine().trimmed();
    if ( line.isEmpty() )
      continue;
    const QStringList cols = line.contains( ',' ) ? line.split( ',' ) : line.split( '\t' );
    if ( cols.size() < 5 )
      continue;

    const double mx = cols[0].toDouble();
    const double my = cols[1].toDouble();
    const double px = cols[2].toDouble();
    const double py = -cols[3].toDouble(); // pixelY stored negated
    const bool enabled = cols[4].toInt() != 0;
    const QString type = ( v2 && cols.size() >= 9 ) ? cols[8] : QString();
    // Optional 10th column carries the destination CRS authid (ADR 0056).
    // Files written before the column existed fall back to destCrs.
    QgsCoordinateReferenceSystem pointCrs = destCrs;
    if ( v2 && cols.size() >= 10 )
    {
      const QgsCoordinateReferenceSystem fileCrs( cols[9] );
      if ( fileCrs.isValid() )
        pointCrs = fileCrs;
    }

    double srcX = px;
    double srcY = py;
    if ( sourceGeoTransform )
    {
      const double *gt = sourceGeoTransform;
      srcX = gt[0] + px * gt[1] + py * gt[2];
      srcY = gt[3] + px * gt[4] + py * gt[5];
    }

    QgsGcpPoint p( QgsPointXY( srcX, srcY ), QgsPointXY( mx, my ), pointCrs, enabled );
    p.setPointType( type );
    pointsOut.append( p );
  }

  return true;
}
