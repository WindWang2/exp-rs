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

#include "rs_georeferencing_session.h"

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

void QgsGCPListModel::setGcpsSource( RsGeoreferencingSession *session )
{
  beginResetModel();
  if ( mSession )
    disconnect( mSession, nullptr, this, nullptr );
  mSession = session;
  if ( mSession )
  {
    connect( mSession, &RsGeoreferencingSession::gcpsChanged, this, [this]() {
      beginResetModel();
      endResetModel();
    } );
    connect( mSession, &RsGeoreferencingSession::fitChanged, this, [this]( const RsGeorefFitResult & ) {
      if ( rowCount() > 0 )
      {
        emit dataChanged( index( 0, static_cast<int>( Column::ResidualDx ) ),
                          index( rowCount() - 1, static_cast<int>( Column::TotalResidual ) ) );
      }
    } );
  }
  endResetModel();
}

void QgsGCPListModel::setTargetCrs( const QgsCoordinateReferenceSystem &targetCrs, const QgsCoordinateTransformContext &context )
{
  mTargetCrs = targetCrs;
  mTransformContext = context;
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

void QgsGCPListModel::setPixelConverters( std::function<QgsPointXY( const QgsPointXY & )> sourceToPixel,
                                          std::function<QgsPointXY( const QgsPointXY & )> destToPixel )
{
  mSourceToPixel = std::move( sourceToPixel );
  mDestToPixel = std::move( destToPixel );
  refreshAll();
  emit headerDataChanged( Qt::Horizontal, 0, columnCount() - 1 );
}

bool QgsGCPListModel::sourceHasExistingGeoreference() const
{
  return static_cast<bool>( mSourceToPixel );
}

int QgsGCPListModel::gcpRowCount() const
{
  return mSession ? mSession->gcps().size() : 0;
}

bool QgsGCPListModel::rowEnabled( int row ) const
{
  return mSession && row >= 0 && row < mSession->gcps().size() && mSession->gcps().at( row ).isEnabled();
}

QgsPointXY QgsGCPListModel::rowSourcePoint( int row ) const
{
  return ( mSession && row >= 0 && row < mSession->gcps().size() ) ? mSession->gcps().at( row ).sourcePoint() : QgsPointXY();
}

QgsPointXY QgsGCPListModel::rowDestinationPoint( int row ) const
{
  return ( mSession && row >= 0 && row < mSession->gcps().size() ) ? mSession->gcps().at( row ).destinationPoint() : QgsPointXY();
}

QString QgsGCPListModel::rowPointType( int row ) const
{
  return ( mSession && row >= 0 && row < mSession->gcps().size() ) ? mSession->gcps().at( row ).pointType() : QString();
}

QPointF QgsGCPListModel::rowResidual( int row ) const
{
  // Residuals come from the session's last fit. The invalid sentinel
  // (unfit / disabled / failed back-transform) maps to (0,0), mirroring the
  // legacy clearResiduals() display state.
  if ( !mSession )
    return QPointF( 0.0, 0.0 );
  const RsGeorefFitResult &fit = mSession->lastFit();
  if ( row >= 0 && row < fit.residuals.size() && rsGeorefResidualIsValid( fit.residuals.at( row ) ) )
    return fit.residuals.at( row );
  return QPointF( 0.0, 0.0 );
}

QgsPointXY QgsGCPListModel::toSourcePixel( const QgsPointXY &mapOrPixel ) const
{
  if ( mSourceToPixel )
    return mSourceToPixel( mapOrPixel );
  return mapOrPixel;
}

QgsPointXY QgsGCPListModel::toDestPixel( const QgsPointXY &mapOrPixel ) const
{
  if ( mDestToPixel )
    return mDestToPixel( mapOrPixel );
  return QgsPointXY(); // unknown
}

int QgsGCPListModel::rowCount( const QModelIndex & ) const
{
  return gcpRowCount();
}

int QgsGCPListModel::columnCount( const QModelIndex & ) const
{
  return static_cast<int>( Column::LastColumn );
}

QVariant QgsGCPListModel::data( const QModelIndex &index, int role ) const
{
  if ( index.row() < 0 || index.row() >= gcpRowCount()
       || index.column() < 0 || index.column() >= columnCount() )
    return QVariant();

  const Column column = static_cast<Column>( index.column() );

  const double dX = rowResidual( index.row() ).x();
  const double dY = rowResidual( index.row() ).y();
  const double residual = std::sqrt( dX * dX + dY * dY );
  // Pixel residual: warn above 2 px; map residual: warn above 30 m
  const double warnThr = mResidualIsMap ? 30.0 : 2.0;
  const bool enabled = rowEnabled( index.row() );
  const bool warn = enabled && residual >= warnThr;
  const QString dash = QStringLiteral( "—" );

  const QgsPointXY sourcePt = rowSourcePoint( index.row() );
  const QgsPointXY destMap = rowDestinationPoint( index.row() );
  const QgsPointXY srcPx = toSourcePixel( sourcePt );
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
          return formatFixed( sourcePt.x(), 2 );
        case Column::SourceMapY:
          return formatFixed( sourcePt.y(), 2 );
        case Column::SourceCol:
          return sourceHasExistingGeoreference()
                   ? formatFixed( srcPx.x(), 1 )
                   : dash;
        case Column::SourceRow:
          return sourceHasExistingGeoreference()
                   ? formatFixed( srcPx.y(), 1 )
                   : dash;
        case Column::DestMapX:
          return formatFixed( destMap.x(), 2 );
        case Column::DestMapY:
          return formatFixed( destMap.y(), 2 );
        case Column::DestCol:
          return mDestToPixel ? formatFixed( dstPx.x(), 1 ) : dash;
        case Column::DestRow:
          return mDestToPixel ? formatFixed( dstPx.y(), 1 ) : dash;
        case Column::ResidualDx:
          if ( !enabled )
            return dash;
          return formatFixed( dX, 2, true );
        case Column::ResidualDy:
          if ( !enabled )
            return dash;
          return formatFixed( dY, 2, true );
        case Column::TotalResidual:
          if ( !enabled )
            return dash;
          return formatFixed( residual, 2 );
        case Column::PointType:
          return rowPointType( index.row() );
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
          return sourcePt.x();
        case Column::SourceMapY:
          return sourcePt.y();
        case Column::DestMapX:
          return destMap.x();
        case Column::DestMapY:
          return destMap.y();
        case Column::PointType:
          return rowPointType( index.row() );
        default:
          break;
      }
      break;
    }

    case Qt::CheckStateRole:
      if ( column == Column::Enabled )
        return enabled ? Qt::Checked : Qt::Unchecked;
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
      return sourcePt;
  }
  return QVariant();
}

bool QgsGCPListModel::setData( const QModelIndex &index, const QVariant &value, int role )
{
  if ( !mSession )
    return false;
  if ( index.row() < 0 || index.row() >= gcpRowCount()
       || index.column() < 0 || index.column() >= columnCount() )
    return false;

  const Column column = static_cast<Column>( index.column() );
  const int row = index.row();

  // Session mode (ADR 0020): forward edits to the session's granular
  // mutations. The session's gcpsChanged() resets the model and refit()
  // refreshes the residual columns; the per-cell dataChanged below keeps
  // the widget's pointEnabled / pointTypeChanged signals working.
  switch ( column )
  {
    case Column::Enabled:
      if ( role == Qt::CheckStateRole )
      {
        mSession->setGcpEnabled( row, static_cast<Qt::CheckState>( value.toInt() ) == Qt::Checked );
        emit dataChanged( index, index, { Qt::CheckStateRole, Qt::DisplayRole } );
        return true;
      }
      return false;

    case Column::SourceMapX:
    case Column::SourceMapY:
    {
      QgsPointXY sourcePoint = rowSourcePoint( row );
      if ( column == Column::SourceMapX )
        sourcePoint.setX( value.toDouble() );
      else
        sourcePoint.setY( value.toDouble() );
      mSession->setGcpSource( row, sourcePoint );
      emit dataChanged( index, index );
      return true;
    }

    case Column::DestMapX:
    case Column::DestMapY:
    {
      QgsPointXY destinationPoint = rowDestinationPoint( row );
      if ( column == Column::DestMapX )
        destinationPoint.setX( value.toDouble() );
      else
        destinationPoint.setY( value.toDouble() );
      mSession->setGcpDestination( row, destinationPoint );
      emit dataChanged( index, index );
      return true;
    }

    case Column::PointType:
      if ( role == Qt::EditRole || role == Qt::DisplayRole )
      {
        mSession->setGcpPointType( row, value.toString() );
        emit dataChanged( index, index, { Qt::EditRole, Qt::DisplayRole } );
        return true;
      }
      return false;

    default:
      return false;
  }
}

Qt::ItemFlags QgsGCPListModel::flags( const QModelIndex &index ) const
{
  if ( index.row() < 0 || index.row() >= gcpRowCount()
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

void QgsGCPListModel::refreshAll()
{
  if ( rowCount() <= 0 )
    return;
  emit dataChanged( index( 0, 0 ),
                    index( rowCount() - 1, columnCount() - 1 ) );
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
