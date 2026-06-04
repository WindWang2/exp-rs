#ifndef PROGRESS_CALLBACK_H
#define PROGRESS_CALLBACK_H

#include <QString>

namespace sicnu {

class ProgressCallback
{
public:
    virtual ~ProgressCallback() = default;

    virtual void onStart(const QString &taskName, int totalSteps) = 0;
    virtual void onProgress(int currentStep, const QString &message = QString()) = 0;
    virtual void onComplete(bool success, const QString &message = QString()) = 0;
    virtual bool isCancelled() const = 0;
};

class SimpleProgressCallback : public ProgressCallback
{
public:
    void onStart(const QString &taskName, int totalSteps) override;
    void onProgress(int currentStep, const QString &message = QString()) override;
    void onComplete(bool success, const QString &message = QString()) override;
    bool isCancelled() const override;

    void cancel();

    // Accessors for testing
    QString taskName() const;
    int totalSteps() const;
    int currentStep() const;
    bool isStarted() const;
    bool isCompleted() const;
    bool isSuccess() const;
    QString lastMessage() const;

private:
    QString m_taskName;
    int m_totalSteps = 0;
    int m_currentStep = 0;
    bool m_started = false;
    bool m_completed = false;
    bool m_success = false;
    bool m_cancelled = false;
    QString m_lastMessage;
};

} // namespace sicnu

#endif // PROGRESS_CALLBACK_H
