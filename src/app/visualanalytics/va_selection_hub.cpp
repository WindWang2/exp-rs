/***************************************************************************
 * va_selection_hub.cpp — selection authority implementation
 ***************************************************************************/
#include "va_selection_hub.h"

#include <qmath.h>

#include <algorithm>

namespace sicnu::app::va
{

namespace
{
QString clampText( const QString &text )
{
    return text.left( VaSelectionHub::kMaxTextChars );
}

bool finite( double v )
{
    return !std::isnan( v ) && !std::isinf( v );
}
} // namespace

VaSelectionHub::VaSelectionHub( QObject *parent )
    : QObject( parent )
{
    // Queued connections across threads stay possible for GUI-marshalled
    // consumers; the hub itself remains GUI-thread-only.
    qRegisterMetaType<VaSelectionSubject>( "sicnu::app::va::VaSelectionSubject" );
    qRegisterMetaType<VaSelectionEvent>( "sicnu::app::va::VaSelectionEvent" );
}

VaSelectionSubject VaSelectionHub::clampSubject( VaSelectionSubject subject )
{
    subject.viewId = clampText( subject.viewId );
    subject.layerId = clampText( subject.layerId );
    subject.assetId = clampText( subject.assetId );
    subject.crsWkt = clampText( subject.crsWkt );
    subject.chartId = clampText( subject.chartId );
    return subject;
}

bool VaSelectionHub::isValidSubject( const VaSelectionSubject &subject )
{
    // Coordinate authority: every coordinate a subject carries must be
    // finite. Ranges/regions additionally require ordered bounds; pixels
    // require non-negative indices when present.
    if ( !finite( subject.x0 ) || !finite( subject.y0 ) || !finite( subject.x1 )
         || !finite( subject.y1 ) )
        return false;
    switch ( subject.kind )
    {
        case VaSelectionKind::ViewRegion:
        case VaSelectionKind::ChartRange:
            if ( subject.x1 < subject.x0 )
                return false;
            break;
        case VaSelectionKind::Pixel:
            if ( subject.row < 0 || subject.column < 0 )
                return false;
            break;
        default:
            break;
    }
    return true;
}

quint64 VaSelectionHub::publish( const VaSelectionSubject &subject, const QString &origin )
{
    // Loop suppression first: a subscriber slot re-publishing the very
    // event it is handling is an echo, whatever it carries. Compare on the
    // dispatched (origin, generation) pair — cross-surface republication
    // with a different origin (or a later generation) is fresh intent.
    if ( m_dispatchDepth > 0 && origin == m_dispatchOrigin
         && m_dispatchGeneration != 0 )
    {
        ++m_stats.suppressedEchoes;
        return 0;
    }
    if ( !isValidSubject( subject ) )
    {
        ++m_stats.rejected;
        return 0;
    }

    VaSelectionEvent event;
    event.subject = clampSubject( subject );
    event.origin = clampText( origin );
    event.generation = ++m_generation;

    m_history.prepend( event );
    if ( m_history.size() > kHistoryCapacity )
        m_history.resize( kHistoryCapacity );

    ++m_stats.published;

    // Direct connections dispatch inline; the depth guard makes the echo
    // check above reentrancy-proof. Queued connections deliver later and
    // cannot re-enter this frame at all.
    ++m_dispatchDepth;
    m_dispatchOrigin = event.origin;
    m_dispatchGeneration = event.generation;
    emit selectionPublished( event );
    --m_dispatchDepth;
    if ( m_dispatchDepth == 0 )
    {
        m_dispatchOrigin.clear();
        m_dispatchGeneration = 0;
    }
    return event.generation;
}

} // namespace sicnu::app::va
