/***************************************************************************
    rs_dual_viewport_sync_controller.cpp
    --------------------------------------
    Dual 1x2 viewport pan/zoom/rotation synchronization controller.
 ***************************************************************************/
#include "rs_dual_viewport_sync_controller.h"

#include "qgis.h"
#include "qgsmapcanvas.h"
#include "qgsrectangle.h"

namespace
{
    using namespace std::chrono_literals;
    constexpr auto kThrottleInterval = 16ms;      // ~60 FPS coalesce rate limit
    constexpr double kRotationEpsilon = 1e-5;     // Rotation tolerance in degrees
    constexpr double kScaleRelativeEpsilon = 1e-4;// Relative scale tolerance
    constexpr double kCoordEpsilon = 1e-6;        // Coordinate comparison tolerance
}

RsDualViewportSyncController::RsDualViewportSyncController( QgsMapCanvas *primary,
                                                            QgsMapCanvas *secondary,
                                                            QObject *parent )
  : QObject( parent )
  , mPrimary( primary )
  , mSecondary( secondary )
{
    mThrottle.setSingleShot( true );
    mThrottle.setInterval( kThrottleInterval );

    connect( &mThrottle, &QTimer::timeout, this, [this]() {
        if ( !mEnabled || mPending == Pending::None )
        {
            mPending = Pending::None;
            return;
        }
        if ( !mPrimary || !mSecondary )
        {
            mPending = Pending::None;
            return;
        }
        const Pending pending = mPending;
        mPending = Pending::None;

        if ( pending == Pending::FromPrimary )
            applyFromPrimary();
        else if ( pending == Pending::FromSecondary )
            applyFromSecondary();
    } );

    if ( mPrimary )
    {
        connect( mPrimary.data(), &QgsMapCanvas::extentsChanged, this,
                 &RsDualViewportSyncController::onPrimaryExtentChanged );
        connect( mPrimary.data(), &QObject::destroyed, this,
                 &RsDualViewportSyncController::onCanvasDestroyed );
    }
    if ( mSecondary )
    {
        connect( mSecondary.data(), &QgsMapCanvas::extentsChanged, this,
                 &RsDualViewportSyncController::onSecondaryExtentChanged );
        connect( mSecondary.data(), &QObject::destroyed, this,
                 &RsDualViewportSyncController::onCanvasDestroyed );
    }
}

void RsDualViewportSyncController::onCanvasDestroyed( QObject *obj )
{
    Q_UNUSED( obj )
    mThrottle.stop();
    mPending = Pending::None;
}

void RsDualViewportSyncController::setEnabled( bool on )
{
    mEnabled = on;
    if ( !on )
    {
        mThrottle.stop();
        mPending = Pending::None;
    }
}

void RsDualViewportSyncController::setScaleSync( bool on )
{
    mScaleSync = on;
}

void RsDualViewportSyncController::snapSecondaryToPrimary()
{
    if ( !mEnabled || !mPrimary || !mSecondary )
        return;
    mThrottle.stop();
    mPending = Pending::None;
    applyFromPrimary();
}

void RsDualViewportSyncController::onPrimaryExtentChanged()
{
    if ( !mEnabled || mApplying || !mPrimary || !mSecondary )
        return;
    ++mStats.extentChangedEvents;
    schedule( /*fromPrimary=*/true );
}

void RsDualViewportSyncController::onSecondaryExtentChanged()
{
    if ( !mEnabled || mApplying || !mPrimary || !mSecondary )
        return;
    ++mStats.extentChangedEvents;
    schedule( /*fromPrimary=*/false );
}

void RsDualViewportSyncController::schedule( bool fromPrimary )
{
    mPending = fromPrimary ? Pending::FromPrimary : Pending::FromSecondary;
    // Keep single-shot timer ticking if already active to guarantee a frame delivery
    // every ~16ms without unbounded debounce starvation under continuous updates.
    if ( !mThrottle.isActive() )
    {
        mThrottle.start();
    }
}

void RsDualViewportSyncController::applySync( QgsMapCanvas *source, QgsMapCanvas *target )
{
    if ( !source || !target || mApplying )
        return;

    mApplying = true;
    ++mStats.appliedSyncCount;
    bool targetModified = false;

    // 1. Rotation sync (if scale/rotation sync enabled and rotation differs)
    if ( mScaleSync )
    {
        const double sourceRot = source->rotation();
        if ( !qgsDoubleNear( target->rotation(), sourceRot, kRotationEpsilon ) )
        {
            target->setRotation( sourceRot );
            targetModified = true;
        }
    }

    // Guard against canvas destruction during synchronous signal dispatch
    if ( !mPrimary || !mSecondary )
    {
        mApplying = false;
        return;
    }

    // 2. Extent & Scale sync
    if ( mScaleSync )
    {
        // Full extent sync
        const QgsRectangle sourceExtent = source->extent();
        if ( target->extent() != sourceExtent )
        {
            target->setExtent( sourceExtent );
            targetModified = true;
        }

        // Guard against canvas destruction during setExtent signal cascade
        if ( !mPrimary || !mSecondary )
        {
            mApplying = false;
            return;
        }

        // Scale sync
        const double sourceScale = source->scale();
        const double targetScale = target->scale();
        if ( sourceScale > 0.0 && !qgsDoubleNear( targetScale, sourceScale, kScaleRelativeEpsilon * sourceScale ) )
        {
            target->zoomScale( sourceScale );
            targetModified = true;
        }
    }
    else
    {
        // Pan-center only sync: preserve target canvas's independent zoom scale / dimensions
        const QgsPointXY sourceCenter = source->extent().center();
        const QgsRectangle currentTargetExtent = target->extent();
        const QgsPointXY currentTargetCenter = currentTargetExtent.center();
        const double dx = sourceCenter.x() - currentTargetCenter.x();
        const double dy = sourceCenter.y() - currentTargetCenter.y();
        if ( !qgsDoubleNear( dx, 0.0, kCoordEpsilon ) ||
             !qgsDoubleNear( dy, 0.0, kCoordEpsilon ) )
        {
            const QgsRectangle shiftedExtent(
                currentTargetExtent.xMinimum() + dx,
                currentTargetExtent.yMinimum() + dy,
                currentTargetExtent.xMaximum() + dx,
                currentTargetExtent.yMaximum() + dy
            );
            target->setExtent( shiftedExtent, true );
            targetModified = true;
        }
    }

    // Guard against canvas destruction before refresh
    if ( !mPrimary || !mSecondary )
    {
        mApplying = false;
        return;
    }

    // Refresh ONLY the target canvas if modified. NEVER refresh the source canvas.
    if ( targetModified )
    {
        ++mStats.canvasRefreshRequests;
        target->refresh();
    }

    mApplying = false;
}

void RsDualViewportSyncController::applyFromPrimary()
{
    applySync( mPrimary.data(), mSecondary.data() );
}

void RsDualViewportSyncController::applyFromSecondary()
{
    applySync( mSecondary.data(), mPrimary.data() );
}
