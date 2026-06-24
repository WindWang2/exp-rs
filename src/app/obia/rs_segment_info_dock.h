// rs_segment_info_dock.h — Phase 10B Task 10B.5: dock widget showing
// information about the currently selected segment.
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

    /// Update the display with info about a segment.
    void showSegmentInfo( quint32 segmentId,
                          const RsSegmentFeatures::SegmentStat &stat,
                          int classId = 0 );

    /// Clear the display.
    void clearInfo();

  private:
    QTextEdit *mInfoText = nullptr;
};
