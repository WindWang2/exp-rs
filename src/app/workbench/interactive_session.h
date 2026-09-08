/***************************************************************************
 * interactive_session.h — shared interactive session contract (5.0, H)
 *
 * Classification / georeference / OBIA are legitimate interactive
 * exceptions: their *interaction* stays bespoke. What they must share is the
 * lifecycle the rest of the shell sees: dirty state, in-flight compute,
 * cancel routing through the TaskCenter seam, close confirmation and
 * state persistence. This interface is that shared surface — adapters wrap
 * the existing session windows; no session logic moves.
 *
 * Compute law (unchanged): sessions submit through TaskCenter (GuiJobHandle
 * or an equivalent injected executor seam) — never via JobEngine directly;
 * `requestCancel()` forwards to that seam.
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QVariantMap>

namespace sicnu::app
{

class InteractiveSession : public QObject
{
    Q_OBJECT
  public:
    explicit InteractiveSession( QObject *parent = nullptr );
    /// Stable id shared with the owning workbench (e.g. "classify").
    virtual QString sessionId() const = 0;

    /// Unsaved interaction state (samples, GCPs, edits).
    virtual bool isDirty() const = 0;
    virtual void clearDirty() = 0;

    /// True while a TaskCenter-tracked compute is in flight.
    virtual bool hasInFlightCompute() const = 0;

    /// Cancel the in-flight compute through its TaskCenter handle. Returns
    /// false when nothing is running or the backend rejected cancellation —
    /// never blocks.
    virtual bool requestCancel() = 0;

    /// User-facing close: confirms dirty state and refuses when the user
    /// cancels; in-flight compute keeps running unless cancelled explicitly.
    virtual bool requestClose() = 0;

    /// Session-scoped persistence (workflow snapshot, window geometry).
    virtual QVariantMap saveSessionState() const { return QVariantMap(); }
    virtual void restoreSessionState( const QVariantMap &state ) { Q_UNUSED( state ); }

  signals:
    void dirtyChanged( bool dirty );
    void computeStarted();
    void computeFinished( bool success );
};

} // namespace sicnu::app
