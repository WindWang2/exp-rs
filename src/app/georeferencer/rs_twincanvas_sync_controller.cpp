/***************************************************************************
    rs_twincanvas_sync_controller.cpp
     --------------------------------------
    Date                 : 2026-06-02 (SICNU original)
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "rs_twincanvas_sync_controller.h"

#include "qgsmapcanvas.h"
#include "qgsrectangle.h"

RsTwinCanvasSyncController::RsTwinCanvasSyncController( QgsMapCanvas *src, QgsMapCanvas *ref, QObject *parent )
  : QObject( parent )
  , mSrc( src )
  , mRef( ref )
{
  mThrottle.setSingleShot( true );
  mThrottle.setInterval( 16 ); // ~60 FPS coalesce

  connect( &mThrottle, &QTimer::timeout, this, [this]() {
    if ( !mEnabled || mPending == None )
    {
      mPending = None;
      return;
    }
    if ( !mSrc || !mRef )
    {
      mPending = None;
      return;
    }
    mApplying = true;
    if ( mPending == FromSrc )
      mRef->setExtent( mSrc->extent() );
    else
      mSrc->setExtent( mRef->extent() );
    mRef->refresh();
    mSrc->refresh();
    mApplying = false;
    mPending = None;
  } );

  if ( mSrc )
    connect( mSrc, &QgsMapCanvas::extentsChanged, this, &RsTwinCanvasSyncController::onSrcExtentChanged );
  if ( mRef )
    connect( mRef, &QgsMapCanvas::extentsChanged, this, &RsTwinCanvasSyncController::onRefExtentChanged );
}

void RsTwinCanvasSyncController::setEnabled( bool on )
{
  mEnabled = on;
  if ( !on )
  {
    mThrottle.stop();
    mPending = None;
  }
}

void RsTwinCanvasSyncController::onSrcExtentChanged()
{
  if ( !mEnabled || mApplying )
    return;
  mPending = FromSrc;
  mThrottle.start();
}

void RsTwinCanvasSyncController::onRefExtentChanged()
{
  if ( !mEnabled || mApplying )
    return;
  mPending = FromRef;
  mThrottle.start();
}
