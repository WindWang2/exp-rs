// execution_governor.cpp — see execution_governor.h.
#include "execution_governor.h"

#include "runtime/observability/diagnostic_report.h"
#include "runtime/observability/execution_telemetry.h"

#include <mutex>
#include <string>

namespace sicnu::runtime::exec
{

namespace
{
std::mutex g_lastLeakMutex;
std::string g_lastLeakReportJson;
}

ExecutionGovernor::ExecutionGovernor( Config config )
    : m_config( std::move( config ) ), m_scratch( chunk::ScratchRegistry::Config{ m_config.scratchRoot, m_config.scratchBytes } ),
      m_writeGate( m_config.writeInFlightBytes )
{
}

ExecutionGovernor::~ExecutionGovernor()
{
    if ( hasOutstandingResources() )
    {
        using observability::diagnostics::DiagnosticReport;
        using observability::diagnostics::Recoverability;
        using observability::Counter;
        using observability::ExecutionTelemetry;

        DiagnosticReport report;
        report.code = "execution.resource_leak";
        report.component = "runtime.exec.governor";
        report.recoverability = Recoverability::Manual;
        report.suggestedAction = "release outstanding scratch leases / write-gate "
                                 "reservations before destroying the governor";
        report.causeChain = { "governor destroyed with live resources",
                              "scratch outstanding "
                                  + std::to_string( m_scratch.outstandingBytes() ) + " B" };
        report.artifacts = { m_scratch.root() };
        {
            std::lock_guard<std::mutex> leakLock( g_lastLeakMutex );
            g_lastLeakReportJson = report.toJson();
        }
        ExecutionTelemetry::instance().increment( Counter::ResourceLeaksDetected );
        observability::TelemetryEvent event;
        event.kind = observability::EventKind::ResourceWait;
        event.subject = "exec.governor";
        event.detail = "leak: scratch outstanding "
                       + std::to_string( m_scratch.outstandingBytes() ) + " B";
        ExecutionTelemetry::instance().record( event );
    }
}

chunk::TileMemoryPlan ExecutionGovernor::admitOrRefuse(
    const chunk::TileMemoryRequest &request ) const
{
    chunk::TileMemoryRequest governed = request;
    if ( m_config.ramBytes > 0 )
        governed.budgetBytes = m_config.ramBytes;
    if ( m_config.scratchBytes > 0 )
        governed.scratchBudgetBytes = m_config.scratchBytes;

    const chunk::TileMemoryPlan plan = chunk::planTileMemory( governed );
    if ( plan.action == chunk::TileMemoryPlan::Action::Refuse )
        throw AdmissionRefused( plan.reason );
    return plan;
}

chunk::TileMemoryPlan ExecutionGovernor::advise( const chunk::TileMemoryRequest &request ) const
{
    chunk::TileMemoryRequest governed = request;
    if ( m_config.ramBytes > 0 )
        governed.budgetBytes = m_config.ramBytes;
    return chunk::planTileMemory( governed );
}

bool ExecutionGovernor::hasOutstandingResources() const
{
    return m_scratch.outstandingBytes() > 0 || m_writeGate.outstandingBytes() > 0;
}

std::string ExecutionGovernor::lastLeakReportJson()
{
    std::lock_guard<std::mutex> leakLock( g_lastLeakMutex );
    return g_lastLeakReportJson;
}

} // namespace sicnu::runtime::exec
