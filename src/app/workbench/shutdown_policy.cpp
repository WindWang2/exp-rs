/***************************************************************************
 * shutdown_policy.cpp — see shutdown_policy.h
 ***************************************************************************/
#include "shutdown_policy.h"

#include "workbench_host.h"

namespace sicnu::app
{

ShutdownPlan planWorkbenchShutdown( const QVector<WorkbenchShutdownFacts> &benchFacts,
                                    int runningTaskCount )
{
    ShutdownPlan plan;
    plan.runningTaskCount = runningTaskCount < 0 ? 0 : runningTaskCount;
    for ( const WorkbenchShutdownFacts &facts : benchFacts )
    {
        if ( facts.dirty )
            plan.dirtyBenches.append( facts.title );
        if ( facts.inFlight )
            plan.inFlightBenches.append( facts.title );
    }
    return plan;
}

QVector<WorkbenchShutdownFacts> collectWorkbenchShutdownFacts( const WorkbenchHost *host )
{
    QVector<WorkbenchShutdownFacts> facts;
    if ( !host )
        return facts;
    for ( const IWorkbench *bench : host->workbenches() )
    {
        if ( !bench )
            continue;
        WorkbenchShutdownFacts entry;
        entry.benchId = bench->id();
        entry.title = bench->title();
        entry.dirty = bench->isDirty();
        entry.inFlight = bench->hasInFlightCompute();
        facts.append( entry );
    }
    return facts;
}

int cancelInFlightBenches( WorkbenchHost *host )
{
    if ( !host )
        return 0;
    int cancelled = 0;
    for ( IWorkbench *bench : host->workbenches() )
    {
        if ( bench && bench->hasInFlightCompute() && bench->requestCancel() )
            ++cancelled;
    }
    return cancelled;
}

bool requestCloseDirtyBenches( WorkbenchHost *host )
{
    if ( !host )
        return true;
    for ( IWorkbench *bench : host->workbenches() )
    {
        if ( bench && bench->isDirty() && !bench->requestClose() )
            return false;
    }
    return true;
}

} // namespace sicnu::app
