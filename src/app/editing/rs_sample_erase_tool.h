// rs_sample_erase_tool.h — F11 Package B: raster-sample erase painting.
//
// Removes whole sample features intersecting the brush stamps of one stroke
// (D7: whole-feature erase — split-on-erase would silently redefine sample
// geometry). All removals of a stroke happen under ONE edit command: a
// single undo restores every erased feature (Oracle O1).
//
// Refusal contract mirrors RsSampleBrushTool.
#pragma once

#include <QPointer>
#include <QSet>
#include <QVector>

#include <qgsmaptool.h>
#include <qgsgeometry.h>
#include <qgsvectorlayer.h>

class QgsMapCanvas;
class QgsMapMouseEvent;
class QgsRubberBand;
class QgsVectorLayer;
class RsEditSession;

class RsSampleEraseTool : public QgsMapTool
{
    Q_OBJECT

  public:
    static constexpr int kDiscSegments = 24;
    static constexpr int kMaxStampsPerStroke = 2048;

    explicit RsSampleEraseTool( QgsMapCanvas *canvas );
    ~RsSampleEraseTool() override;

    void setTargetLayer( QgsVectorLayer *layer );
    QgsVectorLayer *targetLayer() const { return mLayer.data(); }
    void setSession( RsEditSession *session ) { mSession = session; }
    RsEditSession *session() const { return mSession.data(); }
    /// Radius in LAYER CRS units (same unit contract as the brush).
    void setRadius( double radius ) { mRadius = radius; }
    double radius() const { return mRadius; }

    void deactivate() override;

  signals:
    void strokeCommitted( int featuresRemoved );
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
    void collectHits( const QgsGeometry &stamp );
    void finishStroke();
    void cancelStroke();

    QPointer<QgsVectorLayer> mLayer;
    QPointer<RsEditSession> mSession;
    double mRadius = 10.0;

    bool mStroking = false;
    QVector<QgsGeometry> mStamps;
    QSet<QgsFeatureId> mHitIds;
    int mStampCount = 0; // includes coalesced stamps (cap accounting)
    QgsRubberBand *mRubber = nullptr;
};
