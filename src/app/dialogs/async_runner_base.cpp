// async_runner_base.cpp — out-of-line translation unit for the header-only
// Q_OBJECT base. The explicit moc include guarantees the signal/vtable symbols
// (e.g. AsyncRunnerBase::failed) are emitted into the linking target even when
// the header is only ever included through another header.
#include "async_runner_base.h"
#include "moc_async_runner_base.cpp"

// Dying mid-run must not leak the override cursor forever: the application
// cursor stack would stay in WaitCursor until the app quits (#1695).
AsyncRunnerBase::~AsyncRunnerBase()
{
    if (m_running)
        endRun();
}
