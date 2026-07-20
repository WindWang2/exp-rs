// src/processing/gdal/gdal_error_handler.h
#pragma once

#include <QString>
#include <cpl_error.h>

/**
 * Custom GDAL error handler that captures errors instead of printing to stderr.
 *
 * Usage:
 *   GdalErrorHandler handler;
 *   handler.install();
 *   // ... use GDAL ...
 *   if (handler.hasError()) {
 *       qDebug() << handler.lastErrorMessage();
 *   }
 *
 * The handler captures the most recent GDAL error. Call clear() to reset.
 * Only one handler can be active at a time (GDAL global state).
 */
class GdalErrorHandler
{
public:
    GdalErrorHandler();
    ~GdalErrorHandler();

    GdalErrorHandler(const GdalErrorHandler&) = delete;
    GdalErrorHandler& operator=(const GdalErrorHandler&) = delete;

    /// Install this handler as the GDAL error callback.
    void install();

    /// Uninstall (restore default GDAL error handler).
    void uninstall();

    /// Clear captured error state.
    void clear();

    /// True if an error has been captured since last clear().
    bool hasError() const;

    /// The last error message (empty if no error).
    QString lastErrorMessage() const;

    /// The last error severity (CE_None, CE_Debug, CE_Warning, CE_Failure, CE_Fatal).
    CPLErr lastErrorSeverity() const;

    /// The last error number (CPLE_XXX).
    int lastErrorNumber() const;

private:
    static void errorHandler(CPLErr eErrClass, int nError, const char *msg);

    QString m_message;
    CPLErr m_severity = CE_None;
    int m_errorNumber = 0;
    bool m_hasError = false;
    CPLErrorHandler m_previousHandler = nullptr;

    static thread_local GdalErrorHandler *s_activeHandler;
};
