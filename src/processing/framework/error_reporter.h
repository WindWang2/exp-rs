#pragma once

#include <QString>
#include <QList>
#include <QDateTime>
#include <QMutex>
#include <functional>

namespace sicnu {

struct ProcessingError
{
    QString provider;
    QString algorithm;
    QString message;
    int errorCode = -1;
    QDateTime timestamp;
};

class ErrorReporter
{
public:
    using ErrorCallback = std::function<void(const QString &provider, const QString &algorithm,
                                              const QString &message, int errorCode)>;

    void reportError(const QString &provider, const QString &algorithm,
                     const QString &message, int errorCode = -1);

    QList<ProcessingError> errors() const;
    int errorCount() const;
    void clear();

    ProcessingError lastError() const;
    bool hasErrors() const;

    void setErrorCallback(ErrorCallback callback);

private:
    mutable QMutex m_mutex;
    QList<ProcessingError> m_errors;
    ErrorCallback m_callback;
};

} // namespace sicnu
