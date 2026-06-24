// src/processing/gdal/gdal_error_handler.cpp — GDAL error handler implementation
#include "gdal_error_handler.h"

thread_local GdalErrorHandler *GdalErrorHandler::s_activeHandler = nullptr;

GdalErrorHandler::GdalErrorHandler() = default;

GdalErrorHandler::~GdalErrorHandler()
{
    uninstall();
}

void GdalErrorHandler::install()
{
    s_activeHandler = this;
    CPLSetErrorHandler(errorHandler);
}

void GdalErrorHandler::uninstall()
{
    if (s_activeHandler == this) {
        CPLSetErrorHandler(nullptr);
        s_activeHandler = nullptr;
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
