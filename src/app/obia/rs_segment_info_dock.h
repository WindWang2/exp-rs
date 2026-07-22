// rs_segment_info_dock.h — Segment info dock (flat + hierarchy fields).
#pragma once

#include <qgsdockwidget.h>

#include "rs_segment_features.h"

#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

class RsSegmentInfoDock : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit RsSegmentInfoDock( QWidget *parent = nullptr );

    /// Update the display with info about a segment (flat stats).
    void showSegmentInfo( quint32 segmentId,
                          const RsSegmentFeatures::SegmentStat &stat,
                          int classId = 0 );

    /// Hierarchy-aware display: parent / childCount / areaRatio (level-local ids).
    void showSegmentInfo( quint32 segmentId,
                          const RsSegmentFeatures::SegmentStat &stat,
                          int classId,
                          int level,
                          quint32 parentId,
                          int childCount,
                          double areaRatioToParent );

    /// Clear the display.
    void clearInfo();

  private:
    QTextEdit *mInfoText = nullptr;

    static QString formatBaseHtml( quint32 segmentId,
                                   const RsSegmentFeatures::SegmentStat &stat,
                                   int classId );
};
