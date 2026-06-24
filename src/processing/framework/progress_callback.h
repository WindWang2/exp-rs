#pragma once

#include <QString>
#include <atomic>

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
    std::atomic<int> m_totalSteps{0};
    std::atomic<int> m_currentStep{0};
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_completed{false};
    std::atomic<bool> m_success{false};
    std::atomic<bool> m_cancelled{false};
    QString m_lastMessage;
};

} // namespace sicnu
