/***************************************************************************
 * va_source.cpp — bounded, cancellable VA payload delivery
 ***************************************************************************/
#include "va_source.h"

#include "widgets/rs_scan_pool.h"
#include "workbench/marshal_ui.h"

#include <QMetaType>

namespace sicnu::app::va
{

namespace
{
struct Registration
{
    Registration() { qRegisterMetaType<VaData>( "sicnu::app::va::VaData" ); }
};
const Registration kRegistration;
} // namespace

VaDataSource::VaDataSource( QObject *parent )
    : QObject( parent )
{
}

VaDataSource::~VaDataSource()
{
    cancel();
}

void VaDataSource::cancel()
{
    // Supersede the generation; the worker observes staleness on its next
    // poll and any queued completion is dropped by the generation check.
    RsScanPool::instance().cancel( m_generation.load( std::memory_order_relaxed ), this );
    RsScanPool::instance().nextGeneration( this );
    m_busy.store( false, std::memory_order_release );
}

void VaDataSource::request( ComputeFn fn )
{
    cancel();
    const quint64 generation = RsScanPool::instance().nextGeneration( this );
    m_generation.store( generation, std::memory_order_relaxed );
    m_busy.store( true, std::memory_order_release );
    emit loading();

    const auto stale = [this, generation]() {
        return RsScanPool::instance().isStale( generation, this );
    };

    // Pool thread: compute the bounded payload, then marshal the delivery.
    // The receiver-scoped queued call dies with this object; the generation
    // check drops results superseded while in flight.
    RsScanPool::instance().pool().start( [this, generation, fn = std::move( fn ), stale]() {
        try
        {
            VaData data = fn( stale );
            ui_callback::marshalTo( this, [this, generation, data = std::move( data )]() {
                if ( RsScanPool::instance().isStale( generation, this ) )
                    return;
                m_busy.store( false, std::memory_order_release );
                emit ready( data );
            } );
        }
        catch ( const std::exception &e )
        {
            const QByteArray message = QString::fromUtf8( e.what() ).toUtf8();
            ui_callback::marshalTo( this, [this, generation, message]() {
                if ( RsScanPool::instance().isStale( generation, this ) )
                    return;
                m_busy.store( false, std::memory_order_release );
                emit failed( QString::fromUtf8( message ) );
            } );
        }
    } );
}

} // namespace sicnu::app::va
