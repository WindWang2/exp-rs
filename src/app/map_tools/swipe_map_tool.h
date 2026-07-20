/***************************************************************************
 * swipe_map_tool.h  —  Swipe (curtain) comparison map tool
 ***************************************************************************/
#pragma once

#include <qgsmaptool.h>
#include <qgspointxy.h>
#include <qgsmaplayer.h>

#include <QPointer>

class QgsMapCanvas;
class QgsMapLayer;
class QgsMapCanvasItem;
class QgsMapRendererParallelJob;
class QSlider;

/**
 * \brief Map tool that renders a swipe/curtain comparison between two layers.
 *
 * The tool draws a vertical or horizontal divider across the canvas.  One
 * side displays the base layer (rendered normally by the canvas) and the
 * other side displays a snapshot of the comparison layer, clipped to the
 * divider.  The divider follows the mouse or can be set programmatically.
 *
 * This is intended for quick visual QA of before/after processing results.
 */
class SwipeMapTool : public QgsMapTool
{
    Q_OBJECT

  public:
    enum class Direction
    {
        Vertical,   //!< Left/right split
        Horizontal  //!< Top/bottom split
    };

    explicit SwipeMapTool( QgsMapCanvas *canvas );
    ~SwipeMapTool() override;

    /**
     * Set the layer rendered by the map canvas on one side of the divider.
     */
    void setBaseLayer( QgsMapLayer *layer );

    /**
     * Set the layer rendered as a snapshot on the other side of the divider.
     */
    void setCompareLayer( QgsMapLayer *layer );

    QgsMapLayer *baseLayer() const { return m_baseLayer; }
    QgsMapLayer *compareLayer() const { return m_compareLayer; }

    /**
     * Divider position as a fraction [0.0, 1.0] across the canvas.
     */
    void setSwipePosition( double fraction );
    double swipePosition() const { return m_swipePosition; }

    /**
     * Split direction.
     */
    void setDirection( Direction direction );
    Direction direction() const { return m_direction; }

    /**
     * Enable/disable mouse-follow mode.  When enabled the divider tracks
     * the mouse position; when disabled it stays at the current position.
     */
    void setMouseFollow( bool follow ) { m_mouseFollow = follow; }
    bool mouseFollow() const { return m_mouseFollow; }

    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;
    void keyPressEvent( QKeyEvent *e ) override;
    void activate() override;
    void deactivate() override;

  signals:
    void swipePositionChanged( double fraction );
    void directionChanged( Direction direction );

  private:
    void updateSwipeItem();
    void renderCompareSnapshot();
    void cancelRenderJob();

    QPointer<QgsMapLayer> m_baseLayer;
    QPointer<QgsMapLayer> m_compareLayer;

    double m_swipePosition = 0.5;
    Direction m_direction = Direction::Vertical;
    bool m_mouseFollow = true;

    class QgsMapCanvasItem *m_swipeItem = nullptr;
    QImage m_compareSnapshot;
    bool m_snapshotDirty = true;

    //! Async compare-layer render (avoids blocking the UI thread).
    QgsMapRendererParallelJob *m_renderJob = nullptr;
};
