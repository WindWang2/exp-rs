/***************************************************************************
    qgsgcplist.cpp - SICNU port of QGIS GCP list (canvas-decoupled)
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

#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransformcontext.h"
#include "qgsgeoreftransform.h"
#include "qgspointxy.h"

#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cmath>

QgsGCPList::~QgsGCPList()
{
  deleteAll();
}

void QgsGCPList::deleteAll()
{
  qDeleteAll( *this );
  QList<QgsGcpPoint *>::clear();
}

void QgsGCPList::appendPoint( const QgsGcpPoint &point )
{
  QList<QgsGcpPoint *>::append( new QgsGcpPoint( point ) );
  emit changed();
}

void QgsGCPList::removePointAt( int row )
{
  if ( row < 0 || row >= size() )
    return;
  delete at( row );
  QList<QgsGcpPoint *>::removeAt( row );
  emit changed();
}

void QgsGCPList::clearPoints()
{
  if ( isEmpty() )
    return;
  deleteAll();
  emit changed();
}

void QgsGCPList::notifyPointsMutated()
{
  emit changed();
}

void QgsGCPList::clearResiduals()
{
  for ( QgsGcpPoint *p : *this )
  {
    if ( p )
      p->setResidual( QPointF() );
  }
}

void QgsGCPList::createGCPVectors( QVector<QgsPointXY> &sourcePoints,
                                   QVector<QgsPointXY> &destinationPoints,
                                   const QgsCoordinateReferenceSystem &targetCrs,
                                   const QgsCoordinateTransformContext &context ) const
{
  const int targetSize = countEnabledPoints();
  sourcePoints.clear();
  sourcePoints.reserve( targetSize );
  destinationPoints.clear();
  destinationPoints.reserve( targetSize );

  for ( const QgsGcpPoint *pt : std::as_const( *this ) )
  {
    if ( !pt || !pt->isEnabled() )
      continue;

    sourcePoints.push_back( pt->sourcePoint() );
    if ( targetCrs.isValid() )
    {
      destinationPoints.push_back( pt->transformedDestinationPoint( targetCrs, context ) );
    }
    else
    {
      destinationPoints.push_back( pt->destinationPoint() );
    }
  }
}

int QgsGCPList::countEnabledPoints() const
{
  int s = 0;
  for ( const QgsGcpPoint *pt : *this )
  {
    if ( pt && pt->isEnabled() )
      ++s;
  }
  return s;
}

void QgsGCPList::updateResiduals( QgsGeorefTransform *georefTransform,
                                  const QgsCoordinateReferenceSystem &targetCrs,
                                  const QgsCoordinateTransformContext &context,
                                  Qgis::RenderUnit residualUnit )
{
  bool transformReady = false;
  QVector<QgsPointXY> sourceCoordinates;
  QVector<QgsPointXY> destinationCoordinates;
  createGCPVectors( sourceCoordinates, destinationCoordinates, targetCrs, context );

  if ( georefTransform )
  {
    // Prefer the already-fitted transform (recomputeFit owns the fit).
    // Only fit here when the transform has not been initialized yet
    // (e.g. model/test callers that only invoke updateResiduals).
    if ( georefTransform->parametersInitialized() )
    {
      transformReady = true;
    }
    else
    {
      // invertYAxis=true matches the raster CS convention used by recomputeFit.
      transformReady = georefTransform->updateParametersFromGcps(
        sourceCoordinates, destinationCoordinates, /*invertYAxis=*/true );
    }
  }

  for ( int i = 0; i < size(); ++i )
  {
    QgsGcpPoint *p = at( i );
    if ( !p )
      continue;

    // SICNU semantics: skip disabled points entirely (do not overwrite residual).
    if ( !p->isEnabled() )
      continue;

    const QgsPointXY transformedDestinationPoint = targetCrs.isValid()
        ? p->transformedDestinationPoint( targetCrs, context )
        : p->destinationPoint();

    double dX = 0;
    double dY = 0;
    if ( georefTransform && transformReady && georefTransform->parametersInitialized() )
    {
      QgsPointXY dst;
      const QgsPointXY pixel = georefTransform->toSourcePixel( p->sourcePoint() );
      if ( residualUnit == Qgis::RenderUnit::Pixels )
      {
        if ( georefTransform->transformWorldToRaster( transformedDestinationPoint, dst ) )
        {
          dX = ( dst.x() - pixel.x() );
          dY = -( dst.y() - pixel.y() );
        }
      }
      else if ( residualUnit == Qgis::RenderUnit::MapUnits )
      {
        if ( georefTransform->transformRasterToWorld( pixel, dst ) )
        {
          dX = ( dst.x() - transformedDestinationPoint.x() );
          dY = ( dst.y() - transformedDestinationPoint.y() );
        }
      }
    }
    p->setResidual( QPointF( dX, dY ) );
  }
}

void QgsGCPList::updateResiduals( QgsGeorefTransform *georefTransform,
                                  const QgsCoordinateReferenceSystem & /*sourceCrs*/,
                                  const QgsCoordinateReferenceSystem &targetCrs )
{
  QgsCoordinateTransformContext ctx;
  updateResiduals( georefTransform, targetCrs, ctx, Qgis::RenderUnit::Pixels );
}

bool QgsGCPList::saveGcps( const QString &filePath ) const
{
  QFile pointFile( filePath );
  if ( !pointFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    return false;

  QTextStream out( &pointFile );
  out << "# QGEOS .points v2\n";
  out << "mapX,mapY,pixelX,pixelY,enable,dX,dY,residual,pointType\n";
  for ( const QgsGcpPoint *p : *this )
  {
    if ( !p )
      continue;
    const double resX = p->residual().x();
    const double resY = p->residual().y();
    const double resTotal = std::sqrt( resX * resX + resY * resY );
    out << QString::number( p->destinationPoint().x(), 'f', 8 ) << ","
        << QString::number( p->destinationPoint().y(), 'f', 8 ) << ","
        << QString::number( p->sourcePoint().x(), 'f', 6 ) << ","
        << QString::number( -p->sourcePoint().y(), 'f', 6 ) << ","
        << ( p->isEnabled() ? 1 : 0 ) << ","
        << QString::number( resX, 'f', 6 ) << ","
        << QString::number( resY, 'f', 6 ) << ","
        << QString::number( resTotal, 'f', 6 ) << ","
        << p->pointType() << "\n";
  }
  return true;
}

bool QgsGCPList::loadGcps( const QString &filePath, const QgsCoordinateReferenceSystem &destCrs )
{
  QFile pointFile( filePath );
  if ( !pointFile.open( QIODevice::ReadOnly | QIODevice::Text ) )
    return false;

  // Start with a clean list (emit one changed() at the end).
  if ( !isEmpty() )
    deleteAll();

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
    QList<QgsGcpPoint *>::append( new QgsGcpPoint( p ) );
  }

  emit changed();
  return true;
}
