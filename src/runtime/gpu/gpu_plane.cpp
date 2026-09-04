// gpu_plane.cpp — see gpu_plane.h for the contract.
#include "gpu_plane.h"

#include <algorithm>
#include <atomic>

namespace sicnu::runtime::gpu
{
namespace
{
std::string makeSessionId()
{
    static std::atomic<unsigned long long> counter{ 0 };
    return "session-" + std::to_string( counter.fetch_add( 1 ) + 1 );
}
} // namespace

struct ModelSessionPool::Impl
{
    std::shared_ptr<GpuBackend> backend;
    size_t vramBudgetMb = 0;

    mutable std::mutex mutex;

    struct LiveSession
    {
        std::shared_ptr<ModelSession> session;
        size_t vramMb = 0;
        bool released = false; ///< released = warm for reuse, evictable
    };
    std::vector<LiveSession> sessions;         ///< all live sessions (all devices)
};

ModelSessionPool::ModelSessionPool( std::shared_ptr<GpuBackend> backend,
                                    size_t vramBudgetMbPerDevice )
{
    m_impl = new Impl;
    m_impl->backend = std::move( backend );
    m_impl->vramBudgetMb = vramBudgetMbPerDevice;
}

ModelSessionPool::~ModelSessionPool()
{
    if ( !m_impl )
        return;
    // Free all VRAM accounting before teardown.
    for ( const auto &live : m_impl->sessions )
        m_impl->backend->freeVram( live.session->deviceId, live.vramMb );
    delete m_impl;
}

AcquireResult ModelSessionPool::acquireSession( const SessionRequest &request )
{
    AcquireResult result;
    if ( !m_impl )
        return result;

    std::lock_guard<std::mutex> lock( m_impl->mutex );

    // 1) Reuse: identity match ⇒ the loaded model is reused as-is.
    for ( auto &live : m_impl->sessions )
    {
        if ( live.session->model.modelId != request.model.modelId )
            continue;
        if ( live.session->model.signature != request.model.signature )
            continue; // stale — evictStale handles recycling
        live.released = false;
        live.session->useCount += 1;
        result.outcome = AcquireOutcome::Acquired;
        result.session = live.session;
        return result;
    }

    // 2) Device selection.
    auto devices = m_impl->backend->enumerate();
    if ( request.model.modelId.empty() || devices.empty() )
    {
        result.outcome = AcquireOutcome::NoDevice;
        return result;
    }
    int deviceId = request.deviceId;
    if ( deviceId >= 0 )
    {
        const bool exists = std::any_of( devices.begin(), devices.end(),
                                         [&]( const DeviceInfo &d ) {
                                             return d.deviceId == deviceId && d.available;
                                         } );
        if ( !exists )
        {
            result.outcome = AcquireOutcome::NoDevice;
            return result;
        }
    }
    else
    {
        for ( const auto &device : devices )
        {
            if ( device.available )
            {
                deviceId = device.deviceId;
                break;
            }
        }
        if ( deviceId < 0 )
        {
            result.outcome = AcquireOutcome::NoDevice;
            return result;
        }
    }

    // 3) OOM ladder: preferred size, then the reduction steps, then CPU.
    size_t used = 0;
    for ( const auto &live : m_impl->sessions )
        if ( live.session->deviceId == deviceId )
            used += live.vramMb;

    std::vector<size_t> ladder;
    if ( request.vramMb > 0 )
        ladder.push_back( request.vramMb );
    for ( size_t step : request.reducedVramMb )
        if ( step > 0 )
            ladder.push_back( step );

    // Per-device warm-session bound: a secondary fairness guard behind the
    // VRAM budget (which is the primary limiter). Only when budget would
    // still admit a model do we stop the 5th warm session from loading.
    const size_t deviceSessionCount =
        std::count_if( m_impl->sessions.begin(), m_impl->sessions.end(),
                       [&]( const Impl::LiveSession &live ) {
                           return live.session->deviceId == deviceId;
                       } );
    if ( deviceSessionCount >= 4 && !ladder.empty() )
    {
        result.outcome = AcquireOutcome::Busy;
        return result;
    }

    for ( size_t mb : ladder )
    {
        if ( m_impl->vramBudgetMb > 0 && used + mb > m_impl->vramBudgetMb )
            continue;
        if ( !m_impl->backend->allocateVram( deviceId, mb ) )
            continue;
        auto session = std::make_shared<ModelSession>();
        session->sessionId = makeSessionId();
        session->model = request.model;
        session->grantedVramMb = mb;
        session->deviceId = deviceId;
        session->useCount = 1;
        m_impl->sessions.push_back( Impl::LiveSession{ session, mb, false } );
        result.outcome = mb == request.vramMb ? AcquireOutcome::Acquired
                                              : AcquireOutcome::AcquiredReduced;
        result.session = session;
        return result;
    }

    // Nothing fit: CPU fallback.
    result.outcome = AcquireOutcome::CpuFallback;
    return result;
}

void ModelSessionPool::releaseSession( const std::string &sessionId )
{
    if ( !m_impl )
        return;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    for ( auto &live : m_impl->sessions )
    {
        if ( live.session->sessionId == sessionId )
        {
            live.released = true; // stays warm for reuse until evicted
            return;
        }
    }
}

size_t ModelSessionPool::liveSessionCount() const
{
    if ( !m_impl )
        return 0;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    return m_impl->sessions.size();
}

size_t ModelSessionPool::usedVramMb( int deviceId ) const
{
    if ( !m_impl )
        return 0;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    size_t used = 0;
    for ( const auto &live : m_impl->sessions )
        if ( live.session->deviceId == deviceId )
            used += live.vramMb;
    return used;
}

std::vector<DeviceInfo> ModelSessionPool::devices() const
{
    if ( !m_impl )
        return {};
    return m_impl->backend->enumerate();
}

void ModelSessionPool::evictStale( const std::string &modelId,
                                   const std::string &currentSignature )
{
    if ( !m_impl )
        return;
    std::lock_guard<std::mutex> lock( m_impl->mutex );
    for ( auto it = m_impl->sessions.begin(); it != m_impl->sessions.end(); )
    {
        if ( it->session->model.modelId == modelId
             && it->session->model.signature != currentSignature )
        {
            m_impl->backend->freeVram( it->session->deviceId, it->vramMb );
            it = m_impl->sessions.erase( it );
        }
        else
        {
            ++it;
        }
    }
}

} // namespace sicnu::runtime::gpu
