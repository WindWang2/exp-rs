// execution_governor.cpp — see execution_governor.h.
#include "execution_governor.h"

#include "runtime/observability/diagnostic_report.h"
#include "runtime/observability/execution_telemetry.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

#if defined( __linux__ )
#include <unistd.h>
#elif defined( __APPLE__ )
#include <mach/mach.h>
#include <mach/task_info.h>
#elif defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace sicnu::runtime::exec
{

namespace
{
std::mutex g_lastLeakMutex;
std::string g_lastLeakReportJson;

std::uint64_t defaultCurrentRssBytes()
{
#if defined( __linux__ )
    std::ifstream statm( "/proc/self/statm" );
    long pagesTotal = 0;
    long pagesResident = 0;
    if ( statm >> pagesTotal >> pagesResident && pagesResident > 0 )
    {
        static const long kPageSize = [] {
            const long ps = ::sysconf( _SC_PAGESIZE );
            return ps > 0 ? ps : 4096;
        }();
        return static_cast<std::uint64_t>( pagesResident ) * static_cast<std::uint64_t>( kPageSize );
    }
    return 0;
#elif defined( __APPLE__ )
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if ( task_info( mach_task_self(), MACH_TASK_BASIC_INFO,
                    reinterpret_cast<task_info_t>( &info ), &count ) == KERN_SUCCESS )
    {
        return static_cast<std::uint64_t>( info.resident_size );
    }
    return 0;
#elif defined( _WIN32 )
    PROCESS_MEMORY_COUNTERS pmc;
    std::memset( &pmc, 0, sizeof( pmc ) );
    pmc.cb = sizeof( pmc );
    if ( ::GetProcessMemoryInfo( ::GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
        return static_cast<std::uint64_t>( pmc.WorkingSetSize );
    return 0;
#else
    return 0;
#endif
}
} // namespace

// ── TileMemoryPool::PoolState ────────────────────────────────────────────────

struct TileMemoryPool::PoolState
{
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> poolAlive{ true };

    std::uint64_t maxAllocatedBytes = 0;
    std::uint64_t maxPoolBytes = 0;

    std::uint64_t allocatedBytes = 0;
    std::uint64_t pooledBytes = 0;

    std::uint64_t totalAllocations = 0;
    std::uint64_t poolHits = 0;
    std::uint64_t poolMisses = 0;

    std::vector<std::unique_ptr<std::vector<float>>> freePool;
};

// ── TileMemoryPool ───────────────────────────────────────────────────────────

TileMemoryPool::TileMemoryPool()
    : TileMemoryPool( Config{} )
{
}

TileMemoryPool::TileMemoryPool( Config config )
    : m_config( config ), m_state( std::make_shared<PoolState>() )
{
    m_state->maxAllocatedBytes = m_config.maxAllocatedBytes;
    m_state->maxPoolBytes = m_config.maxPoolBytes;
}

TileMemoryPool::~TileMemoryPool()
{
    if ( m_state )
    {
        {
            std::lock_guard<std::mutex> lock( m_state->mutex );
            m_state->poolAlive.store( false, std::memory_order_release );
            m_state->freePool.clear();
            m_state->pooledBytes = 0;
        }
        m_state->cv.notify_all();
    }
}

std::shared_ptr<std::vector<float>> TileMemoryPool::acquireBuffer(
    std::size_t elementCount,
    const std::atomic<bool> *cancelFlag,
    std::chrono::milliseconds timeout )
{
    const std::uint64_t requestedBytes =
        static_cast<std::uint64_t>( elementCount ) * sizeof( float );

    std::unique_lock<std::mutex> lock( m_state->mutex );

    auto canAdmit = [&] {
        return m_state->maxAllocatedBytes == 0 ||
               m_state->allocatedBytes == 0 ||
               ( m_state->allocatedBytes + requestedBytes <= m_state->maxAllocatedBytes );
    };

    if ( !canAdmit() )
    {
        const auto startTime = std::chrono::steady_clock::now();
        while ( !canAdmit() )
        {
            if ( cancelFlag && cancelFlag->load( std::memory_order_relaxed ) )
                throw chunk::ChunkCancelled( "tile memory allocation cancelled" );
            if ( !m_state->poolAlive.load( std::memory_order_relaxed ) )
                throw chunk::ChunkCancelled( "tile memory pool shutting down" );

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime );
            if ( elapsed >= timeout )
                return nullptr;

            const auto step = std::min( std::chrono::milliseconds( 10 ), timeout - elapsed );
            m_state->cv.wait_for( lock, step );
        }
    }

    m_state->allocatedBytes += requestedBytes;
    m_state->totalAllocations++;

    // Locate best-fitting buffer in free pool (smallest capacity >= elementCount)
    std::unique_ptr<std::vector<float>> vec;
    std::size_t bestIdx = std::numeric_limits<std::size_t>::max();
    std::size_t bestCap = std::numeric_limits<std::size_t>::max();

    for ( std::size_t i = 0; i < m_state->freePool.size(); ++i )
    {
        const std::size_t cap = m_state->freePool[i]->capacity();
        if ( cap >= elementCount && cap < bestCap )
        {
            bestCap = cap;
            bestIdx = i;
            if ( cap == elementCount )
                break;
        }
    }

    if ( bestIdx != std::numeric_limits<std::size_t>::max() )
    {
        vec = std::move( m_state->freePool[bestIdx] );
        m_state->freePool[bestIdx] = std::move( m_state->freePool.back() );
        m_state->freePool.pop_back();

        const std::uint64_t capBytes =
            static_cast<std::uint64_t>( vec->capacity() ) * sizeof( float );
        if ( m_state->pooledBytes >= capBytes )
            m_state->pooledBytes -= capBytes;
        else
            m_state->pooledBytes = 0;

        m_state->poolHits++;
        lock.unlock();

        vec->resize( elementCount );
        std::memset( vec->data(), 0, elementCount * sizeof( float ) );
    }
    else
    {
        m_state->poolMisses++;
        lock.unlock();

        try
        {
            vec = std::make_unique<std::vector<float>>( elementCount, 0.0f );
        }
        catch ( ... )
        {
            std::lock_guard<std::mutex> rollbackLock( m_state->mutex );
            if ( m_state->allocatedBytes >= requestedBytes )
                m_state->allocatedBytes -= requestedBytes;
            else
                m_state->allocatedBytes = 0;
            m_state->cv.notify_all();
            throw;
        }
    }

    std::weak_ptr<PoolState> weakState = m_state;
    const std::uint64_t trackedBytes = requestedBytes;

    return std::shared_ptr<std::vector<float>>(
        vec.release(),
        [weakState, trackedBytes]( std::vector<float> *raw ) {
            if ( !raw )
                return;

            if ( auto state = weakState.lock() )
            {
                std::unique_lock<std::mutex> lock( state->mutex );
                if ( state->allocatedBytes >= trackedBytes )
                    state->allocatedBytes -= trackedBytes;
                else
                    state->allocatedBytes = 0;

                const bool alive = state->poolAlive.load( std::memory_order_relaxed );
                const std::uint64_t capBytes =
                    static_cast<std::uint64_t>( raw->capacity() ) * sizeof( float );

                if ( alive && ( state->maxPoolBytes == 0 ||
                                ( state->pooledBytes + capBytes <= state->maxPoolBytes ) ) )
                {
                    state->pooledBytes += capBytes;
                    state->freePool.push_back( std::unique_ptr<std::vector<float>>( raw ) );
                    lock.unlock();
                    state->cv.notify_all();
                    return;
                }

                lock.unlock();
                state->cv.notify_all();
                delete raw;
            }
            else
            {
                delete raw;
            }
        } );
}

std::shared_ptr<std::vector<float>> TileMemoryPool::tryAcquireBuffer( std::size_t elementCount )
{
    return acquireBuffer( elementCount, nullptr, std::chrono::milliseconds( 0 ) );
}

std::shared_ptr<std::vector<float>> TileMemoryPool::adopt(
    std::vector<float> &&vec,
    const std::atomic<bool> *cancelFlag )
{
    const std::uint64_t requestedBytes =
        static_cast<std::uint64_t>( vec.capacity() ) * sizeof( float );

    std::unique_lock<std::mutex> lock( m_state->mutex );
    auto canAdmit = [&] {
        return m_state->maxAllocatedBytes == 0 ||
               m_state->allocatedBytes == 0 ||
               ( m_state->allocatedBytes + requestedBytes <= m_state->maxAllocatedBytes );
    };

    while ( !canAdmit() )
    {
        if ( cancelFlag && cancelFlag->load( std::memory_order_relaxed ) )
            throw chunk::ChunkCancelled( "tile memory allocation cancelled" );
        if ( !m_state->poolAlive.load( std::memory_order_relaxed ) )
            throw chunk::ChunkCancelled( "tile memory pool shutting down" );

        m_state->cv.wait_for( lock, std::chrono::milliseconds( 10 ) );
    }

    m_state->allocatedBytes += requestedBytes;
    m_state->totalAllocations++;
    lock.unlock();

    auto raw = new std::vector<float>( std::move( vec ) );
    std::weak_ptr<PoolState> weakState = m_state;

    return std::shared_ptr<std::vector<float>>(
        raw,
        [weakState, requestedBytes]( std::vector<float> *ptr ) {
            if ( !ptr )
                return;
            if ( auto state = weakState.lock() )
            {
                std::unique_lock<std::mutex> lock( state->mutex );
                if ( state->allocatedBytes >= requestedBytes )
                    state->allocatedBytes -= requestedBytes;
                else
                    state->allocatedBytes = 0;

                const bool alive = state->poolAlive.load( std::memory_order_relaxed );
                const std::uint64_t capBytes =
                    static_cast<std::uint64_t>( ptr->capacity() ) * sizeof( float );

                if ( alive && ( state->maxPoolBytes == 0 ||
                                ( state->pooledBytes + capBytes <= state->maxPoolBytes ) ) )
                {
                    state->pooledBytes += capBytes;
                    state->freePool.push_back( std::unique_ptr<std::vector<float>>( ptr ) );
                    lock.unlock();
                    state->cv.notify_all();
                    return;
                }
                lock.unlock();
                state->cv.notify_all();
                delete ptr;
            }
            else
            {
                delete ptr;
            }
        } );
}

void TileMemoryPool::releaseBuffer( std::unique_ptr<std::vector<float>> buf )
{
    if ( !buf )
        return;
    std::lock_guard<std::mutex> lock( m_state->mutex );
    const std::uint64_t capBytes =
        static_cast<std::uint64_t>( buf->capacity() ) * sizeof( float );
    if ( m_state->maxPoolBytes == 0 ||
         ( m_state->pooledBytes + capBytes <= m_state->maxPoolBytes ) )
    {
        m_state->pooledBytes += capBytes;
        m_state->freePool.push_back( std::move( buf ) );
    }
}

void TileMemoryPool::clearPool()
{
    std::lock_guard<std::mutex> lock( m_state->mutex );
    m_state->freePool.clear();
    m_state->pooledBytes = 0;
}

void TileMemoryPool::trimPool( std::uint64_t targetBytes )
{
    std::lock_guard<std::mutex> lock( m_state->mutex );
    while ( !m_state->freePool.empty() && m_state->pooledBytes > targetBytes )
    {
        const std::uint64_t capBytes =
            static_cast<std::uint64_t>( m_state->freePool.back()->capacity() ) * sizeof( float );
        m_state->freePool.pop_back();
        if ( m_state->pooledBytes >= capBytes )
            m_state->pooledBytes -= capBytes;
        else
            m_state->pooledBytes = 0;
    }
}

std::uint64_t TileMemoryPool::allocatedBytes() const
{
    if ( !m_state )
        return 0;
    std::lock_guard<std::mutex> lock( m_state->mutex );
    return m_state->allocatedBytes;
}

std::uint64_t TileMemoryPool::pooledBytes() const
{
    if ( !m_state )
        return 0;
    std::lock_guard<std::mutex> lock( m_state->mutex );
    return m_state->pooledBytes;
}

std::size_t TileMemoryPool::pooledCount() const
{
    if ( !m_state )
        return 0;
    std::lock_guard<std::mutex> lock( m_state->mutex );
    return m_state->freePool.size();
}

std::uint64_t TileMemoryPool::totalAllocations() const
{
    if ( !m_state )
        return 0;
    std::lock_guard<std::mutex> lock( m_state->mutex );
    return m_state->totalAllocations;
}

std::uint64_t TileMemoryPool::poolHits() const
{
    if ( !m_state )
        return 0;
    std::lock_guard<std::mutex> lock( m_state->mutex );
    return m_state->poolHits;
}

std::uint64_t TileMemoryPool::poolMisses() const
{
    if ( !m_state )
        return 0;
    std::lock_guard<std::mutex> lock( m_state->mutex );
    return m_state->poolMisses;
}

// ── ExecutionGovernor ────────────────────────────────────────────────────────

ExecutionGovernor::ExecutionGovernor( Config config )
    : m_config( std::move( config ) ),
      m_scratch( chunk::ScratchRegistry::Config{ m_config.scratchRoot, m_config.scratchBytes } ),
      m_writeGate( m_config.writeInFlightBytes ),
      m_memoryPool( TileMemoryPool::Config{ m_config.maxAllocatedBytes, m_config.maxPoolBytes } )
{
    // A fresh governor starts a fresh leak-observation window: a previous
    // governor's destructor report must not bleed into a LATER, strictly
    // sequential governor's diagnostics. (Last-writer-wins: with overlapping
    // lifetimes an inner construction clears and the outer destructor then
    // overwrites — the sequential case is the contract, the overlap case is
    // best-effort by design, matching the process-wide single-slot report.)
    std::lock_guard<std::mutex> leakLock( g_lastLeakMutex );
    g_lastLeakReportJson.clear();
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
                                 "reservations / memory pool buffers before destroying the governor";

        std::vector<std::string> causes;
        causes.push_back( "governor destroyed with live resources" );
        std::string detail = "leak:";

        if ( m_scratch.outstandingBytes() > 0 )
        {
            const std::string s = "scratch outstanding "
                                  + std::to_string( m_scratch.outstandingBytes() ) + " B";
            causes.push_back( s );
            detail += " " + s;
        }
        if ( m_writeGate.outstandingBytes() > 0 )
        {
            const std::string s = "write-gate outstanding "
                                  + std::to_string( m_writeGate.outstandingBytes() ) + " B";
            causes.push_back( s );
            detail += " " + s;
        }
        if ( m_memoryPool.allocatedBytes() > 0 )
        {
            const std::string s = "memory pool outstanding "
                                  + std::to_string( m_memoryPool.allocatedBytes() ) + " B";
            causes.push_back( s );
            detail += " " + s;
        }

        report.causeChain = std::move( causes );
        if ( !m_scratch.root().empty() )
            report.artifacts = { m_scratch.root() };

        {
            std::lock_guard<std::mutex> leakLock( g_lastLeakMutex );
            g_lastLeakReportJson = report.toJson();
        }
        ExecutionTelemetry::instance().increment( Counter::ResourceLeaksDetected );
        observability::TelemetryEvent event;
        event.kind = observability::EventKind::ResourceWait;
        event.subject = "exec.governor";
        event.detail = std::move( detail );
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

std::uint64_t ExecutionGovernor::currentRssBytes() const
{
    if ( m_config.rssSampler )
        return m_config.rssSampler();
    return defaultCurrentRssBytes();
}

bool ExecutionGovernor::isWatermarkExceeded() const
{
    if ( m_config.rssWatermarkBytes == 0 )
        return false;
    const std::uint64_t current = currentRssBytes();
    if ( current == 0 )
        return false;
    return current >= m_config.rssWatermarkBytes;
}

void ExecutionGovernor::throttleWait( const std::atomic<bool> *cancelFlag,
                                      std::chrono::milliseconds pollInterval ) const
{
    if ( cancelFlag )
    {
        throttleWait( [cancelFlag] { return cancelFlag->load( std::memory_order_relaxed ); },
                      pollInterval );
    }
    else
    {
        throttleWait( std::function<bool()>{}, pollInterval );
    }
}

void ExecutionGovernor::throttleWait( const std::function<bool()> &cancelPredicate,
                                      std::chrono::milliseconds pollInterval ) const
{
    if ( cancelPredicate && cancelPredicate() )
        throw chunk::ChunkCancelled( "cancelled while throttled on RSS watermark" );

    if ( m_config.rssWatermarkBytes == 0 )
        return;

    const std::uint64_t lowWatermark =
        ( m_config.rssLowWatermarkBytes > 0 && m_config.rssLowWatermarkBytes <= m_config.rssWatermarkBytes )
            ? m_config.rssLowWatermarkBytes
            : m_config.rssWatermarkBytes;

    std::uint64_t rss = currentRssBytes();
    if ( rss == 0 || rss < m_config.rssWatermarkBytes )
        return;

    if ( pollInterval <= std::chrono::milliseconds::zero() )
        pollInterval = std::chrono::milliseconds( 1 );

    const auto t0 = std::chrono::steady_clock::now();

    while ( true )
    {
        if ( cancelPredicate && cancelPredicate() )
            throw chunk::ChunkCancelled( "cancelled while throttled on RSS watermark" );

        rss = currentRssBytes();
        if ( rss == 0 || rss < lowWatermark )
            break;

        std::this_thread::sleep_for( pollInterval );
    }

    const auto elapsedNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0 ).count();

    auto &telemetry = observability::ExecutionTelemetry::instance();
    if ( telemetry.isEnabled() )
    {
        telemetry.recordSimple( observability::EventKind::ResourceWait, -1,
                                elapsedNanos, "exec.governor.rss_throttle" );
    }
}

bool ExecutionGovernor::hasOutstandingResources() const
{
    return m_scratch.outstandingBytes() > 0 ||
           m_writeGate.outstandingBytes() > 0 ||
           m_memoryPool.allocatedBytes() > 0;
}

std::string ExecutionGovernor::lastLeakReportJson()
{
    std::lock_guard<std::mutex> leakLock( g_lastLeakMutex );
    return g_lastLeakReportJson;
}

} // namespace sicnu::runtime::exec
