// rs_sample_brush_tool.h — F11 Package B: raster-sample brush painting.
//
// Paints sample features onto a memory polygon sample layer: while the left
// button is held, each move event stamps a disc (radius in LAYER CRS units,
// D7) at the cursor; releasing the button combines every stamp of the stroke
// into ONE multipart feature added under ONE edit command — a whole stroke
// is a single undo step (Oracle O1).
//
// Refusal contract: with no target layer / a layer that is not editable /
// a session-locked layer, the tool does nothing and emits strokeRefused().
// A stroke exceeding kMaxStampsPerStroke coalesces collected stamps into the
// running combined geometry instead of growing without bound (PERFORMANCE).
#pragma once

#include <QPointer>
#include <QVector>

#include <qgsmaptool.h>
#include <qgsgeometry.h>
#include <qgsvectorlayer.h>

class QgsMapCanvas;
class QgsMapMouseEvent;
class QgsRubberBand;
class QgsVectorLayer;
class RsEditSession;

class RsSampleBrushTool : public QgsMapTool
{
    Q_OBJECT

  public:
    /// Stamp tessellation segments per disc (constant, known-answer tested).
    static constexpr int kDiscSegments = 24;
    /// Bounded stamps per stroke; beyond this, stamps coalesce (D10).
    static constexpr int kMaxStampsPerStroke = 2048;

    explicit RsSampleBrushTool( QgsMapCanvas *canvas );
    ~RsSampleBrushTool() override;

    /// Target sample layer (must be a polygon layer). Null clears the target.
    void setTargetLayer( QgsVectorLayer *layer );
    QgsVectorLayer *targetLayer() const { return mLayer.data(); }

    /// Optional session: when set, the target layer must be attached and
    /// unlocked for a stroke to start.
    void setSession( RsEditSession *session ) { mSession = session; }
    RsEditSession *session() const { return mSession.data(); }

    /// Brush radius in LAYER CRS units (D7: unit honesty — no implicit
    /// geographic/plane conversion).
    void setRadius( double radius ) { mRadius = radius; }
    double radius() const { return mRadius; }

    void deactivate() override;

  signals:
    /// One multipart feature added per stroke.
    void strokeCommitted( int featuresAdded );
    void strokeRefused( const QString &reason );

  public:
    // Public so offscreen tests can drive strokes with synthetic events
    // (same contract as the existing RsRoiTool* tools).
    void canvasPressEvent( QgsMapMouseEvent *e ) override;
    void canvasMoveEvent( QgsMapMouseEvent *e ) override;
    void canvasReleaseEvent( QgsMapMouseEvent *e ) override;

  private:
    QgsGeometry discAt( const QgsPointXY &layerPoint ) const;
    bool strokeStartAllowed( QString *reason ) const;
    void finishStroke();
    void cancelStroke();
    QPointer<QgsVectorLayer> mLayer;
    QPointer<RsEditSession> mSession;
    double mRadius = 10.0;

    bool mStroking = false;
    QVector<QgsGeometry> mStamps;
    QgsGeometry mCombined;   // coalesced head of the stroke (cap pressure)
    int mCoalescedCount = 0; // stamps already merged into mCombined
    int mRubberStamps = 0;   // stamps currently drawn in the rubber band
    QgsRubberBand *mRubber = nullptr;
};
