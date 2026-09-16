// src/agent/tool_catalog/surface_progress.cpp

#include "surface_progress.h"

namespace sicnu::agent::tool_catalog::progress {

bool RateLimiter::shouldEmit( double progress, bool terminal, const std::string &state )
{
    if ( terminal )
    {
        if ( m_terminalSent )
            return false; // exactly one terminal event per task
        m_terminalSent = true;
        m_lastEmitted = progress;
        m_lastState = state;
        return true;
    }
    if ( !state.empty() && state != m_lastState )
    {
        // Non-terminal state change (e.g. queued → running) is announced
        // regardless of the progress delta.
        m_lastState = state;
        m_lastEmitted = progress;
        return true;
    }
    if ( m_lastEmitted < 0.0 || progress - m_lastEmitted >= m_minDelta
         || progress >= kTaskCenterMax )
    {
        m_lastEmitted = progress;
        return true;
    }
    return false;
}

} // namespace sicnu::agent::tool_catalog::progress
