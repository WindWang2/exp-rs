/***************************************************************************
    rs_dual_viewport_sync_controller.cpp
    --------------------------------------
    Dual 1x2 viewport pan/zoom/rotation synchronization controller.
 ***************************************************************************/
#include "rs_dual_viewport_sync_controller.h"

#include "qgis.h"
#include "qgsmapcanvas.h"
#include "qgsrectangle.h"

RsDualViewportSyncController::RsDualViewportSyncController( QgsMapCanvas *primary,
                                                            QgsMapCanvas *secondary,
                                                            QObject *parent )
  : QObject( parent )
  , mPrimary( primary )
  , mSecondary( secondary )
{
    mThrottle.setSingleShot( true );
    mThrottle.setInterval( 16 ); // ~60 FPS coalesce rate limit

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
    ++mStats.extentChangedEvents;
    if ( !mEnabled || mApplying || !mPrimary || !mSecondary )
        return;
    schedule( /*fromPrimary=*/true );
}

void RsDualViewportSyncController::onSecondaryExtentChanged()
{
    ++mStats.extentChangedEvents;
    if ( !mEnabled || mApplying || !mPrimary || !mSecondary )
        return;
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

void RsDualViewportSyncController::applyFromPrimary()
{
    if ( !mPrimary || !mSecondary || mApplying )
        return;

    mApplying = true;
    ++mStats.appliedSyncCount;
    bool targetModified = false;

    // 1. Rotation sync (if scaleSync/rotation enabled and rotation differs)
    if ( mScaleSync )
    {
        const double primaryRot = mPrimary->rotation();
        if ( !qgsDoubleNear( mSecondary->rotation(), primaryRot, 1e-5 ) )
        {
            mSecondary->setRotation( primaryRot );
            targetModified = true;
        }
    }

    // 2. Extent sync (if extent differs)
    const QgsRectangle primaryExtent = mPrimary->extent();
    if ( mSecondary->extent() != primaryExtent )
    {
        mSecondary->setExtent( primaryExtent );
        targetModified = true;
    }

    // 3. Scale sync (if scale sync enabled and scale differs beyond floating point precision)
    if ( mScaleSync )
    {
        const double primaryScale = mPrimary->scale();
        const double secondaryScale = mSecondary->scale();
        if ( primaryScale > 0.0 && !qgsDoubleNear( secondaryScale, primaryScale, 1e-4 * primaryScale ) )
        {
            mSecondary->zoomScale( primaryScale );
            targetModified = true;
        }
    }

    // Refresh ONLY the secondary (target) canvas if modified.
    // NEVER refresh the primary (source) canvas.
    if ( targetModified )
    {
        ++mStats.canvasRefreshRequests;
        mSecondary->refresh();
    }

    mApplying = false;
}

void RsDualViewportSyncController::applyFromSecondary()
{
    if ( !mPrimary || !mSecondary || mApplying )
        return;

    mApplying = true;
    ++mStats.appliedSyncCount;
    bool targetModified = false;

    // 1. Rotation sync (if scaleSync/rotation enabled and rotation differs)
    if ( mScaleSync )
    {
        const double secondaryRot = mSecondary->rotation();
        if ( !qgsDoubleNear( mPrimary->rotation(), secondaryRot, 1e-5 ) )
        {
            mPrimary->setRotation( secondaryRot );
            targetModified = true;
        }
    }

    // 2. Extent sync (if extent differs)
    const QgsRectangle secondaryExtent = mSecondary->extent();
    if ( mPrimary->extent() != secondaryExtent )
    {
        mPrimary->setExtent( secondaryExtent );
        targetModified = true;
    }

    // 3. Scale sync (if scale sync enabled and scale differs beyond floating point precision)
    if ( mScaleSync )
    {
        const double secondaryScale = mSecondary->scale();
        const double primaryScale = mPrimary->scale();
        if ( secondaryScale > 0.0 && !qgsDoubleNear( primaryScale, secondaryScale, 1e-4 * secondaryScale ) )
        {
            mPrimary->zoomScale( secondaryScale );
            targetModified = true;
        }
    }

    // Refresh ONLY the primary (target) canvas if modified.
    // NEVER refresh the secondary (source) canvas.
    if ( targetModified )
    {
        ++mStats.canvasRefreshRequests;
        mPrimary->refresh();
    }

    mApplying = false;
}
