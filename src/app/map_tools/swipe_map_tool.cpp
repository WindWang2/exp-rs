/***************************************************************************
 * swipe_map_tool.cpp  —  Swipe (curtain) comparison map tool
 ***************************************************************************/
#include "swipe_map_tool.h"

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmapcanvasitem.h>
#include <qgsmapmouseevent.h>
#include <qgsmaprendererparalleljob.h>
#include <qgsmapsettings.h>
#include <qgsrectangle.h>

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>

namespace {

/**
 * \brief Canvas item that paints a compare-layer snapshot on one side of
 *        a swipe divider and a translucent divider line.
 */
class SwipeCanvasItem : public QgsMapCanvasItem
{
  public:
    explicit SwipeCanvasItem( QgsMapCanvas *canvas )
        : QgsMapCanvasItem( canvas )
    {
        setZValue( 1000 ); // Draw on top of map content
    }

    QRectF boundingRect() const override
    {
        if ( !mMapCanvas )
            return QRectF();
        return QRectF( QPointF( 0, 0 ), mMapCanvas->size() );
    }

    void setSnapshot( const QImage &image ) { prepareGeometryChange(); m_snapshot = image; update(); }
    void setSwipePosition( double fraction ) { m_position = fraction; update(); }
    void setDirection( SwipeMapTool::Direction direction ) { m_direction = direction; update(); }
    void setInverted( bool inverted ) { m_inverted = inverted; update(); }
    bool inverted() const { return m_inverted; }

  protected:
    void paint( QPainter *painter ) override
    {
        if ( !painter || !mMapCanvas )
            return;

        const QSize canvasSize = mMapCanvas->size();
        const int w = canvasSize.width();
        const int h = canvasSize.height();
        if ( w <= 0 || h <= 0 )
            return;

        int splitX = w / 2;
        int splitY = h / 2;
        if ( m_direction == SwipeMapTool::Direction::Vertical )
            splitX = static_cast<int>( m_position * w );
        else
            splitY = static_cast<int>( m_position * h );

        // Draw snapshot on the non-base side
        if ( !m_snapshot.isNull() )
        {
            painter->save();
            if ( m_direction == SwipeMapTool::Direction::Vertical )
            {
                if ( m_inverted )
                    painter->setClipRect( 0, 0, splitX, h );
                else
                    painter->setClipRect( splitX, 0, w - splitX, h );
            }
            else
            {
                if ( m_inverted )
                    painter->setClipRect( 0, 0, w, splitY );
                else
                    painter->setClipRect( 0, splitY, w, h - splitY );
            }
            painter->drawImage( 0, 0, m_snapshot );
            painter->restore();
        }

        // Draw divider line
        painter->save();
        QPen pen( QColor( 255, 80, 80, 200 ) );
        pen.setWidth( 2 );
        painter->setPen( pen );
        if ( m_direction == SwipeMapTool::Direction::Vertical )
            painter->drawLine( splitX, 0, splitX, h );
        else
            painter->drawLine( 0, splitY, w, splitY );
        painter->restore();
    }

  private:
    QImage m_snapshot;
    double m_position = 0.5;
    SwipeMapTool::Direction m_direction = SwipeMapTool::Direction::Vertical;
    bool m_inverted = false;
};

} // anonymous namespace

SwipeMapTool::SwipeMapTool( QgsMapCanvas *canvas )
    : QgsMapTool( canvas )
{
    setCursor( Qt::SplitHCursor );
}

SwipeMapTool::~SwipeMapTool()
{
    // Blocking settle: this destructor can run while layers are being torn
    // down (canvas destruction, window close) — leaving the compare render's
    // worker threads running past this point lets them dereference layers
    // that die after we do (#1389).
    settleRenderJob();
    if ( m_swipeItem )
    {
        delete static_cast<SwipeCanvasItem *>( m_swipeItem );
        m_swipeItem = nullptr;
    }
}

void SwipeMapTool::setBaseLayer( QgsMapLayer *layer )
{
    if ( m_baseDeletionConn )
    {
        disconnect( m_baseDeletionConn );
        m_baseDeletionConn = QMetaObject::Connection();
    }
    m_baseLayer = layer;
    if ( m_baseLayer )
        m_baseDeletionConn = connect( m_baseLayer.data(), &QgsMapLayer::willBeDeleted, this, [this] { onBaseLayerWillBeDeleted(); } );
    m_snapshotDirty = true;
    updateSwipeItem();
}

void SwipeMapTool::setCompareLayer( QgsMapLayer *layer )
{
    if ( m_compareDeletionConn )
    {
        disconnect( m_compareDeletionConn );
        m_compareDeletionConn = QMetaObject::Connection();
    }
    m_compareLayer = layer;
    if ( m_compareLayer )
        m_compareDeletionConn = connect( m_compareLayer.data(), &QgsMapLayer::willBeDeleted, this, [this] { onCompareLayerWillBeDeleted(); } );
    m_snapshotDirty = true;
    updateSwipeItem();
}

void SwipeMapTool::setSwipePosition( double fraction )
{
    fraction = std::clamp( fraction, 0.0, 1.0 );
    if ( qFuzzyCompare( m_swipePosition, fraction ) )
        return;

    m_swipePosition = fraction;
    updateSwipeItem();
    emit swipePositionChanged( m_swipePosition );
}

void SwipeMapTool::setDirection( Direction direction )
{
    if ( m_direction == direction )
        return;

    m_direction = direction;
    setCursor( direction == Direction::Vertical ? Qt::SplitHCursor : Qt::SplitVCursor );
    updateSwipeItem();
    emit directionChanged( m_direction );
}

void SwipeMapTool::canvasMoveEvent( QgsMapMouseEvent *e )
{
    if ( !mCanvas || !m_mouseFollow )
        return;
    const QPoint pos = e->pos();
    const QSize size = mCanvas->size();
    if ( m_direction == Direction::Vertical && size.width() > 0 )
        setSwipePosition( static_cast<double>( pos.x() ) / size.width() );
    else if ( m_direction == Direction::Horizontal && size.height() > 0 )
        setSwipePosition( static_cast<double>( pos.y() ) / size.height() );
}

void SwipeMapTool::canvasPressEvent( QgsMapMouseEvent *e )
{
    m_mouseFollow = true;
    canvasMoveEvent( e );
}

void SwipeMapTool::canvasReleaseEvent( QgsMapMouseEvent *e )
{
    Q_UNUSED( e );
    m_mouseFollow = false;
}

void SwipeMapTool::keyPressEvent( QKeyEvent *e )
{
    if ( e->key() == Qt::Key_Space || e->key() == Qt::Key_D )
    {
        // Toggle direction
        setDirection( m_direction == Direction::Vertical ? Direction::Horizontal : Direction::Vertical );
    }
    else if ( e->key() == Qt::Key_I )
    {
        if ( m_swipeItem )
        {
            auto *item = static_cast<SwipeCanvasItem *>( m_swipeItem );
            // Invert which side shows the compare layer
            item->setInverted( !item->inverted() ); // Note: assuming accessor or logic adjustment
        }
    }
    else if ( e->key() == Qt::Key_V )
    {
        setDirection( Direction::Vertical );
    }
    else if ( e->key() == Qt::Key_H )
    {
        setDirection( Direction::Horizontal );
    }
}

void SwipeMapTool::activate()
{
    QgsMapTool::activate();
    if ( !m_swipeItem )
    {
        m_swipeItem = new SwipeCanvasItem( mCanvas );
        static_cast<SwipeCanvasItem *>( m_swipeItem )->setInverted( false );
    }
    m_swipeItem->setVisible( true );
    m_snapshotDirty = true;
    if ( mCanvas )
    {
        connect( mCanvas, &QgsMapCanvas::extentsChanged, this, [this]() {
            m_snapshotDirty = true;
            updateSwipeItem();
        } );
        connect( mCanvas, &QgsMapCanvas::destinationCrsChanged, this, [this]() {
            m_snapshotDirty = true;
            updateSwipeItem();
        } );
        connect( mCanvas, &QgsMapCanvas::rotationChanged, this, [this]() {
            m_snapshotDirty = true;
            updateSwipeItem();
        } );
    }
    updateSwipeItem();
}

void SwipeMapTool::deactivate()
{
    m_mouseFollow = false;
    cancelRenderJob();
    if ( mCanvas )
    {
        disconnect( mCanvas, &QgsMapCanvas::extentsChanged, this, nullptr );
        disconnect( mCanvas, &QgsMapCanvas::destinationCrsChanged, this, nullptr );
        disconnect( mCanvas, &QgsMapCanvas::rotationChanged, this, nullptr );
    }
    if ( m_swipeItem )
        m_swipeItem->setVisible( false );
    QgsMapTool::deactivate();
}

void SwipeMapTool::updateSwipeItem()
{
    if ( !m_swipeItem || !mCanvas )
        return;

    if ( m_snapshotDirty && m_compareLayer )
        renderCompareSnapshot();

    auto *item = static_cast<SwipeCanvasItem *>( m_swipeItem );
    item->setSnapshot( m_compareSnapshot );
    item->setSwipePosition( m_swipePosition );
    item->setDirection( m_direction );
}

void SwipeMapTool::cancelRenderJob()
{
    if ( !m_renderJob )
        return;
    // Cancel any in-flight job; finished handler checks pointer identity.
    // The job stays REGISTERED with the canvas: its workers keep winding
    // down after cancelWithoutBlocking(), and every settle path (and the
    // destroyed-prune) must still see it until it is truly idle — the
    // handler's identity check already fails, so no state is touched
    // (review R6 P2-1: unregistering here created an untracked orphan).
    disconnect( m_renderJob, nullptr, this, nullptr );
    m_renderJob->cancelWithoutBlocking();
    m_renderJob->deleteLater();
    m_renderJob = nullptr;
}

void SwipeMapTool::settleRenderJob()
{
    if ( !m_renderJob )
        return;
    // Blocking cancel: a parallel job's cancel() joins its worker threads and
    // emits finished() synchronously on this thread; our handler then drops
    // m_renderJob and unregisters. Returns only once no background thread can
    // still touch the layer being destroyed (#1389).
    m_renderJob->cancel();
    // If the finished handler was severed (e.g. mid-cancelRenderJob), the job
    // is idle now but still ours: detach it without re-canceling.
    if ( m_renderJob )
    {
        disconnect( m_renderJob, nullptr, this, nullptr );
        if ( mCanvas )
            mCanvas->unregisterExternalRenderJob( m_renderJob );
        m_renderJob->deleteLater();
        m_renderJob = nullptr;
    }
}

void SwipeMapTool::onBaseLayerWillBeDeleted()
{
    // The base layer is drawn by the canvas, not by our job, but settling is
    // cheap and keeps both deletion tripwires symmetric.
    settleRenderJob();
    m_baseLayer.clear();
}

void SwipeMapTool::onCompareLayerWillBeDeleted()
{
    // Emitted as the first statement of the layer's destructor body: the
    // instance is still fully intact, so joining our render threads here is
    // safe. This is the backstop for deletion paths that bypass the canvas
    // settle (which already cancels this job via the external-job registry).
    settleRenderJob();
    m_compareLayer.clear();
    m_compareSnapshot = QImage();
    m_snapshotDirty = false;
    if ( m_swipeItem )
    {
        auto *item = static_cast<SwipeCanvasItem *>( m_swipeItem );
        item->setSnapshot( m_compareSnapshot );
    }
}

void SwipeMapTool::renderCompareSnapshot()
{
    if ( !m_compareLayer || !mCanvas )
    {
        cancelRenderJob();
        m_compareSnapshot = QImage();
        m_snapshotDirty = false;
        return;
    }

    const QSize size = mCanvas->size();
    if ( size.isEmpty() )
        return;

    // Avoid stacking jobs / blocking the UI thread with waitForFinished().
    cancelRenderJob();

    QgsMapSettings settings = mCanvas->mapSettings();
    settings.setLayers( { m_compareLayer } );
    settings.setOutputSize( size );

    auto *job = new QgsMapRendererParallelJob( settings );
    m_renderJob = job;
    // The settings copy bakes a RAW QgsMapLayer* that the parallel job's
    // worker threads dereference. Register with the canvas so every settle
    // path (DisplayManager/session-workspace layer removal, project close,
    // canvas destruction) blocks on this job before any layer dies (#1389);
    // the willBeDeleted tripwires below catch any path that bypasses it.
    mCanvas->registerExternalRenderJob( job );
    connect( job, &QgsMapRendererJob::finished, this, [this, job]() {
        if ( m_renderJob != job )
            return;
        if ( mCanvas )
            mCanvas->unregisterExternalRenderJob( job );
        m_compareSnapshot = job->renderedImage();
        m_snapshotDirty = false;
        m_renderJob = nullptr;
        job->deleteLater();
        // Refresh swipe item with the new snapshot (do not re-trigger render).
        if ( m_swipeItem )
        {
            auto *item = static_cast<SwipeCanvasItem *>( m_swipeItem );
            item->setSnapshot( m_compareSnapshot );
            item->setSwipePosition( m_swipePosition );
            item->setDirection( m_direction );
        }
    } );
    job->start();
}
