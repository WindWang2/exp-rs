/***************************************************************************
 * session_adapters.cpp — classification adapter implementation
 ***************************************************************************/
#include "session_adapters.h"

#include "classification/qgsclassificationmainwindow.h"

namespace sicnu::app
{

ClassifySessionAdapter::ClassifySessionAdapter( Opener opener, QObject *parent )
    : InteractiveSession( parent )
    , m_opener( std::move( opener ) )
{
}

QgsClassificationMainWindow *ClassifySessionAdapter::ensureWindow() const
{
    if ( m_window )
        return m_window.data();
    if ( m_opener )
        m_window = m_opener();
    return m_window.data();
}

bool ClassifySessionAdapter::isDirty() const
{
    return m_window ? m_window->isSessionDirty() : false;
}

void ClassifySessionAdapter::clearDirty()
{
    // The lab clears its own dirty flag when the user saves/loads (owner of
    // the state); the contract method exists so shells must not poke state.
}

bool ClassifySessionAdapter::hasInFlightCompute() const
{
    return m_window ? m_window->hasInFlightCompute() : false;
}

bool ClassifySessionAdapter::requestCancel()
{
    if ( !m_window || !m_window->hasInFlightCompute() )
        return false;
    m_window->cancelInFlightCompute();
    return true;
}

bool ClassifySessionAdapter::requestClose()
{
    // The lab's closeEvent already confirms unsaved ROI/class state
    // (Save/Discard/Cancel). Closing is therefore delegated, and the user's
    // answer decides. A window that goes away reports closed; a visible
    // window refused the close.
    QgsClassificationMainWindow *window = ensureWindow();
    if ( !window )
        return true;
    window->close();
    return !window->isVisible();
}

} // namespace sicnu::app
