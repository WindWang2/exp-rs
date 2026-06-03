// rs_class_quick_list.cpp — Phase 10A Task 10.3.
#include "rs_class_quick_list.h"

#include "rs_roi_collection.h"

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
  lay->addWidget( mList );
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
  mList->clear();
  if ( !mRois )
    return;

  const QHash<int, RsClassDef> defs = mRois->classDefs();
  QList<int> ids = defs.keys();
  std::sort( ids.begin(), ids.end() );

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
  }
}

int RsClassQuickList::rowCount() const
{
  return mList->count();
}
