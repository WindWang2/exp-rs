#include "rs_roi_collection.h"

RsRoiCollection::RsRoiCollection( QObject *parent )
  : QObject( parent )
{}

void RsRoiCollection::appendRoi( const RsRoi &roi )
{
  mRois.append( roi );
  emit roiAdded( roi.classId(), mRois.size() - 1 );
  emit changed();
}

void RsRoiCollection::removeRoiAt( int i )
{
  if ( i < 0 || i >= mRois.size() )
    return;
  mRois.removeAt( i );
  emit roiRemoved( i );
  emit changed();
}

void RsRoiCollection::clear()
{
  if ( mRois.isEmpty() )
    return;
  mRois.clear();
  emit changed();
}

QVector<RsRoi> RsRoiCollection::roisForClass( int classId ) const
{
  QVector<RsRoi> r;
  for ( const RsRoi &roi : mRois )
  {
    if ( roi.classId() == classId )
      r.append( roi );
  }
  return r;
}

quint64 RsRoiCollection::pixelCountForClass( int classId ) const
{
  quint64 sum = 0;
  for ( const RsRoi &roi : mRois )
  {
    if ( roi.classId() == classId )
      sum += static_cast<quint64>( roi.pixelIndices().size() );
  }
  return sum;
}

void RsRoiCollection::setClassDef( const RsClassDef &d )
{
  mClasses.insert( d.id(), d );
  emit classDefChanged( d.id() );
  emit changed();
}

void RsRoiCollection::setClassDefs( const QHash<int, RsClassDef> &defs )
{
  mClasses = defs;
  emit classDefChanged( -1 );
  emit changed();
}

void RsRoiCollection::clearClassDefs()
{
  if ( mClasses.isEmpty() )
    return;
  mClasses.clear();
  emit classDefChanged( -1 );
  emit changed();
}
