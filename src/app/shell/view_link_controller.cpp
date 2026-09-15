/***************************************************************************
 * view_link_controller.cpp — N-view linked extent/cursor coordination
 ***************************************************************************/
#include "view_link_controller.h"

#include <qgscoordinatetransform.h>
#include <qgsexception.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrectangle.h>
#include <qgsvertexmarker.h>

#include <QEvent>

#include <algorithm>

namespace sicnu::app
{

namespace
{
/// Same throttle class as the dual-viewport sync (~60 FPS rate-limit).
constexpr int kThrottleMs = 16;
constexpr double kScaleRelativeEpsilon = 1e-6;
/// History dedupe: extents closer than this relative fraction of the span
/// count as the same viewport and are not recorded twice.
constexpr double kHistoryRelativeEpsilon = 1e-9;
} // namespace

const QString ViewLinkController::kDefaultGroup = QStringLiteral( "default" );

ViewLinkController::ViewLinkController( sicnu::display::QgisDisplayManager *displayManager,
                                        QObject *parent )
    : QObject( parent )
    , m_displayManager( displayManager )
{
    mThrottle.setSingleShot( true );
    mThrottle.setInterval( kThrottleMs );
    connect( &mThrottle, &QTimer::timeout, this, [this] {
        if ( !mPendingSource.isNull() )
            propagateFrom( mPendingSource );
    } );
    if ( m_displayManager )
    {
        connect( m_displayManager, &sicnu::display::QgisDisplayManager::viewAboutToBeRemoved,
                 this, &ViewLinkController::onViewAboutToBeRemoved );
        connect( m_displayManager, &sicnu::display::QgisDisplayManager::activeViewChanged,
                 this, [this]( sicnu::display::DisplayViewId viewId ) { setActiveView( viewId ); } );
    }
}

void ViewLinkController::setActiveView( sicnu::display::DisplayViewId viewId )
{
    mActiveView = viewId;
}

bool ViewLinkController::restoreActiveViewport()
{
    return restorePreviousViewport( mActiveView );
}

QgsMapCanvas *ViewLinkController::canvasFor( sicnu::display::DisplayViewId viewId ) const
{
    if ( !m_displayManager )
        return nullptr;
    return m_displayManager->mapCanvas( viewId );
}

auto ViewLinkController::recordFor( sicnu::display::DisplayViewId viewId ) -> ViewRecord *
{
    for ( auto &record : m_views )
        if ( record.id == viewId )
            return &record;
    return nullptr;
}

auto ViewLinkController::recordFor( sicnu::display::DisplayViewId viewId ) const
  -> const ViewRecord *
{
    for ( const auto &record : m_views )
        if ( record.id == viewId )
            return &record;
    return nullptr;
}

void ViewLinkController::addView( sicnu::display::DisplayViewId viewId )
{
    if ( viewId.isNull() || recordFor( viewId ) )
        return;
    if ( m_displayManager && !m_displayManager->view( viewId ) )
        return;
    m_views.append( ViewRecord{ viewId, QString(), {}, {} } );
    if ( QgsMapCanvas *canvas = canvasFor( viewId ) )
    {
        // Duplicate registration is already rejected above (recordFor guard);
        // Qt::UniqueConnection is illegal for lambdas.
        connect( canvas, &QgsMapCanvas::extentsChanged, this,
                 [this, viewId] { onExtentChanged( viewId ); } );
        connect( canvas, &QgsMapCanvas::xyCoordinates, this,
                 [this, viewId]( const QgsPointXY &p ) { onCursorMoved( viewId, p ); } );
        canvas->installEventFilter( this );
    }
}

void ViewLinkController::removeView( sicnu::display::DisplayViewId viewId )
{
    if ( QgsMapCanvas *canvas = canvasFor( viewId ) )
        canvas->removeEventFilter( this );
    m_views.removeIf( [&viewId]( const ViewRecord &record ) {
        return record.id == viewId;
    } );
}

void ViewLinkController::onViewAboutToBeRemoved( sicnu::display::DisplayViewId viewId )
{
    // The canvas is still alive here; detach before the manager drops it.
    removeView( viewId );
    if ( mActiveView == viewId )
        mActiveView = sicnu::display::DisplayViewId();
    if ( mMarkerOwner == viewId )
    {
        hideAllMarkers();
        mMarkerOwner = sicnu::display::DisplayViewId();
    }
}

// ── Link groups ───────────────────────────────────────────────────────

QVector<sicnu::display::DisplayViewId> ViewLinkController::views() const
{
    QVector<sicnu::display::DisplayViewId> ids;
    ids.reserve( m_views.size() );
    for ( const auto &record : m_views )
        ids.append( record.id );
    return ids;
}

void ViewLinkController::setLinkGroup( sicnu::display::DisplayViewId viewId,
                                       const QString &groupId )
{
    ViewRecord *record = recordFor( viewId );
    if ( !record )
        return;
    record->group = groupId;
    if ( groupId.isEmpty() )
        return;

    // Snap the newly grouped view to the first group peer's viewport so the
    // link starts coherent instead of diverging until the next pan. The
    // applying guard keeps the snap itself from scheduling a propagation
    // pass (it is not user intent).
    for ( const auto &other : std::as_const( m_views ) )
    {
        if ( other.id == viewId || other.group != groupId )
            continue;
        if ( QgsMapCanvas *peerCanvas = canvasFor( other.id ) )
        {
            if ( QgsMapCanvas *canvas = canvasFor( viewId ) )
            {
                mApplying = true;
                canvas->setExtent( peerCanvas->extent() );
                canvas->refresh();
                mApplying = false;
            }
            break;
        }
    }
}

QString ViewLinkController::linkGroup( sicnu::display::DisplayViewId viewId ) const
{
    const ViewRecord *record = recordFor( viewId );
    return record ? record->group : QString();
}

QStringList ViewLinkController::groups() const
{
    QStringList result;
    for ( const auto &record : m_views )
        if ( !record.group.isEmpty() && !result.contains( record.group ) )
            result.append( record.group );
    return result;
}

QVector<sicnu::display::DisplayViewId> ViewLinkController::viewsInGroup(
  const QString &groupId ) const
{
    QVector<sicnu::display::DisplayViewId> result;
    if ( groupId.isEmpty() )
        return result;
    for ( const auto &record : m_views )
        if ( record.group == groupId )
            result.append( record.id );
    return result;
}

bool ViewLinkController::isLinked( sicnu::display::DisplayViewId viewId ) const
{
    const ViewRecord *record = recordFor( viewId );
    return record && !record->group.isEmpty();
}

void ViewLinkController::setLinked( sicnu::display::DisplayViewId viewId, bool linked )
{
    setLinkGroup( viewId, linked ? kDefaultGroup : QString() );
}

void ViewLinkController::setCenterSync( bool on )
{
    mCenterSync = on;
}

void ViewLinkController::setScaleSync( bool on )
{
    mScaleSync = on;
}

void ViewLinkController::setCursorSync( bool on )
{
    mCursorSync = on;
    if ( !on )
        clearCursor();
}

void ViewLinkController::clearCursor()
{
    hideAllMarkers();
    mMarkerOwner = sicnu::display::DisplayViewId();
}

// ── Viewport history ──────────────────────────────────────────────────

void ViewLinkController::recordHistory( ViewRecord &record, QgsMapCanvas *canvas )
{
    if ( !canvas )
        return;
    const QgsRectangle current = canvas->extent();
    // History holds the sequence of DISTINCT viewports (newest first,
    // current included); restorePreviousViewport walks past the current one.
    // The dedupe keeps interactive pan/zoom storms from flooding the ring,
    // and both user pans and incoming group syncs are real history.
    if ( !record.history.isEmpty() )
    {
        const QgsRectangle &last = record.history.front();
        const double ex = kHistoryRelativeEpsilon * std::max( 1.0, std::abs( current.width() ) );
        const double ey = kHistoryRelativeEpsilon * std::max( 1.0, std::abs( current.height() ) );
        if ( qgsDoubleNear( last.xMinimum(), current.xMinimum(), ex )
              && qgsDoubleNear( last.yMinimum(), current.yMinimum(), ey )
              && qgsDoubleNear( last.xMaximum(), current.xMaximum(), ex )
              && qgsDoubleNear( last.yMaximum(), current.yMaximum(), ey ) )
            return;
    }
    if ( current.isEmpty() || !current.isFinite() )
        return;
    record.history.prepend( current );
    if ( record.history.size() > kHistoryCapacity )
        record.history.resize( kHistoryCapacity );
}

int ViewLinkController::historyCount( sicnu::display::DisplayViewId viewId ) const
{
    const ViewRecord *record = recordFor( viewId );
    return record ? record->history.size() : 0;
}

bool ViewLinkController::restorePreviousViewport( sicnu::display::DisplayViewId viewId )
{
    ViewRecord *record = recordFor( viewId );
    QgsMapCanvas *canvas = canvasFor( viewId );
    if ( !record || !canvas )
        return false;
    const QgsRectangle current = canvas->extent();
    const double ex = kHistoryRelativeEpsilon * std::max( 1.0, std::abs( current.width() ) );
    const double ey = kHistoryRelativeEpsilon * std::max( 1.0, std::abs( current.height() ) );
    while ( !record->history.isEmpty() )
    {
        const QgsRectangle candidate = record->history.takeFirst();
        if ( candidate.isEmpty() || !candidate.isFinite() )
            continue;
        // Skip the viewport we are already at (history includes current) so
        // repeated calls walk back one step at a time.
        if ( qgsDoubleNear( candidate.xMinimum(), current.xMinimum(), ex )
              && qgsDoubleNear( candidate.yMinimum(), current.yMinimum(), ey )
              && qgsDoubleNear( candidate.xMaximum(), current.xMaximum(), ex )
              && qgsDoubleNear( candidate.yMaximum(), current.yMaximum(), ey ) )
            continue;
        canvas->setExtent( candidate );
        canvas->refresh();
        ++mStats.restoredViewports;
        return true;
    }
    return false;
}

// ── Extent propagation ────────────────────────────────────────────────

void ViewLinkController::onExtentChanged( sicnu::display::DisplayViewId sourceId )
{
    ++mStats.extentEvents;
    ViewRecord *record = recordFor( sourceId );
    if ( record )
        recordHistory( *record, canvasFor( sourceId ) );
    if ( mApplying || !mCenterSync )
    {
        if ( mApplying )
            ++mStats.suppressedExtents;
        return;
    }
    if ( !record || record->group.isEmpty() )
        return;
    // Coalesce signal storms during interactive pan/zoom: the last source
    // wins after the throttle fires.
    mPendingSource = sourceId;
    if ( !mThrottle.isActive() )
        mThrottle.start();
}

void ViewLinkController::propagateFrom( sicnu::display::DisplayViewId sourceId )
{
    QgsMapCanvas *source = canvasFor( sourceId );
    if ( !source || mApplying )
        return;

    const ViewRecord *record = recordFor( sourceId );
    if ( !record || record->group.isEmpty() )
        return;

    mApplying = true;
    QgsRectangle extent = source->extent();
    const QgsCoordinateReferenceSystem sourceCrs = source->mapSettings().destinationCrs();

    for ( const auto &peer : std::as_const( m_views ) )
    {
        if ( peer.id == sourceId || peer.group != record->group )
            continue;
        QgsMapCanvas *target = canvasFor( peer.id );
        if ( !target )
            continue;

        QgsRectangle targetExtent = extent;
        const QgsCoordinateReferenceSystem targetCrs =
          target->mapSettings().destinationCrs();
        if ( sourceCrs != targetCrs && sourceCrs.isValid() && targetCrs.isValid() )
        {
            try
            {
                QgsCoordinateTransform ct( sourceCrs, targetCrs, QgsProject::instance() );
                targetExtent = ct.transformBoundingBox( targetExtent );
            }
            catch ( const QgsCsException & )
            {
                // Fail closed: leave that view alone, count it honestly.
                ++mStats.transformFailures;
                continue;
            }
        }
        if ( target->extent() != targetExtent )
        {
            target->setExtent( targetExtent );
            target->refresh();
            ++mStats.appliedSyncCount;
        }
        if ( mScaleSync )
        {
            const double sourceScale = source->scale();
            if ( sourceScale > 0.0 && !qgsDoubleNear( target->scale(), sourceScale,
                                                      kScaleRelativeEpsilon * sourceScale ) )
                target->zoomScale( sourceScale );
        }
    }
    mApplying = false;
}

// ── Cursor link ───────────────────────────────────────────────────────

bool ViewLinkController::eventFilter( QObject *watched, QEvent *event )
{
    if ( event->type() == QEvent::Leave )
    {
        for ( const auto &record : m_views )
        {
            if ( m_displayManager && canvasFor( record.id ) == watched )
            {
                onCursorLeft( record.id );
                break;
            }
        }
    }
    return QObject::eventFilter( watched, event );
}

void ViewLinkController::onCursorMoved( sicnu::display::DisplayViewId sourceId,
                                        const QgsPointXY &point )
{
    ++mStats.cursorEvents;
    if ( point.isEmpty() )
        return;
    ViewRecord *record = recordFor( sourceId );
    QgsMapCanvas *canvas = canvasFor( sourceId );
    if ( !record || !canvas )
        return;
    // WKT is cached per CRS: WKT generation on every pointer move would be
    // a needless PROJ/string cost on the hottest path in this controller.
    const QgsCoordinateReferenceSystem crs = canvas->mapSettings().destinationCrs();
    if ( !record->crsCacheValid || !( record->cachedCrs == crs ) )
    {
        record->cachedCrs = crs;
        record->cachedCrsWkt = crs.toWkt( Qgis::CrsWktVariant::Preferred );
        record->crsCacheValid = true;
    }
    emit cursorMoved( sourceId, point, record->cachedCrsWkt );
    if ( !mCursorSync )
        return;
    propagateCursorFrom( sourceId, point );
}

void ViewLinkController::onCursorLeft( sicnu::display::DisplayViewId sourceId )
{
    emit cursorLeft( sourceId );
    if ( mMarkerOwner == sourceId || mMarkerOwner.isNull() )
        clearCursor();
}

void ViewLinkController::hideAllMarkers()
{
    for ( auto &record : m_views )
    {
        if ( record.marker )
            record.marker->hide();
    }
}

void ViewLinkController::propagateCursorFrom( sicnu::display::DisplayViewId sourceId,
                                              const QgsPointXY &point )
{
    const ViewRecord *sourceRecord = recordFor( sourceId );
    if ( !sourceRecord || sourceRecord->group.isEmpty() )
        return;
    QgsMapCanvas *source = canvasFor( sourceId );
    if ( !source )
        return;
    const QgsCoordinateReferenceSystem sourceCrs = source->mapSettings().destinationCrs();

    for ( ViewRecord &peer : m_views )
    {
        if ( peer.id == sourceId || peer.group != sourceRecord->group )
            continue;
        QgsMapCanvas *target = canvasFor( peer.id );
        if ( !target )
            continue;

        QgsPointXY targetPoint = point;
        const QgsCoordinateReferenceSystem targetCrs =
          target->mapSettings().destinationCrs();
        if ( sourceCrs != targetCrs && sourceCrs.isValid() && targetCrs.isValid() )
        {
            try
            {
                QgsCoordinateTransform ct( sourceCrs, targetCrs, QgsProject::instance() );
                targetPoint = ct.transform( targetPoint );
            }
            catch ( const QgsCsException & )
            {
                // Fail closed: no crosshair rather than a wrong location.
                ++mStats.transformFailures;
                continue;
            }
        }
        ++mStats.cursorProjections;
        if ( !mCursorMarkers )
            continue;
        // The marker is a canvas child: the canvas destroys it with itself,
        // and the QPointer catches any intermediate teardown.
        if ( !peer.marker )
        {
            auto *created = new QgsVertexMarker( target );
            created->setIconType( QgsVertexMarker::ICON_CROSS );
            peer.marker = created;
        }
        peer.marker->setCenter( targetPoint );
        peer.marker->show();
    }
    mMarkerOwner = sourceId;
}

} // namespace sicnu::app
