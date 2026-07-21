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
    disconnect( mGCPList, nullptr, this, nullptr );
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
    emit dataChanged( index( 0, static_cast<int>( Column::DestMapX ) ),
                      index( rowCount() - 1, static_cast<int>( Column::DestMapY ) ) );
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

void QgsGCPListModel::setRasterPaths( const QString &sourcePath, const QString &destPath )
{
  mHasSrcRaster = false;
  mHasDstRaster = false;
  if ( !sourcePath.isEmpty() )
  {
    mSrcCoords.loadRaster( sourcePath );
    mHasSrcRaster = mSrcCoords.hasExistingGeoreference();
  }
  if ( !destPath.isEmpty() )
  {
    mDstCoords.loadRaster( destPath );
    mHasDstRaster = mDstCoords.hasExistingGeoreference();
  }
  refreshAll();
  emit headerDataChanged( Qt::Horizontal, 0, columnCount() - 1 );
}

QgsPointXY QgsGCPListModel::toSourcePixel( const QgsPointXY &mapOrPixel ) const
{
  if ( mGeorefTransform && mGeorefTransform->hasExistingGeoreference() )
    return mGeorefTransform->toSourcePixel( mapOrPixel );
  if ( mHasSrcRaster )
    return mSrcCoords.toColumnLine( mapOrPixel );
  return mapOrPixel;
}

QgsPointXY QgsGCPListModel::toDestPixel( const QgsPointXY &mapOrPixel ) const
{
  if ( mHasDstRaster )
    return mDstCoords.toColumnLine( mapOrPixel );
  return QgsPointXY(); // unknown
}

int QgsGCPListModel::rowCount( const QModelIndex & ) const
{
  return mGCPList ? mGCPList->size() : 0;
}

int QgsGCPListModel::columnCount( const QModelIndex & ) const
{
  return static_cast<int>( Column::LastColumn );
}

QVariant QgsGCPListModel::data( const QModelIndex &index, int role ) const
{
  if ( !mGCPList || index.row() < 0 || index.row() >= mGCPList->size()
       || index.column() < 0 || index.column() >= columnCount() )
    return QVariant();

  const Column column = static_cast<Column>( index.column() );
  const QgsGcpPoint *point = mGCPList->at( index.row() );
  if ( !point )
    return QVariant();

  const double dX = point->residual().x();
  const double dY = point->residual().y();
  const double residual = std::sqrt( dX * dX + dY * dY );
  // Pixel residual: warn above 2 px; map residual: warn above 30 m
  const double warnThr = mResidualIsMap ? 30.0 : 2.0;
  const bool warn = point->isEnabled() && residual >= warnThr;
  const QString dash = QStringLiteral( "—" );

  const QgsPointXY destMap = point->destinationPoint();
  const QgsPointXY srcPx = toSourcePixel( point->sourcePoint() );
  const QgsPointXY dstPx = toDestPixel( destMap );

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
          return index.row() + 1;
        case Column::SourceMapX:
          return formatFixed( point->sourcePoint().x(), 2 );
        case Column::SourceMapY:
          return formatFixed( point->sourcePoint().y(), 2 );
        case Column::SourceCol:
          return ( mHasSrcRaster || ( mGeorefTransform && mGeorefTransform->hasExistingGeoreference() ) )
                   ? formatFixed( srcPx.x(), 1 )
                   : dash;
        case Column::SourceRow:
          return ( mHasSrcRaster || ( mGeorefTransform && mGeorefTransform->hasExistingGeoreference() ) )
                   ? formatFixed( srcPx.y(), 1 )
                   : dash;
        case Column::DestMapX:
          return formatFixed( destMap.x(), 2 );
        case Column::DestMapY:
          return formatFixed( destMap.y(), 2 );
        case Column::DestCol:
          return mHasDstRaster ? formatFixed( dstPx.x(), 1 ) : dash;
        case Column::DestRow:
          return mHasDstRaster ? formatFixed( dstPx.y(), 1 ) : dash;
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
        case Column::SourceMapX:
          return point->sourcePoint().x();
        case Column::SourceMapY:
          return point->sourcePoint().y();
        case Column::DestMapX:
          return destMap.x();
        case Column::DestMapY:
          return destMap.y();
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
      return QVariant( Qt::AlignRight | Qt::AlignVCenter );

    case Qt::ForegroundRole:
      if ( warn && ( column == Column::ResidualDx || column == Column::ResidualDy
                     || column == Column::TotalResidual ) )
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
  if ( !mGCPList || index.row() < 0 || index.row() >= mGCPList->size()
       || index.column() < 0 || index.column() >= columnCount() )
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
        mGCPList->notifyPointsMutated();
        return true;
      }
      break;

    case Column::SourceMapX:
    case Column::SourceMapY:
    {
      QgsPointXY sourcePoint = point->sourcePoint();
      if ( column == Column::SourceMapX )
        sourcePoint.setX( value.toDouble() );
      else
        sourcePoint.setY( value.toDouble() );
      point->setSourcePoint( sourcePoint );
      emit dataChanged( index, index );
      mGCPList->notifyPointsMutated();
      return true;
    }

    case Column::DestMapX:
    case Column::DestMapY:
    {
      QgsPointXY destinationPoint = point->destinationPoint();
      if ( column == Column::DestMapX )
        destinationPoint.setX( value.toDouble() );
      else
        destinationPoint.setY( value.toDouble() );
      point->setDestinationPoint( destinationPoint );
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

    default:
      return false;
  }
  return false;
}

Qt::ItemFlags QgsGCPListModel::flags( const QModelIndex &index ) const
{
  if ( !mGCPList || index.row() < 0 || index.row() >= mGCPList->size()
       || index.column() < 0 || index.column() >= columnCount() )
    return QAbstractTableModel::flags( index );

  const Column column = static_cast<Column>( index.column() );
  switch ( column )
  {
    case Column::Enabled:
      return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable;
    case Column::SourceMapX:
    case Column::SourceMapY:
    case Column::DestMapX:
    case Column::DestMapY:
    case Column::PointType:
      return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    default:
      return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  }
}

QVariant QgsGCPListModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
  if ( orientation != Qt::Horizontal || ( role != Qt::DisplayRole && role != Qt::ToolTipRole ) )
    return QVariant();

  const QString resU = mResidualIsMap ? tr( "m" ) : tr( "px" );

  if ( role == Qt::ToolTipRole )
  {
    switch ( static_cast<Column>( section ) )
    {
      case Column::SourceMapX:
      case Column::SourceMapY:
        return tr( "源影像图层坐标系下的地图坐标" );
      case Column::SourceCol:
      case Column::SourceRow:
        return tr( "源影像像元行列号（列=col，行=row，自左上角）" );
      case Column::DestMapX:
      case Column::DestMapY:
        return tr( "参考影像/地图图层坐标系下的坐标（Base）" );
      case Column::DestCol:
      case Column::DestRow:
        return tr( "参考影像像元行列号（列=col，行=row）" );
      case Column::ResidualDx:
      case Column::ResidualDy:
      case Column::TotalResidual:
        return mResidualIsMap ? tr( "残差（地图单位）" ) : tr( "残差（源影像像元）" );
      default:
        break;
    }
  }

  switch ( static_cast<Column>( section ) )
  {
    case Column::Enabled:       return tr( "启用" );
    case Column::ID:            return tr( "#" );
    case Column::SourceMapX:    return tr( "X源(map)" );
    case Column::SourceMapY:    return tr( "Y源(map)" );
    case Column::SourceCol:     return tr( "列源" );
    case Column::SourceRow:     return tr( "行源" );
    case Column::DestMapX:      return tr( "X参(map)" );
    case Column::DestMapY:      return tr( "Y参(map)" );
    case Column::DestCol:       return tr( "列参" );
    case Column::DestRow:       return tr( "行参" );
    case Column::ResidualDx:    return tr( "ΔX(%1)" ).arg( resU );
    case Column::ResidualDy:    return tr( "ΔY(%1)" ).arg( resU );
    case Column::TotalResidual: return tr( "RMS(%1)" ).arg( resU );
    case Column::PointType:     return tr( "类型" );
    case Column::LastColumn:    break;
  }
  return QVariant();
}

Qgis::RenderUnit QgsGCPListModel::residualUnit() const
{
  if ( mResidualIsMap )
    return Qgis::RenderUnit::MapUnits;
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
