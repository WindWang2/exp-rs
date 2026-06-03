// rs_roi_collection.h — Phase 10A: ROI + class definition holder with QObject signals
#pragma once
#include "qgis_analysis_export.h"
#include "rs_roi.h"
#include "rs_class_def.h"
#include <QObject>
#include <QHash>
#include <QVector>

class QGIS_ANALYSIS_EXPORT RsRoiCollection : public QObject
{
    Q_OBJECT
  public:
    explicit RsRoiCollection( QObject *parent = nullptr );

    int size() const { return mRois.size(); }
    const RsRoi &at( int i ) const { return mRois.at( i ); }
    const QVector<RsRoi> &rois() const { return mRois; }
    QVector<RsRoi> roisForClass( int classId ) const;
    quint64 pixelCountForClass( int classId ) const;

    void appendRoi( const RsRoi &roi );
    void removeRoiAt( int i );
    void clear();

    void setClassDef( const RsClassDef &d );
    RsClassDef classDef( int id ) const { return mClasses.value( id ); }
    QHash<int, RsClassDef> classDefs() const { return mClasses; }

  signals:
    void roiAdded( int classId, int roiIndex );
    void roiRemoved( int roiIndex );
    void classDefChanged( int classId );
    void changed();

  private:
    QVector<RsRoi> mRois;
    QHash<int, RsClassDef> mClasses;
};
