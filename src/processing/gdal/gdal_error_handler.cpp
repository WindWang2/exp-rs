// src/processing/gdal/gdal_error_handler.cpp — GDAL error handler implementation
#include "gdal_error_handler.h"

#include <algorithm>
#include <vector>

thread_local GdalErrorHandler *GdalErrorHandler::s_activeHandler = nullptr;

namespace
{
// Handler nesting stack (#634): an inner install/uninstall used to clear
// s_activeHandler and restore the previous CPL handler, silently DETACHING
// any outer handler on this thread. A thread-local stack keeps every level
// intact.
thread_local std::vector<GdalErrorHandler *> s_handlerStack;
// The process default captured when the FIRST handler on this thread
// installs, so uninstalling the last handler (in or out of LIFO order)
// genuinely restores the default instead of leaving the static routing
// handler installed, silently swallowing CPL diagnostics (#654).
thread_local CPLErrorHandler s_processDefaultHandler = nullptr;
} // namespace

GdalErrorHandler::GdalErrorHandler() = default;

GdalErrorHandler::~GdalErrorHandler()
{
    uninstall();
}

void GdalErrorHandler::install()
{
    if ( s_handlerStack.empty() )
        s_processDefaultHandler = CPLSetErrorHandler(errorHandler);
    else
        CPLSetErrorHandler(errorHandler);
    s_handlerStack.push_back( this );
    s_activeHandler = this;
    m_previousHandler = s_processDefaultHandler;
}

void GdalErrorHandler::uninstall()
{
    // Pop THIS handler; the new active handler is the previous stack level
    // (its CPL callback is re-armed by restoring its callback chain).
    for ( auto it = s_handlerStack.rbegin(); it != s_handlerStack.rend(); ++it )
    {
        if ( *it == this )
        {
            s_handlerStack.erase( std::next( it ).base() );
            break;
        }
    }
    if ( s_activeHandler == this )
    {
        // Out-of-LIFO uninstalls (#654): m_previousHandler is what the CPL
        // chain pointed at when THIS handler installed. When the stack
        // empties through such an uninstall, that is the first installer's
        // captured process default (handlers below already popped) — and
        // when another handler remains, the routing callback must stay
        // armed. Either way the process default, captured once per thread,
        // is the only correct final restore target.
        CPLSetErrorHandler( s_handlerStack.empty() ? s_processDefaultHandler
                                                   : errorHandler );
        m_previousHandler = nullptr;
        s_activeHandler = s_handlerStack.empty() ? nullptr : s_handlerStack.back();
    }
}

void GdalErrorHandler::clear()
{
    m_hasError = false;
    m_message.clear();
    m_severity = CE_None;
    m_errorNumber = 0;
}

bool GdalErrorHandler::hasError() const
{
    return m_hasError;
}

QString GdalErrorHandler::lastErrorMessage() const
{
    return m_message;
}

CPLErr GdalErrorHandler::lastErrorSeverity() const
{
    return m_severity;
}

int GdalErrorHandler::lastErrorNumber() const
{
    return m_errorNumber;
}

void GdalErrorHandler::errorHandler(CPLErr eErrClass, int nError, const char *msg)
{
    if (s_activeHandler) {
        s_activeHandler->m_hasError = true;
        s_activeHandler->m_severity = eErrClass;
        s_activeHandler->m_errorNumber = nError;
        s_activeHandler->m_message = msg ? QString::fromUtf8(msg) : QString();
    }
}
