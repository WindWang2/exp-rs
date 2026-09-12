/***************************************************************************
 * marshal_ui.h — worker→UI completion delivery (Workbench 9.0 M0)
 *
 * The shell has several hand-rolled copies of the same async-completion
 * dance: a worker thread finishes (or bails out early) and must update a
 * widget without touching it after destruction or from the wrong thread.
 * This header is the single reviewed form of that pattern:
 *
 *   - marshalTo(receiver, fn) queues fn onto @p receiver's thread. Qt
 *     discards the queued call when the receiver is destroyed before
 *     delivery — no dead-widget callback can run. Posting to the receiver
 *     (instead of qApp + captured QPointer) makes that discard automatic.
 *   - A captured QPointer inside fn stays the guard for anything fn does
 *     beyond the receiver's own members; re-check it when fn outlives the
 *     call site's epoch (superseded-request drops belong at the call site,
 *     together with the per-widget request epoch).
 *
 * Worker-side epoch reads must be atomic: an epoch counter read from a
 * pool thread is a data race unless declared std::atomic (see
 * RoiStatisticsWidget::m_requestEpoch).
 ***************************************************************************/
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QtGlobal>

#include <utility>

namespace sicnu::app::ui_callback
{

/// Queue @p fn onto @p receiver's thread (QueuedConnection semantics).
/// Discarded silently when @p receiver dies before delivery or when it is
/// already null. @p fn must not block and must not re-enter the event loop.
template <typename Fn>
void marshalTo( QObject *receiver, Fn &&fn )
{
  if ( !receiver )
    return;
  QMetaObject::invokeMethod( receiver, std::forward<Fn>( fn ), Qt::QueuedConnection );
}

} // namespace sicnu::app::ui_callback
