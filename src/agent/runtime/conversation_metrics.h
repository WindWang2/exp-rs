#pragma once

#include "agent_conversation_runtime.h"

#include <QElapsedTimer>

namespace sicnu::agent::runtime
{

/// Optional diagnostic observer. It cannot submit, cancel, approve, or retry.
/// No profile, message, tool payload, or provider diagnostic enters this module.
/// Records only runs which start while the existing UnifiedTrace sink is on.
class SICNU_AGENT_EXPORT ConversationMetrics final : public QObject
{
  public:
    enum class Backend { Native, Pi };
    explicit ConversationMetrics( AgentConversationRuntime *runtime, Backend backend );
    ~ConversationMetrics() override;

    /// Last terminal observation; empty when tracing was disabled at run start.
    QJsonObject snapshot() const { return m_snapshot; }

  private:
    void begin( const QString &run );
    void request( const QString &run );
    void usage( const QString &run, const QJsonObject &envelope );
    void firstDelta( const QString &run );
    void finish( const QString &status );

    const Backend m_backend;
    QString m_run;
    QElapsedTimer m_elapsed;
    qint64 m_firstDeltaUs = -1;
    qint64 m_requests = 0;
    qint64 m_reports = 0;
    qint64 m_invalidReports = 0;
    QJsonObject m_totals;
    QJsonObject m_snapshot;
};

} // namespace sicnu::agent::runtime
