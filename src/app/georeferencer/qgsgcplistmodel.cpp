/***************************************************************************
    qgsgcplistmodel.cpp - SICNU port of QGIS GCP list table model
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
#include "qgsgcplistmodel.h"

#include "qgis.h"
#include "qgsgcplist.h"
#include "qgsgcppoint.h"
#include "qgsgeoreftransform.h"

#include <QBrush>
#include <QColor>
#include <QLocale>
#include <QString>
#include <QVariant>

#include <cmath>

namespace
{
  // Warn color from design.html for residuals >= 1 unit.
  const QColor sWarnColor( QStringLiteral( "#bf8700" ) );
  constexpr double sWarnThreshold = 1.0;

  QString formatFixed( double value, int decimals, bool showSign = false )
  {
    QString s = QString::number( value, 'f', decimals );
    if ( showSign && value >= 0.0 && !s.startsWith( '+' ) )
      s.prepend( '+' );
    return s;
  }
}

QgsGCPListModel::QgsGCPListModel( QObject *parent )
  : QAbstractTableModel( parent )
{
}

void QgsGCPListModel::setGCPList( QgsGCPList *theGCPList )
{
  beginResetModel();
  if ( mGCPList )
  {
    disconnect( mGCPList, nullptr, this, nullptr );
  }
  mGCPList = theGCPList;
  if ( mGCPList )
  {
    connect( mGCPList, &QgsGCPList::changed, this, [this]() {
      beginResetModel();
      endResetModel();
    } );
  }
  endResetModel();
  updateResiduals();
}

void QgsGCPListModel::setGeorefTransform( QgsGeorefTransform *georefTransform )
{
  mGeorefTransform = georefTransform;
  updateResiduals();
}

void QgsGCPListModel::setTargetCrs( const QgsCoordinateReferenceSystem &targetCrs, const QgsCoordinateTransformContext &context )
{
  mTargetCrs = targetCrs;
  mTransformContext = context;
  updateResiduals();
  if ( rowCount() > 0 )
  {
    emit dataChanged( index( 0, static_cast<int>( Column::DestinationX ) ),
                      index( rowCount() - 1, static_cast<int>( Column::DestinationY ) ) );
  }
  emit headerDataChanged( Qt::Horizontal, 0, columnCount() - 1 );
}

void QgsGCPListModel::setCoordinateDisplayMode( bool sourceIsMap, bool residualIsMap,
                                                  const QString &destCrsAuth )
{
  mSourceIsMap = sourceIsMap;
  mResidualIsMap = residualIsMap;
  mDestCrsAuth = destCrsAuth;
  emit headerDataChanged( Qt::Horizontal, 0, columnCount() - 1 );
  refreshAll();
}

int QgsGCPListModel::rowCount( const QModelIndex & ) const
{
  return mGCPList ? mGCPList->size() : 0;
}

int QgsGCPListModel::columnCount( const QModelIndex & ) const
{
  return 10;
}

QVariant QgsGCPListModel::data( const QModelIndex &index, int role ) const
{
  if ( !mGCPList || index.row() < 0 || index.row() >= mGCPList->size() || index.column() < 0 || index.column() >= columnCount() )
    return QVariant();

  const Column column = static_cast<Column>( index.column() );
  const QgsGcpPoint *point = mGCPList->at( index.row() );
  if ( !point )
    return QVariant();

  const double dX = point->residual().x();
  const double dY = point->residual().y();
  const double residual = std::sqrt( dX * dX + dY * dY );
  const bool warn = point->isEnabled() && residual >= sWarnThreshold;
  const QString dash = QStringLiteral( "—" );

  switch ( role )
  {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
    {
      switch ( column )
      {
        case Column::Enabled:
          return QString();
        case Column::ID:
          return index.row();
        case Column::SourceX:
          return formatFixed( point->sourcePoint().x(), 2 );
        case Column::SourceY:
          // Store and display the same canvas/map coordinates used for markers.
          // (Do not negate Y — dual-canvas pick stores real map Y, not GDAL row.)
          return formatFixed( point->sourcePoint().y(), 2 );
        case Column::DestinationX:
        {
          // Prefer raw destination as picked on REF/Map (image/map coords of base).
          // Only reproject when target CRS is valid and differs from stored CRS.
          if ( mTargetCrs.isValid()
               && point->destinationPointCrs().isValid()
               && mTargetCrs != point->destinationPointCrs() )
          {
            const QgsPointXY td = point->transformedDestinationPoint( mTargetCrs, mTransformContext );
            return formatFixed( td.x(), 2 );
          }
          return formatFixed( point->destinationPoint().x(), 2 );
        }
        case Column::DestinationY:
        {
          if ( mTargetCrs.isValid()
               && point->destinationPointCrs().isValid()
               && mTargetCrs != point->destinationPointCrs() )
          {
            const QgsPointXY td = point->transformedDestinationPoint( mTargetCrs, mTransformContext );
            return formatFixed( td.y(), 2 );
          }
          return formatFixed( point->destinationPoint().y(), 2 );
        }
        case Column::ResidualDx:
          if ( !point->isEnabled() )
            return dash;
          return formatFixed( dX, 2, true );
        case Column::ResidualDy:
          if ( !point->isEnabled() )
            return dash;
          return formatFixed( dY, 2, true );
        case Column::TotalResidual:
          if ( !point->isEnabled() )
            return dash;
          return formatFixed( residual, 2 );
        case Column::PointType:
          return point->pointType();
        case Column::LastColumn:
          break;
      }
      break;
    }

    case Qt::EditRole:
    {
      switch ( column )
      {
        case Column::SourceX:
          return point->sourcePoint().x();
        case Column::SourceY:
          return point->sourcePoint().y();
        case Column::DestinationX:
          return point->transformedDestinationPoint( mTargetCrs, mTransformContext ).x();
        case Column::DestinationY:
          return point->transformedDestinationPoint( mTargetCrs, mTransformContext ).y();
        case Column::PointType:
          return point->pointType();
        default:
          break;
      }
      break;
    }

    case Qt::CheckStateRole:
      if ( column == Column::Enabled )
        return point->isEnabled() ? Qt::Checked : Qt::Unchecked;
      break;

    case Qt::TextAlignmentRole:
      if ( column == Column::Enabled )
        return QVariant( Qt::AlignCenter );
      if ( column == Column::PointType )
        return QVariant( Qt::AlignLeft | Qt::AlignVCenter );
      if ( column != Column::LastColumn )
        return QVariant( Qt::AlignRight | Qt::AlignVCenter );
      break;

    case Qt::ForegroundRole:
      if ( warn && ( column == Column::ResidualDx || column == Column::ResidualDy || column == Column::TotalResidual ) )
        return QBrush( sWarnColor );
      break;

    case Qt::DecorationRole:
      if ( warn && column == Column::TotalResidual )
        return QVariant( QStringLiteral( "⚠" ) );
      break;

    case static_cast<int>( Role::SourcePointRole ):
      return point->sourcePoint();
  }
  return QVariant();
}

bool QgsGCPListModel::setData( const QModelIndex &index, const QVariant &value, int role )
{
  if ( !mGCPList || index.row() < 0 || index.row() >= mGCPList->size() || index.column() < 0 || index.column() >= columnCount() )
    return false;

  QgsGcpPoint *point = mGCPList->at( index.row() );
  if ( !point )
    return false;

  const Column column = static_cast<Column>( index.column() );
  switch ( column )
  {
    case Column::Enabled:
      if ( role == Qt::CheckStateRole )
      {
        const bool checked = static_cast<Qt::CheckState>( value.toInt() ) == Qt::Checked;
        point->setEnabled( checked );
        emit dataChanged( index, index, { Qt::CheckStateRole, Qt::DisplayRole } );
        emit pointEnabled( point, index.row() );
        // Notify shell so canvas markers recompute fit / refresh.
        mGCPList->notifyPointsMutated();
        return true;
      }
      break;

    case Column::SourceX:
    case Column::SourceY:
    {
      QgsPointXY sourcePoint = point->sourcePoint();
      if ( column == Column::SourceX )
        sourcePoint.setX( value.toDouble() );
      else
        sourcePoint.setY( value.toDouble() );
      point->setSourcePoint( sourcePoint );
      emit dataChanged( index, index );
      mGCPList->notifyPointsMutated();
      return true;
    }

    case Column::DestinationX:
    case Column::DestinationY:
    {
      QgsPointXY destinationPoint = point->transformedDestinationPoint( mTargetCrs, mTransformContext );
      if ( column == Column::DestinationX )
        destinationPoint.setX( value.toDouble() );
      else
        destinationPoint.setY( value.toDouble() );
      point->setDestinationPoint( destinationPoint );
      if ( mTargetCrs.isValid() )
        point->setDestinationPointCrs( mTargetCrs );
      emit dataChanged( index, index );
      mGCPList->notifyPointsMutated();
      return true;
    }

    case Column::PointType:
      if ( role == Qt::EditRole || role == Qt::DisplayRole )
      {
        point->setPointType( value.toString() );
        emit dataChanged( index, index, { Qt::EditRole, Qt::DisplayRole } );
        mGCPList->notifyPointsMutated();
        return true;
      }
      break;

    case Column::ID:
    case Column::ResidualDx:
    case Column::ResidualDy:
    case Column::TotalResidual:
    case Column::LastColumn:
      return false;
  }
  return false;
}

Qt::ItemFlags QgsGCPListModel::flags( const QModelIndex &index ) const
{
  if ( !mGCPList || index.row() < 0 || index.row() >= mGCPList->size() || index.column() < 0 || index.column() >= columnCount() )
    return QAbstractTableModel::flags( index );

  const Column column = static_cast<Column>( index.column() );
  switch ( column )
  {
    case Column::Enabled:
      return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable;
    case Column::SourceX:
    case Column::SourceY:
    case Column::DestinationX:
    case Column::DestinationY:
    case Column::PointType:
      return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    case Column::ID:
    case Column::ResidualDx:
    case Column::ResidualDy:
    case Column::TotalResidual:
    case Column::LastColumn:
      return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  }
  return QAbstractTableModel::flags( index );
}

QVariant QgsGCPListModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
  if ( orientation != Qt::Horizontal || ( role != Qt::DisplayRole && role != Qt::ToolTipRole ) )
    return QVariant();

  // Chinese headers — units depend on whether dual-pick stores map or pixel coords.
  const QString srcU = mSourceIsMap ? tr( "map" ) : tr( "px" );
  const QString resU = mResidualIsMap ? tr( "m" ) : tr( "px" );
  const QString dstU = mDestCrsAuth.isEmpty() ? tr( "map" ) : mDestCrsAuth;

  if ( role == Qt::ToolTipRole )
  {
    switch ( static_cast<Column>( section ) )
    {
      case Column::SourceX:
      case Column::SourceY:
        return mSourceIsMap
                 ? tr( "源影像画布上的地图/图层坐标（双画布点选）" )
                 : tr( "源影像像元坐标" );
      case Column::DestinationX:
      case Column::DestinationY:
        return tr( "参考/地图画布上的坐标（Base 影像坐标系）\n%1" ).arg( dstU );
      case Column::ResidualDx:
      case Column::ResidualDy:
      case Column::TotalResidual:
        return mResidualIsMap
                 ? tr( "残差（地图单位）" )
                 : tr( "残差（源影像像元）" );
      default:
        break;
    }
  }

  switch ( static_cast<Column>( section ) )
  {
    case Column::Enabled:       return tr( "启用" );
    case Column::ID:            return tr( "#" );
    case Column::SourceX:       return tr( "X 源 (%1)" ).arg( srcU );
    case Column::SourceY:       return tr( "Y 源 (%1)" ).arg( srcU );
    case Column::DestinationX:  return tr( "X 参 (%1)" ).arg( mDestCrsAuth.isEmpty() ? tr( "map" ) : QStringLiteral( "map" ) );
    case Column::DestinationY:  return tr( "Y 参 (%1)" ).arg( mDestCrsAuth.isEmpty() ? tr( "map" ) : QStringLiteral( "map" ) );
    case Column::ResidualDx:    return tr( "ΔX (%1)" ).arg( resU );
    case Column::ResidualDy:    return tr( "ΔY (%1)" ).arg( resU );
    case Column::TotalResidual: return tr( "RMS (%1)" ).arg( resU );
    case Column::PointType:     return tr( "类型" );
    case Column::LastColumn:    break;
  }
  return QVariant();
}

Qgis::RenderUnit QgsGCPListModel::residualUnit() const
{
  if ( mResidualIsMap )
    return Qgis::RenderUnit::MapUnits;
  // Default: residual in source-pixel space (matches canvas residual arrows).
  return Qgis::RenderUnit::Pixels;
}

void QgsGCPListModel::refreshAll()
{
  if ( rowCount() <= 0 )
    return;
  emit dataChanged( index( 0, 0 ),
                    index( rowCount() - 1, columnCount() - 1 ) );
}

void QgsGCPListModel::updateResiduals()
{
  if ( !mGCPList )
    return;
  mGCPList->updateResiduals( mGeorefTransform, mTargetCrs, mTransformContext, residualUnit() );
  if ( rowCount() > 0 )
  {
    emit dataChanged( index( 0, static_cast<int>( Column::ResidualDx ) ),
                      index( rowCount() - 1, static_cast<int>( Column::TotalResidual ) ) );
  }
}

QString QgsGCPListModel::formatNumber( double number )
{
  int decimalPlaces = 4;
  if ( std::fabs( number ) > 100000 )
    decimalPlaces = 2;
  else if ( std::fabs( number ) < 1000 )
    decimalPlaces = 6;
  return QLocale().toString( number, 'f', decimalPlaces );
}
