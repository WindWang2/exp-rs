/***************************************************************************
    rs_dual_viewport_sync_controller.cpp
    --------------------------------------
    Dual 1x2 viewport pan/zoom/rotation synchronization controller.
 ***************************************************************************/
#include "rs_dual_viewport_sync_controller.h"

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
    mThrottle.setInterval( 16 ); // ~60 FPS coalesce

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
        if ( mPending == Pending::FromPrimary )
            applyFromPrimary();
        else
            applyFromSecondary();
        mPending = Pending::None;
    } );

    if ( mPrimary )
        connect( mPrimary, &QgsMapCanvas::extentsChanged, this,
                 &RsDualViewportSyncController::onPrimaryExtentChanged );
    if ( mSecondary )
        connect( mSecondary, &QgsMapCanvas::extentsChanged, this,
                 &RsDualViewportSyncController::onSecondaryExtentChanged );
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
    applyFromPrimary();
}

void RsDualViewportSyncController::onPrimaryExtentChanged()
{
    if ( !mEnabled || mApplying )
        return;
    schedule( /*fromPrimary=*/true );
}

void RsDualViewportSyncController::onSecondaryExtentChanged()
{
    if ( !mEnabled || mApplying )
        return;
    schedule( /*fromPrimary=*/false );
}

void RsDualViewportSyncController::schedule( bool fromPrimary )
{
    mPending = fromPrimary ? Pending::FromPrimary : Pending::FromSecondary;
    mThrottle.start();
}

void RsDualViewportSyncController::applyFromPrimary()
{
    if ( !mPrimary || !mSecondary )
        return;
    mApplying = true;
    mSecondary->setExtent( mPrimary->extent() );
    if ( mScaleSync )
    {
        mSecondary->zoomScale( mPrimary->scale() );
        mSecondary->setRotation( mPrimary->rotation() );
    }
    mSecondary->refresh();
    mPrimary->refresh();
    mApplying = false;
}

void RsDualViewportSyncController::applyFromSecondary()
{
    if ( !mPrimary || !mSecondary )
        return;
    mApplying = true;
    mPrimary->setExtent( mSecondary->extent() );
    if ( mScaleSync )
    {
        mPrimary->zoomScale( mSecondary->scale() );
        mPrimary->setRotation( mSecondary->rotation() );
    }
    mPrimary->refresh();
    mSecondary->refresh();
    mApplying = false;
}
