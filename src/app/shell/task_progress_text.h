/***************************************************************************
 * task_progress_text.h  —  Task Center progress text rules
 *
 * Split out of rs_job_panel.cpp so the honesty rule is a pure function that
 * can be asserted without linking the whole desktop shell:
 *
 *   A failed or cancelled task NEVER finished. Printing its last partial
 *   percentage next to "Failed" / "Cancelled" reads as "half done" progress
 *   the user may still wait on, so the list/column view refuses to print one.
 *   The DETAIL view may still report how far the task got — it sits directly
 *   next to an explicit "Status: Failed" line — hence observedText().
 ***************************************************************************/
#pragma once

#include "processing/framework/task_center.h"

#include <QString>

namespace sicnu::app::taskprogress
{

/// List/column text: "—" for a task that never finished, "…" while the
/// percentage is unknown, otherwise a clamped percentage.
inline QString columnText( double progress, sicnu::TaskStatus status )
{
    if ( status == sicnu::TaskStatus::Failed || status == sicnu::TaskStatus::Canceled )
        return QStringLiteral( "—" );
    if ( progress < 0.0 )
        return QStringLiteral( "…" );
    if ( progress > 1.0 )
        progress = 1.0;
    return QStringLiteral( "%1%" ).arg( static_cast<int>( progress * 100.0 + 0.5 ) );
}

/// Detail-panel text: the last OBSERVED percentage regardless of status.
inline QString observedText( double progress )
{
    if ( progress < 0.0 )
        return QStringLiteral( "…" );
    if ( progress > 1.0 )
        progress = 1.0;
    return QStringLiteral( "%1%" ).arg( static_cast<int>( progress * 100.0 + 0.5 ) );
}

} // namespace sicnu::app::taskprogress
