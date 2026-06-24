#include "error_reporter.h"
#include <QMutexLocker>

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

    ErrorCallback cb;
    {
        QMutexLocker locker(&m_mutex);
        m_errors.append(error);
        cb = m_callback;
    }

    if (cb)
        cb(provider, algorithm, message, errorCode);
}

QList<ProcessingError> ErrorReporter::errors() const
{
    QMutexLocker locker(&m_mutex);
    return m_errors;
}

int ErrorReporter::errorCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_errors.size();
}

void ErrorReporter::clear()
{
    QMutexLocker locker(&m_mutex);
    m_errors.clear();
}

ProcessingError ErrorReporter::lastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_errors.isEmpty() ? ProcessingError() : m_errors.last();
}

bool ErrorReporter::hasErrors() const
{
    QMutexLocker locker(&m_mutex);
    return !m_errors.isEmpty();
}

void ErrorReporter::setErrorCallback(ErrorCallback callback)
{
    QMutexLocker locker(&m_mutex);
    m_callback = std::move(callback);
}

} // namespace sicnu
