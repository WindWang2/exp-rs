/***************************************************************************
    rs_twincanvas_sync_controller.h
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
#ifndef RS_TWINCANVAS_SYNC_CONTROLLER_H
#define RS_TWINCANVAS_SYNC_CONTROLLER_H

#include <QObject>
#include <QTimer>

class QgsMapCanvas;

/**
 * \brief Twin-canvas extent synchronization controller.
 *
 * Connects two QgsMapCanvas instances so that user-driven extent changes
 * on one canvas propagate to the other, with a 16ms throttle to coalesce
 * signal storms during panning/zooming and a reentrancy guard to prevent
 * feedback loops.
 *
 * Task 11.4.5.
 */
class RsTwinCanvasSyncController : public QObject
{
    Q_OBJECT

  public:
    RsTwinCanvasSyncController( QgsMapCanvas *src, QgsMapCanvas *ref, QObject *parent = nullptr );

    bool isEnabled() const { return mEnabled; }

  public slots:
    void setEnabled( bool on );

  private slots:
    void onSrcExtentChanged();
    void onRefExtentChanged();

  private:
    QgsMapCanvas *mSrc = nullptr;
    QgsMapCanvas *mRef = nullptr;
    bool mEnabled = true;
    bool mApplying = false; // reentrancy guard
    QTimer mThrottle;       // 16ms coalesce

    enum Pending
    {
      None,
      FromSrc,
      FromRef
    } mPending = None;
};

#endif // RS_TWINCANVAS_SYNC_CONTROLLER_H
