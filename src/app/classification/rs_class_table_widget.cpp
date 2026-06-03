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
  mTable->setSelectionMode( QAbstractItemView::SingleSelection );
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
  mTable->setRowCount( 0 );
  if ( !mRois )
    return;

  const QHash<int, RsClassDef> defs = mRois->classDefs();
  QList<int> ids = defs.keys();
  std::sort( ids.begin(), ids.end() );

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
  mTable->selectRow( row );
}

int RsClassTableWidget::currentClassId() const
{
  const auto rows = mTable->selectionModel()->selectedRows();
  if ( rows.isEmpty() )
    return 0;
  auto *it = mTable->item( rows.first().row(), 0 );
  return it ? it->data( Qt::UserRole ).toInt() : 0;
}

void RsClassTableWidget::onSelectionChanged()
{
  emit currentClassChanged( currentClassId() );
}
