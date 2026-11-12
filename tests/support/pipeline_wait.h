#pragma once

#include "workflow/pipeline_run_coordinator.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>

#include <cstdio>

namespace sicnu_test
{

// Call on the application's event-loop thread. Keep a real event loop so
// deferred deletion and queued worker signals have the same semantics as
// the application's event loop.
inline bool waitForPipelineCompleted( sicnu::workflow::PipelineRunCoordinator &coordinator,
                                      int timeoutMs )
{
    if ( coordinator.hasCompleted() )
        return true;
    QSignalSpy spy( &coordinator, &sicnu::workflow::PipelineRunCoordinator::pipelineCompleted );
    QEventLoop loop;
    QObject::connect( &coordinator, &sicnu::workflow::PipelineRunCoordinator::pipelineCompleted,
                      &loop, &QEventLoop::quit, Qt::QueuedConnection );
    // Close the completion-before-connect window before entering exec().
    if ( coordinator.hasCompleted() )
        return true;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer::singleShot( timeoutMs, &loop, &QEventLoop::quit );
    loop.exec();
    const bool completed = spy.count() >= 1 || coordinator.hasCompleted();
    if ( !completed )
        std::fprintf( stderr,
                      "[sicnu_test::waitForPipelineCompleted] timed out after %lld ms (budget %d ms); "
                      "expected: pipeline completed; actual: hasCompleted=false\n",
                      static_cast<long long>( elapsed.elapsed() ), timeoutMs );
    return completed;
}

} // namespace sicnu_test
