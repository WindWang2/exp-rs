/***************************************************************************
    qgsgcplistwidget.cpp - SICNU GCP table view (design.html ArtboardGeoref)
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
#include "qgsgcplistwidget.h"

#include "qgsgcplist.h"
#include "qgsgcplistmodel.h"
#include "qgsgcppoint.h"
#include "qgsgeorefdelegates.h"

#include <QAbstractItemModel>
#include <QFont>
#include <QHeaderView>

QgsGCPListWidget::QgsGCPListWidget( QWidget *parent )
  : QTableView( parent )
{
  setObjectName( QStringLiteral( "rsGcpTable" ) );

  mModel = new QgsGCPListModel( this );
  setModel( mModel );

  verticalHeader()->setVisible( false );
  verticalHeader()->setDefaultSectionSize( 26 );
  horizontalHeader()->setSectionResizeMode( QHeaderView::Interactive );
  horizontalHeader()->setStretchLastSection( true );

  setSelectionBehavior( QAbstractItemView::SelectRows );
  setSelectionMode( QAbstractItemView::SingleSelection );
  setAlternatingRowColors( true );
  setShowGrid( false );

  QFont mono( QStringLiteral( "IBM Plex Mono" ), 10 );
  setFont( mono );
  horizontalHeader()->setFont( QFont( QStringLiteral( "IBM Plex Sans" ), 9, QFont::DemiBold ) );

  // Column widths from design.html: 40,40,120,120,140,140,80,80,90,120
  const int widths[10] = { 40, 40, 120, 120, 140, 140, 80, 80, 90, 120 };
  for ( int c = 0; c < 10; ++c )
    setColumnWidth( c, widths[c] );

  setItemDelegateForColumn( 9, new RsGcpTypeDelegate( this ) );

  connect( mModel, &QAbstractItemModel::dataChanged,
           this, &QgsGCPListWidget::onModelDataChanged );
}

void QgsGCPListWidget::setGCPList( QgsGCPList *list )
{
  mList = list;
  mModel->setGCPList( list );
}

void QgsGCPListWidget::onModelDataChanged( const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles )
{
  if ( !mList || !topLeft.isValid() )
    return;
  // Only react to single-cell edits originating from the user; bulk residual
  // updates span multiple columns/rows and are ignored.
  if ( topLeft != bottomRight )
    return;

  const int row = topLeft.row();
  if ( row < 0 || row >= mList->size() )
    return;
  const QgsGcpPoint *point = mList->at( row );
  if ( !point )
    return;

  const int col = topLeft.column();
  if ( col == 0 && ( roles.isEmpty() || roles.contains( Qt::CheckStateRole ) ) )
  {
    emit pointEnabled( row, point->isEnabled() );
  }
  else if ( col == 9 && ( roles.isEmpty() || roles.contains( Qt::EditRole ) || roles.contains( Qt::DisplayRole ) ) )
  {
    emit pointTypeChanged( row, point->pointType() );
  }
}
