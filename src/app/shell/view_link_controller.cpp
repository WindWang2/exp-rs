/***************************************************************************
 * view_link_controller.cpp — N-view linked extent coordination
 ***************************************************************************/
#include "view_link_controller.h"

#include <qgscoordinatetransform.h>
#include <qgsexception.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrectangle.h>

#include <algorithm>

namespace sicnu::app
{

namespace
{
/// Same throttle class as the dual-viewport sync (~60 FPS rate-limit).
constexpr int kThrottleMs = 16;
constexpr double kScaleRelativeEpsilon = 1e-6;
} // namespace

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
    }
}

QgsMapCanvas *ViewLinkController::canvasFor( sicnu::display::DisplayViewId viewId ) const
{
    if ( !m_displayManager )
        return nullptr;
    return m_displayManager->mapCanvas( viewId );
}

void ViewLinkController::addView( sicnu::display::DisplayViewId viewId )
{
    if ( viewId.isNull() || m_views.contains( viewId ) )
        return;
    if ( m_displayManager && !m_displayManager->view( viewId ) )
        return;
    m_views.append( viewId );
    if ( QgsMapCanvas *canvas = canvasFor( viewId ) )
    {
        connect( canvas, &QgsMapCanvas::extentsChanged, this,
                 [this, viewId] { onExtentChanged( viewId ); }, Qt::UniqueConnection );
    }
}

void ViewLinkController::removeView( sicnu::display::DisplayViewId viewId )
{
    m_linkedViews.removeAll( viewId );
    m_views.removeAll( viewId );
}

void ViewLinkController::onViewAboutToBeRemoved( sicnu::display::DisplayViewId viewId )
{
    // The canvas is still alive here; detach before the manager drops it.
    removeView( viewId );
}

bool ViewLinkController::isLinked( sicnu::display::DisplayViewId viewId ) const
{
    return m_linkedViews.contains( viewId );
}

void ViewLinkController::setLinked( sicnu::display::DisplayViewId viewId, bool linked )
{
    if ( viewId.isNull() || !m_views.contains( viewId ) )
        return;
    if ( linked && !m_linkedViews.contains( viewId ) )
    {
        m_linkedViews.append( viewId );
        // Snap the newly linked view to the first linked peer's viewport so
        // the link starts coherent instead of diverging until the next pan.
        for ( const auto &peer : m_linkedViews )
        {
            if ( peer == viewId )
                continue;
            if ( QgsMapCanvas *peerCanvas = canvasFor( peer ) )
            {
                if ( QgsMapCanvas *canvas = canvasFor( viewId ) )
                    canvas->setExtent( peerCanvas->extent() );
                break;
            }
        }
    }
    else if ( !linked )
    {
        m_linkedViews.removeAll( viewId );
    }
}

void ViewLinkController::setCenterSync( bool on )
{
    mCenterSync = on;
}

void ViewLinkController::setScaleSync( bool on )
{
    mScaleSync = on;
}

void ViewLinkController::onExtentChanged( sicnu::display::DisplayViewId sourceId )
{
    ++mStats.extentEvents;
    if ( mApplying || !mCenterSync || !m_linkedViews.contains( sourceId ) )
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

    mApplying = true;
    QgsRectangle extent = source->extent();
    const QgsCoordinateReferenceSystem sourceCrs = source->mapSettings().destinationCrs();

    for ( const auto &viewId : std::as_const( m_linkedViews ) )
    {
        if ( viewId == sourceId )
            continue;
        QgsMapCanvas *target = canvasFor( viewId );
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
                continue; // untransformable: leave that view alone, honestly
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

} // namespace sicnu::app
