#include "error_reporter.h"

namespace sicnu {

void ErrorReporter::reportError(const QString &provider, const QString &algorithm,
                                const QString &message, int errorCode)
{
    ProcessingError error;
    error.provider = provider;
    error.algorithm = algorithm;
    error.message = message;
    error.errorCode = errorCode;
    error.timestamp = QDateTime::currentDateTime();
    m_errors.append(error);

    if (m_callback)
        m_callback(provider, algorithm, message, errorCode);
}

QList<ProcessingError> ErrorReporter::errors() const
{
    return m_errors;
}

int ErrorReporter::errorCount() const
{
    return m_errors.size();
}

void ErrorReporter::clear()
{
    m_errors.clear();
}

ProcessingError ErrorReporter::lastError() const
{
    return m_errors.isEmpty() ? ProcessingError() : m_errors.last();
}

bool ErrorReporter::hasErrors() const
{
    return !m_errors.isEmpty();
}

void ErrorReporter::setErrorCallback(ErrorCallback callback)
{
    m_callback = std::move(callback);
}

} // namespace sicnu
