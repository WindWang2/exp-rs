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
#include <QAction>
#include <QContextMenuEvent>
#include <QFont>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>

#include <algorithm>

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
  setSelectionMode( QAbstractItemView::ExtendedSelection );
  setAlternatingRowColors( true );
  setShowGrid( false );
  setContextMenuPolicy( Qt::DefaultContextMenu );
  setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                   | QAbstractItemView::SelectedClicked );

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
  connect( selectionModel(), &QItemSelectionModel::selectionChanged,
           this, &QgsGCPListWidget::onSelectionChanged );
  connect( selectionModel(), &QItemSelectionModel::currentChanged,
           this, &QgsGCPListWidget::onSelectionChanged );
}

void QgsGCPListWidget::setGCPList( QgsGCPList *list )
{
  mList = list;
  mModel->setGCPList( list );
}

QList<int> QgsGCPListWidget::selectedRows() const
{
  QList<int> rows;
  if ( !selectionModel() )
    return rows;
  const QModelIndexList selected = selectionModel()->selectedRows();
  rows.reserve( selected.size() );
  for ( const QModelIndex &idx : selected )
  {
    if ( idx.isValid() )
      rows.append( idx.row() );
  }
  std::sort( rows.begin(), rows.end() );
  rows.erase( std::unique( rows.begin(), rows.end() ), rows.end() );
  return rows;
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

void QgsGCPListWidget::onSelectionChanged()
{
  const QList<int> rows = selectedRows();
  emit currentGcpRowChanged( rows.isEmpty() ? -1 : rows.first() );
}

void QgsGCPListWidget::keyPressEvent( QKeyEvent *event )
{
  if ( event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace )
  {
    const QList<int> rows = selectedRows();
    if ( !rows.isEmpty() )
    {
      emit deleteRowsRequested( rows );
      return;
    }
  }
  QTableView::keyPressEvent( event );
}

void QgsGCPListWidget::mouseDoubleClickEvent( QMouseEvent *event )
{
  const QModelIndex idx = indexAt( event->pos() );
  if ( idx.isValid() )
  {
    // Editable columns open the editor; ID / residual → locate both canvases.
    const int col = idx.column();
    if ( col == 1 || col == 6 || col == 7 || col == 8 )
    {
      emit zoomToBothRequested( idx.row() );
      return;
    }
  }
  QTableView::mouseDoubleClickEvent( event );
}

void QgsGCPListWidget::contextMenuEvent( QContextMenuEvent *event )
{
  const QModelIndex idx = indexAt( event->pos() );
  if ( !idx.isValid() || !mList || !mModel )
  {
    QTableView::contextMenuEvent( event );
    return;
  }

  if ( !selectionModel()->isSelected( idx ) )
    selectRow( idx.row() );

  const int row = idx.row();
  const QList<int> rows = selectedRows();

  QMenu menu( this );
  auto *zoomSrc = menu.addAction( tr( "定位到源点" ) );
  zoomSrc->setToolTip( tr( "将源影像画布平移到该 GCP 的源位置" ) );
  auto *zoomDst = menu.addAction( tr( "定位到目标点" ) );
  zoomDst->setToolTip( tr( "将参考/地图画布平移到该 GCP 的目标位置" ) );
  auto *zoomBoth = menu.addAction( tr( "两侧定位" ) );
  zoomBoth->setToolTip( tr( "同时在源与目标画布上定位该点" ) );
  menu.addSeparator();

  QgsGcpPoint *pt = ( row >= 0 && row < mList->size() ) ? mList->at( row ) : nullptr;
  auto *toggle = menu.addAction( pt && pt->isEnabled() ? tr( "禁用" ) : tr( "启用" ) );
  auto *editSrc = menu.addAction( tr( "编辑源坐标…" ) );
  auto *editDst = menu.addAction( tr( "编辑目标坐标…" ) );
  menu.addSeparator();
  auto *del = menu.addAction( rows.size() > 1
                                ? tr( "删除选中的 %1 个点" ).arg( rows.size() )
                                : tr( "删除" ) );
  del->setShortcut( QKeySequence::Delete );

  QAction *chosen = menu.exec( event->globalPos() );
  if ( !chosen )
    return;

  if ( chosen == zoomSrc )
    emit zoomToSourceRequested( row );
  else if ( chosen == zoomDst )
    emit zoomToDestRequested( row );
  else if ( chosen == zoomBoth )
    emit zoomToBothRequested( row );
  else if ( chosen == toggle && pt )
  {
    mModel->setData( mModel->index( row, 0 ),
                     pt->isEnabled() ? Qt::Unchecked : Qt::Checked,
                     Qt::CheckStateRole );
  }
  else if ( chosen == editSrc )
  {
    const QModelIndex editIdx = mModel->index( row, 2 );
    setCurrentIndex( editIdx );
    edit( editIdx );
  }
  else if ( chosen == editDst )
  {
    const QModelIndex editIdx = mModel->index( row, 4 );
    setCurrentIndex( editIdx );
    edit( editIdx );
  }
  else if ( chosen == del )
    emit deleteRowsRequested( rows );
}
