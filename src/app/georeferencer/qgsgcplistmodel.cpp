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

#include <QLocale>
#include <QString>

#include <cmath>

QgsGCPListModel::QgsGCPListModel( QObject *parent )
  : QAbstractTableModel( parent )
{
}

void QgsGCPListModel::setGCPList( QgsGCPList *theGCPList )
{
  beginResetModel();
  mGCPList = theGCPList;
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
  if ( !mGCPList || index.row() < 0 || index.row() >= mGCPList->size() || index.column() < 0 || index.column() >= columnCount() )
    return QVariant();

  const Column column = static_cast<Column>( index.column() );
  const QgsGcpPoint *point = mGCPList->at( index.row() );
  if ( !point )
    return QVariant();

  switch ( role )
  {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case Qt::UserRole:
    case Qt::ToolTipRole:
      switch ( column )
      {
        case Column::Enabled:
          break;
        case Column::ID:
          return index.row();
        case Column::SourceX:
          return ( role == Qt::DisplayRole || role == Qt::ToolTipRole )
                 ? QVariant( formatNumber( point->sourcePoint().x() ) )
                 : QVariant( point->sourcePoint().x() );
        case Column::SourceY:
          return ( role == Qt::DisplayRole || role == Qt::ToolTipRole )
                 ? QVariant( formatNumber( point->sourcePoint().y() ) )
                 : QVariant( point->sourcePoint().y() );
        case Column::DestinationX:
        {
          const QgsPointXY td = point->transformedDestinationPoint( mTargetCrs, mTransformContext );
          return ( role == Qt::DisplayRole || role == Qt::ToolTipRole )
                 ? QVariant( formatNumber( td.x() ) )
                 : QVariant( td.x() );
        }
        case Column::DestinationY:
        {
          const QgsPointXY td = point->transformedDestinationPoint( mTargetCrs, mTransformContext );
          return ( role == Qt::DisplayRole || role == Qt::ToolTipRole )
                 ? QVariant( formatNumber( td.y() ) )
                 : QVariant( td.y() );
        }
        case Column::ResidualDx:
        case Column::ResidualDy:
        case Column::TotalResidual:
        {
          const double dX = point->residual().x();
          const double dY = point->residual().y();
          const double residual = std::sqrt( dX * dX + dY * dY );
          if ( residual == 0.0 )
            return tr( "n/a" );
          double value = 0;
          if ( column == Column::ResidualDx ) value = dX;
          else if ( column == Column::ResidualDy ) value = dY;
          else value = residual;
          return ( role == Qt::DisplayRole || role == Qt::ToolTipRole )
                 ? QVariant( formatNumber( value ) )
                 : QVariant( value );
        }
        case Column::PointType:
          return point->pointType();
        case Column::LastColumn:
          break;
      }
      break;

    case Qt::CheckStateRole:
      if ( column == Column::Enabled )
        return point->isEnabled() ? Qt::Checked : Qt::Unchecked;
      break;

    case Qt::TextAlignmentRole:
      if ( column == Column::Enabled )
        return Qt::AlignHCenter;
      if ( column == Column::PointType )
        return Qt::AlignLeft;
      if ( column != Column::LastColumn )
        return Qt::AlignRight;
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
        emit dataChanged( index, index );
        updateResiduals();
        emit pointEnabled( point, index.row() );
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
      updateResiduals();
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
      updateResiduals();
      return true;
    }

    case Column::PointType:
      if ( role == Qt::EditRole || role == Qt::DisplayRole )
      {
        point->setPointType( value.toString() );
        emit dataChanged( index, index );
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

  QString unitLabel;
  switch ( residualUnit() )
  {
    case Qgis::RenderUnit::MapUnits:
      unitLabel = tr( "map units" );
      break;
    case Qgis::RenderUnit::Pixels:
      unitLabel = tr( "pixels" );
      break;
    default:
      break;
  }

  switch ( static_cast<Column>( section ) )
  {
    case Column::Enabled:       return tr( "Enabled" );
    case Column::ID:            return tr( "ID" );
    case Column::SourceX:       return tr( "Source X" );
    case Column::SourceY:       return tr( "Source Y" );
    case Column::DestinationX:  return tr( "Dest. X" );
    case Column::DestinationY:  return tr( "Dest. Y" );
    case Column::ResidualDx:    return tr( "dX (%1)" ).arg( unitLabel );
    case Column::ResidualDy:    return tr( "dY (%1)" ).arg( unitLabel );
    case Column::TotalResidual: return tr( "Residual (%1)" ).arg( unitLabel );
    case Column::PointType:     return tr( "Type" );
    case Column::LastColumn:    break;
  }
  return QVariant();
}

Qgis::RenderUnit QgsGCPListModel::residualUnit() const
{
  if ( mGeorefTransform && mGeorefTransform->providesAccurateInverseTransformation() )
    return Qgis::RenderUnit::MapUnits;
  return Qgis::RenderUnit::Pixels;
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
