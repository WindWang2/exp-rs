// rs_class_quick_list.cpp — Phase 10A Task 10.3.
#include "rs_class_quick_list.h"

#include "rs_roi_collection.h"

#include <QAbstractItemView>
#include <QIcon>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QVBoxLayout>

#include <algorithm>

RsClassQuickList::RsClassQuickList( QWidget *parent )
  : QWidget( parent )
{
  auto *lay = new QVBoxLayout( this );
  lay->setContentsMargins( 0, 0, 0, 0 );
  mList = new QListWidget( this );
  mList->setObjectName( QStringLiteral( "rsClassQuickList" ) );
  mList->setSelectionMode( QAbstractItemView::SingleSelection );
  lay->addWidget( mList );

  connect( mList, &QListWidget::itemSelectionChanged,
           this, &RsClassQuickList::onItemActivated );
  connect( mList, &QListWidget::itemClicked,
           this, [this]( QListWidgetItem * ) { onItemActivated(); } );
}

void RsClassQuickList::setRoiCollection( RsRoiCollection *col )
{
  if ( mRois )
    disconnect( mRois, nullptr, this, nullptr );
  mRois = col;
  if ( mRois )
  {
    connect( mRois, &RsRoiCollection::changed, this, &RsClassQuickList::rebuild );
    connect( mRois, &RsRoiCollection::classDefChanged, this, &RsClassQuickList::rebuild );
  }
  rebuild();
}

void RsClassQuickList::rebuild()
{
  const int keepId = currentClassId() > 0 ? currentClassId() : mStickyClassId;

  mList->blockSignals( true );
  mList->clear();
  if ( !mRois )
  {
    mList->blockSignals( false );
    return;
  }

  const QHash<int, RsClassDef> defs = mRois->classDefs();
  QList<int> ids = defs.keys();
  std::sort( ids.begin(), ids.end() );

  QListWidgetItem *restore = nullptr;
  for ( int id : ids )
  {
    const RsClassDef d = defs.value( id );
    const int roiN = mRois->roisForClass( id ).size();
    QPixmap pix( 14, 14 );
    pix.fill( d.color() );
    auto *item = new QListWidgetItem( QIcon( pix ),
                                      QStringLiteral( "%1  (%2)" ).arg( d.name() ).arg( roiN ) );
    item->setData( Qt::UserRole, id );
    mList->addItem( item );
    if ( keepId > 0 && id == keepId )
      restore = item;
  }

  if ( !restore && mList->count() > 0 )
    restore = mList->item( 0 );
  if ( restore )
    mList->setCurrentItem( restore );

  mList->blockSignals( false );

  const int now = currentClassId();
  if ( now > 0 )
    mStickyClassId = now;
}

void RsClassQuickList::setCurrentClassId( int classId )
{
  if ( classId <= 0 || !mList )
    return;
  for ( int i = 0; i < mList->count(); ++i )
  {
    auto *item = mList->item( i );
    if ( item && item->data( Qt::UserRole ).toInt() == classId )
    {
      mList->setCurrentItem( item );
      mStickyClassId = classId;
      return;
    }
  }
}

int RsClassQuickList::currentClassId() const
{
  auto *item = mList ? mList->currentItem() : nullptr;
  if ( !item )
    return mStickyClassId;
  return item->data( Qt::UserRole ).toInt();
}

void RsClassQuickList::onItemActivated()
{
  const int id = currentClassId();
  if ( id > 0 )
    mStickyClassId = id;
  emit currentClassChanged( id );
}

int RsClassQuickList::rowCount() const
{
  return mList->count();
}
