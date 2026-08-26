// rs_segment_select_tool.h — Phase 10B Task 10B.5: map tool for selecting
// segments by clicking on the map canvas.
//
// When the user clicks on the map, this tool reads the segment label at
// that pixel from the RsSegmentMap and emits a signal with the segment ID.
// All pixels belonging to the selected segment are highlighted with a
// QgsRubberBand.
#pragma once

#include <qgsmaptool.h>
#include <qgsrubberband.h>

#include "rs_segment_map.h"

#include <QVector>

class QgsMapCanvas;

class RsSegmentSelectTool : public QgsMapTool
{
    Q_OBJECT
  public:
    explicit RsSegmentSelectTool( QgsMapCanvas *canvas );
    ~RsSegmentSelectTool() override;

    /// Set the segment map to use for selection.
    void setSegmentMap( const RsSegmentMap &segMap );

    /// Set the source raster for geo-coordinate → pixel conversion.
    void setGeoTransform( const double gt[6] );

    /// Clear the current selection highlight.
    void clearSelection();

    /// Currently selected segment ID (0 = none).
    quint32 selectedSegmentId() const { return mSelectedSegId; }

  signals:
    /// Emitted when a segment is selected (clicked).
    void segmentSelected( quint32 segmentId );

    /// Emitted when selection is cleared.
    void selectionCleared();

  protected:
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;

  private:
    void highlightSegment( quint32 segmentId );

    RsSegmentMap mSegMap;
    double mGeoTransform[6] = { 0, 1, 0, 0, 0, 1 };
    quint32 mSelectedSegId = 0;
    QgsRubberBand *mRubberBand = nullptr;
};
