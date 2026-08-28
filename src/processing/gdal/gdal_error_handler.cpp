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
} // namespace

GdalErrorHandler::GdalErrorHandler() = default;

GdalErrorHandler::~GdalErrorHandler()
{
    uninstall();
}

void GdalErrorHandler::install()
{
    s_handlerStack.push_back( this );
    s_activeHandler = this;
    m_previousHandler = CPLSetErrorHandler(errorHandler);
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
        CPLSetErrorHandler(m_previousHandler);
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
