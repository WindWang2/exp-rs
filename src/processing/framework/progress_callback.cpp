#include "progress_callback.h"

namespace sicnu {

void SimpleProgressCallback::onStart(const QString &taskName, int totalSteps)
{
    m_taskName = taskName;
    m_totalSteps = totalSteps;
    m_currentStep = 0;
    m_started = true;
}

void SimpleProgressCallback::onProgress(int currentStep, const QString &message)
{
    m_currentStep = currentStep;
    m_lastMessage = message;
}

void SimpleProgressCallback::onComplete(bool success, const QString &message)
{
    m_completed = true;
    m_success = success;
    m_lastMessage = message;
}

bool SimpleProgressCallback::isCancelled() const
{
    return m_cancelled;
}

void SimpleProgressCallback::cancel()
{
    m_cancelled = true;
}

QString SimpleProgressCallback::taskName() const
{
    return m_taskName;
}

int SimpleProgressCallback::totalSteps() const
{
    return m_totalSteps;
}

int SimpleProgressCallback::currentStep() const
{
    return m_currentStep;
}

bool SimpleProgressCallback::isStarted() const
{
    return m_started;
}

bool SimpleProgressCallback::isCompleted() const
{
    return m_completed;
}

bool SimpleProgressCallback::isSuccess() const
{
    return m_success;
}

QString SimpleProgressCallback::lastMessage() const
{
    return m_lastMessage;
}

} // namespace sicnu
