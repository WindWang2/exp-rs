// async_runner_base.cpp — out-of-line translation unit for the header-only
// Q_OBJECT base. The explicit moc include guarantees the signal/vtable symbols
// (e.g. AsyncRunnerBase::failed) are emitted into the linking target even when
// the header is only ever included through another header.
#include "async_runner_base.h"
#include "moc_async_runner_base.cpp"
