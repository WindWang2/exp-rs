/***************************************************************************
 * va_source.cpp — bounded, cancellable VA payload delivery
 ***************************************************************************/
#include "va_source.h"

#include "widgets/rs_scan_pool.h"
#include "workbench/marshal_ui.h"

#include <QMetaType>
#include <QPointer>

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
    // The receiver-scoped queued call dies with this object, and the
    // generation check drops results superseded while in flight. The raw
    // `this` in `stale` is only a RsScanPool owner key (never dereferenced);
    // the MARSHAL target must be re-derived from a QPointer on the pool
    // thread — marshal_ui.h's discard guarantee only covers calls made
    // before destruction, so a job outrunning this object's destructor must
    // not invokeMethod on the freed receiver (histogram_widget pattern).
    QPointer<VaDataSource> self( this );
    RsScanPool::instance().pool().start( [self, generation, fn = std::move( fn ), stale]() {
        try
        {
            VaData data = fn( stale );
            ui_callback::marshalTo( self.data(), [self, generation, data = std::move( data )]() {
                if ( !self )
                    return;
                if ( RsScanPool::instance().isStale( generation, self.data() ) )
                    return;
                self->m_busy.store( false, std::memory_order_release );
                emit self->ready( data );
            } );
        }
        catch ( const std::exception &e )
        {
            const QByteArray message = QString::fromUtf8( e.what() ).toUtf8();
            ui_callback::marshalTo( self.data(), [self, generation, message]() {
                if ( !self )
                    return;
                if ( RsScanPool::instance().isStale( generation, self.data() ) )
                    return;
                self->m_busy.store( false, std::memory_order_release );
                emit self->failed( QString::fromUtf8( message ) );
            } );
        }
    } );
}

} // namespace sicnu::app::va
