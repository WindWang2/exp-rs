// execution_telemetry.cpp — see execution_telemetry.h for the contract.
#include "execution_telemetry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace sicnu::runtime::observability
{
namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch() ).count();
}

const char *counterName( Counter counter )
{
    switch ( counter )
    {
    case Counter::TasksSubmitted: return "tasks_submitted";
    case Counter::TasksCompleted: return "tasks_completed";
    case Counter::TasksFailed: return "tasks_failed";
    case Counter::TasksCanceled: return "tasks_canceled";
    case Counter::CacheHits: return "cache_hits";
    case Counter::CacheMisses: return "cache_misses";
    case Counter::TilesProcessed: return "tiles_processed";
    case Counter::WorkersSpawned: return "workers_spawned";
    case Counter::WorkersCrashed: return "workers_crashed";
    case Counter::ArtifactsRegistered: return "artifacts_registered";
    case Counter::ArtifactsReclaimed: return "artifacts_reclaimed";
    case Counter::_Count: break;
    }
    return "unknown";
}
} // namespace

ExecutionTelemetry &ExecutionTelemetry::instance()
{
    static ExecutionTelemetry telemetry;
    return telemetry;
}

ExecutionTelemetry::ExecutionTelemetry()
{
    const char *env = std::getenv( "SICNU_TELEMETRY" );
    const bool enabled = env && ( env[0] == '1' || env[0] == 't' || env[0] == 'T' );
    m_enabled.store( enabled, std::memory_order_relaxed );
}

void ExecutionTelemetry::setEnabled( bool on )
{
    m_enabled.store( on, std::memory_order_relaxed );
}

void ExecutionTelemetry::record( const TelemetryEvent &event )
{
    if ( !m_enabled.load( std::memory_order_relaxed ) )
        return;
    std::lock_guard<std::mutex> lock( m_mutex );
    if ( m_events.size() >= kEventCapacity )
    {
        m_events.erase( m_events.begin(),
                        m_events.begin() + ( m_events.size() / 4 ) ); // drop oldest quarter
        m_overflowWarned.store( true, std::memory_order_relaxed );
    }
    TelemetryEvent bounded = event;
    if ( bounded.detail.size() > 512 )
        bounded.detail.resize( 512 );
    m_events.push_back( std::move( bounded ) );
}

void ExecutionTelemetry::recordSimple( EventKind kind, long taskId, int64_t valueNanos,
                                       const std::string &subject )
{
    if ( !m_enabled.load( std::memory_order_relaxed ) )
        return;
    TelemetryEvent event;
    event.timestampMs = nowMs();
    event.kind = kind;
    event.taskId = taskId;
    event.valueNanos = valueNanos;
    event.subject = subject;
    record( event );
}

void ExecutionTelemetry::increment( Counter counter )
{
    if ( counter == Counter::_Count )
        return;
    m_counters[static_cast<size_t>( counter )].fetch_add( 1, std::memory_order_relaxed );
    // Every counter increment is also observable as an event when enabled.
    if ( m_enabled.load( std::memory_order_relaxed ) )
        recordSimple( EventKind::ExecutionEnd, -1, 0, counterName( counter ) );
}

std::map<std::string, uint64_t> ExecutionTelemetry::counters() const
{
    std::map<std::string, uint64_t> out;
    for ( size_t i = 0; i < static_cast<size_t>( Counter::_Count ); ++i )
    {
        const uint64_t value = m_counters[i].load( std::memory_order_relaxed );
        if ( value == 0 )
            continue;
        out[counterName( static_cast<Counter>( i ) )] = value;
    }
    return out;
}

std::vector<TelemetryEvent> ExecutionTelemetry::events() const
{
    std::lock_guard<std::mutex> lock( m_mutex );
    return m_events;
}

std::string ExecutionTelemetry::dumpJson() const
{
    std::string json = "{\"schema\":\"execution-telemetry/1\",\"generatedAt\":";
    json += std::to_string( nowMs() );
    json += ",\"counters\":{";
    bool first = true;
    for ( const auto &[name, value] : counters() )
    {
        if ( !first )
            json += ',';
        first = false;
        json += '"' + name + "\":" + std::to_string( value );
    }
    json += "},\"events\":[";
    first = true;
    for ( const TelemetryEvent &event : events() )
    {
        if ( !first )
            json += ',';
        first = false;
        json += "{\"ts\":" + std::to_string( event.timestampMs );
        json += ",\"kind\":\"" + std::string( eventKindName( event.kind ) ) + "\"";
        json += ",\"taskId\":" + std::to_string( event.taskId );
        json += ",\"pipelineId\":" + std::to_string( event.pipelineId );
        json += ",\"value\":" + std::to_string( event.valueNanos );
        if ( !event.subject.empty() )
            json += ",\"subject\":\"" + event.subject + "\"";
        if ( !event.detail.empty() )
            json += ",\"detail\":\"" + event.detail + "\"";
        json += "}";
    }
    json += "]}";
    return json;
}

std::vector<std::string> ExecutionTelemetry::summaryLines( size_t maxEvents ) const
{
    std::vector<std::string> lines;
    char buffer[256];
    for ( const auto &[name, value] : counters() )
    {
        std::snprintf( buffer, sizeof( buffer ), "%-24s %llu", name.c_str(),
                       static_cast<unsigned long long>( value ) );
        lines.push_back( buffer );
    }
    if ( m_overflowWarned.load( std::memory_order_relaxed ) )
        lines.push_back( "(event log overflowed; oldest entries dropped)" );
    const auto snapshot = events();
    const size_t count = std::min( maxEvents, snapshot.size() );
    lines.push_back( "last events:" );
    for ( size_t i = snapshot.size() - count; i < snapshot.size(); ++i )
    {
        const TelemetryEvent &event = snapshot[i];
        std::snprintf( buffer, sizeof( buffer ), "  [%lld] %-18s task=%ld value=%lld %s",
                       static_cast<long long>( event.timestampMs ),
                       eventKindName( event.kind ), event.taskId,
                       static_cast<long long>( event.valueNanos ),
                       event.subject.c_str() );
        lines.push_back( buffer );
    }
    return lines;
}

void ExecutionTelemetry::clearEvents()
{
    std::lock_guard<std::mutex> lock( m_mutex );
    m_events.clear();
    m_overflowWarned.store( false, std::memory_order_relaxed );
}

ScopedTelemetrySpan::ScopedTelemetrySpan( EventKind kind, long taskId )
    : m_kind( kind ), m_taskId( taskId ), m_start( std::chrono::steady_clock::now() )
{
}

ScopedTelemetrySpan::~ScopedTelemetrySpan()
{
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now() - m_start ).count();
    ExecutionTelemetry::instance().recordSimple( m_kind, m_taskId, nanos );
}

} // namespace sicnu::runtime::observability
