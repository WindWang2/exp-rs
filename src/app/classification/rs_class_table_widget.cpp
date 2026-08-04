// rs_class_table_widget.cpp — Phase 10A Task 10.3.
#include "rs_class_table_widget.h"

#include "rs_roi_collection.h"

#include <QHeaderView>
#include <QList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

RsClassTableWidget::RsClassTableWidget( QWidget *parent )
  : QWidget( parent )
{
  auto *lay = new QVBoxLayout( this );
  lay->setContentsMargins( 0, 0, 0, 0 );

  mTable = new QTableWidget( 0, 4, this );
  mTable->setObjectName( QStringLiteral( "rsClassTable" ) );
  mTable->setHorizontalHeaderLabels( { tr( "色" ), tr( "名称" ), tr( "ROI" ), tr( "像元" ) } );
  mTable->verticalHeader()->setVisible( false );
  mTable->verticalHeader()->setDefaultSectionSize( 26 );
  mTable->setSelectionBehavior( QAbstractItemView::SelectRows );
  mTable->setSelectionMode( QAbstractItemView::ExtendedSelection );
  mTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
  mTable->horizontalHeader()->setStretchLastSection( false );
  mTable->setColumnWidth( 0, 24 );
  mTable->setColumnWidth( 2, 50 );
  mTable->setColumnWidth( 3, 70 );
  mTable->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );

  lay->addWidget( mTable );

  connect( mTable, &QTableWidget::itemSelectionChanged,
           this, &RsClassTableWidget::onSelectionChanged );
}

void RsClassTableWidget::setRoiCollection( RsRoiCollection *col )
{
  if ( mRois )
    disconnect( mRois, nullptr, this, nullptr );
  mRois = col;
  if ( mRois )
  {
    connect( mRois, &RsRoiCollection::changed, this, &RsClassTableWidget::rebuild );
    connect( mRois, &RsRoiCollection::classDefChanged, this, &RsClassTableWidget::rebuild );
  }
  rebuild();
}

void RsClassTableWidget::rebuild()
{
  const QList<int> keepIds = selectedClassIds();
  const int keepId = !keepIds.isEmpty() ? keepIds.first() : ( currentClassId() > 0 ? currentClassId() : mStickyClassId );

  mTable->blockSignals( true );
  mTable->setRowCount( 0 );
  if ( !mRois )
  {
    mTable->blockSignals( false );
    return;
  }

  const QHash<int, RsClassDef> defs = mRois->classDefs();
  QList<int> ids = defs.keys();
  std::sort( ids.begin(), ids.end() );

  QList<int> restoreRows;
  int primaryRestoreRow = -1;
  for ( int id : ids )
  {
    const RsClassDef d = defs.value( id );
    const int row = mTable->rowCount();
    mTable->insertRow( row );

    auto *colorItem = new QTableWidgetItem;
    colorItem->setBackground( d.color() );
    colorItem->setData( Qt::UserRole, d.id() );
    mTable->setItem( row, 0, colorItem );

    mTable->setItem( row, 1, new QTableWidgetItem( d.name() ) );

    const int roiN = mRois->roisForClass( id ).size();
    const quint64 pxN = mRois->pixelCountForClass( id );

    auto *roiCell = new QTableWidgetItem( QString::number( roiN ) );
    roiCell->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
    mTable->setItem( row, 2, roiCell );

    auto *pxCell = new QTableWidgetItem( QString::number( pxN ) );
    pxCell->setTextAlignment( Qt::AlignRight | Qt::AlignVCenter );
    mTable->setItem( row, 3, pxCell );

    if ( keepIds.contains( id ) )
    {
      restoreRows.append( row );
      if ( id == keepId )
        primaryRestoreRow = row;
    }
  }

  if ( restoreRows.isEmpty() && mTable->rowCount() > 0 )
  {
    restoreRows.append( 0 );
    primaryRestoreRow = 0;
  }

  if ( mTable->selectionModel() )
    mTable->selectionModel()->clearSelection();

  for ( int row : restoreRows )
  {
    mTable->selectRow( row );
  }

  if ( primaryRestoreRow >= 0 && mTable->model() )
  {
    mTable->setCurrentIndex( mTable->model()->index( primaryRestoreRow, 0 ) );
  }

  mTable->blockSignals( false );

  // Emit only if sticky id changed (first fill / external set).
  const int now = currentClassId();
  if ( now > 0 && now != mStickyClassId )
  {
    mStickyClassId = now;
    emit currentClassChanged( now );
  }
  else if ( now > 0 )
  {
    mStickyClassId = now;
  }
}

int RsClassTableWidget::rowCount() const
{
  return mTable->rowCount();
}

int RsClassTableWidget::roiCountForRow( int row ) const
{
  auto *it = mTable->item( row, 2 );
  return it ? it->text().toInt() : 0;
}

quint64 RsClassTableWidget::pixelCountForRow( int row ) const
{
  auto *it = mTable->item( row, 3 );
  return it ? it->text().toULongLong() : 0;
}

void RsClassTableWidget::setCurrentRow( int row )
{
  if ( row < 0 || row >= mTable->rowCount() )
    return;
  mTable->selectRow( row );
}

void RsClassTableWidget::setCurrentClassId( int classId )
{
  if ( classId <= 0 || !mTable )
    return;
  for ( int r = 0; r < mTable->rowCount(); ++r )
  {
    auto *it = mTable->item( r, 0 );
    if ( it && it->data( Qt::UserRole ).toInt() == classId )
    {
      mTable->selectRow( r );
      mStickyClassId = classId;
      return;
    }
  }
}

int RsClassTableWidget::currentClassId() const
{
  if ( !mTable || mTable->rowCount() == 0 )
    return 0;

  const auto rows = mTable->selectionModel()->selectedRows();
  if ( rows.isEmpty() )
    return mStickyClassId;

  const QModelIndex curIdx = mTable->currentIndex();
  if ( curIdx.isValid() && mTable->selectionModel()->isSelected( curIdx ) )
  {
    auto *it = mTable->item( curIdx.row(), 0 );
    if ( it )
      return it->data( Qt::UserRole ).toInt();
  }

  auto *it = mTable->item( rows.first().row(), 0 );
  return it ? it->data( Qt::UserRole ).toInt() : mStickyClassId;
}

QList<int> RsClassTableWidget::selectedClassIds() const
{
  QList<int> result;
  if ( !mTable || !mTable->selectionModel() )
    return result;

  const auto rows = mTable->selectionModel()->selectedRows();
  for ( const QModelIndex &idx : rows )
  {
    auto *it = mTable->item( idx.row(), 0 );
    if ( it )
    {
      const int id = it->data( Qt::UserRole ).toInt();
      if ( id > 0 && !result.contains( id ) )
        result.append( id );
    }
  }
  return result;
}

void RsClassTableWidget::onSelectionChanged()
{
  const int id = currentClassId();
  if ( id > 0 )
    mStickyClassId = id;
  emit currentClassChanged( id );
}
