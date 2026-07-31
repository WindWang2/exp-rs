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

#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cmath>

bool rsSaveGcpPointsFile( const QString &filePath, const QVector<QgsGcpPoint> &points )
{
  QFile pointFile( filePath );
  if ( !pointFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    return false;

  QTextStream out( &pointFile );
  out << "# QGEOS .points v2\n";
  out << "mapX,mapY,pixelX,pixelY,enable,dX,dY,residual,pointType\n";
  for ( const QgsGcpPoint &p : points )
  {
    const double resX = p.residual().x();
    const double resY = p.residual().y();
    const double resTotal = std::sqrt( resX * resX + resY * resY );
    out << QString::number( p.destinationPoint().x(), 'f', 8 ) << ","
        << QString::number( p.destinationPoint().y(), 'f', 8 ) << ","
        << QString::number( p.sourcePoint().x(), 'f', 6 ) << ","
        << QString::number( -p.sourcePoint().y(), 'f', 6 ) << ","
        << ( p.isEnabled() ? 1 : 0 ) << ","
        << QString::number( resX, 'f', 6 ) << ","
        << QString::number( resY, 'f', 6 ) << ","
        << QString::number( resTotal, 'f', 6 ) << ","
        << p.pointType() << "\n";
  }
  return true;
}

bool rsLoadGcpPointsFile( const QString &filePath, const QgsCoordinateReferenceSystem &destCrs,
                          QVector<QgsGcpPoint> &pointsOut )
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

    QgsGcpPoint p( QgsPointXY( px, py ), QgsPointXY( mx, my ), destCrs, enabled );
    p.setPointType( type );
    pointsOut.append( p );
  }

  return true;
}
