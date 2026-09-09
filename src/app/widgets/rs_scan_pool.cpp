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

quint64 RsScanPool::nextGeneration()
{
    return m_activeGeneration.fetch_add( 1, std::memory_order_acq_rel ) + 1;
}

void RsScanPool::cancel( quint64 generation )
{
    const std::lock_guard<std::mutex> lock( m_canceledMutex );
    // Bound the set: a worker only ever polls its own (recent) token, so
    // entries can be retired wholesale once the set grows large — by then
    // the matching workers finished long ago.
    if ( m_canceled.size() >= 1024 )
        m_canceled.clear();
    m_canceled.insert( generation );
}

} // namespace sicnu::app
