/***************************************************************************
 * rs_scan_pool.cpp — bounded scan pool implementation
 ***************************************************************************/
#include "rs_scan_pool.h"

namespace sicnu::app
{

RsScanPool::RsScanPool()
{
    // Two concurrent GDAL scans max: rendering and the rest of the
    // application keep their own threads either way (#797).
    m_pool.setMaxThreadCount( 2 );
}

RsScanPool &RsScanPool::instance()
{
    static RsScanPool s_pool;
    return s_pool;
}

quint64 RsScanPool::nextGeneration( const void *owner )
{
    const quint64 gen = m_activeGeneration.fetch_add( 1, std::memory_order_acq_rel ) + 1;
    if ( owner )
    {
        const std::lock_guard<std::mutex> lock( m_canceledMutex );
        m_ownerActiveGeneration[owner] = gen;
    }
    return gen;
}

void RsScanPool::cancel( quint64 generation, const void *owner )
{
    const std::lock_guard<std::mutex> lock( m_canceledMutex );
    // F-07 (ui-backend-state-parity-r4): keep the owner's generation record.
    // Erasing it left a canceled-but-not-superseded generation with only the
    // canceled set as its staleness anchor — and the wholesale clear() below
    // wiped that too, so an expired scan could still land (SP-2).
    //
    // Bound the set without losing live guarantees: entries below the
    // minimum live generation (global + every owner's last issued one) are
    // already stale by the generation comparison alone, so only those are
    // retired.
    m_canceled.insert( generation );
    if ( m_canceled.size() < 1024 )
        return;
    quint64 minLive = m_activeGeneration.load( std::memory_order_acquire );
    for ( const auto &entry : m_ownerActiveGeneration )
    {
        if ( entry.second < minLive )
            minLive = entry.second;
    }
    for ( auto it = m_canceled.begin(); it != m_canceled.end(); )
    {
        if ( *it < minLive )
            it = m_canceled.erase( it );
        else
            ++it;
    }
}

} // namespace sicnu::app
