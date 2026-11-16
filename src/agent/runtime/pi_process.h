#pragma once

#include "agent/sicnu_agent_export.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <memory>

namespace sicnu::agent::runtime
{

/// Owns one verified Node child and the product protocol, never native tools.
/// Verification is cancellable background work. It sends no credential until
/// the complete installed receipts pass and OS containment has been armed.
class SICNU_AGENT_EXPORT PiProcess final : public QObject
{
    Q_OBJECT
  public:
    enum class State { Idle, Verifying, Starting, Ready, Running, Cancelling, Closing, Failed, Closed };
    Q_ENUM( State )

    struct Config
    {
      QString prefixPath;
      QString sessionId;
      QString generation;
      QString toolRevision; // Empty means derive from tools with toolRevision().
      QJsonObject profile; // Nonsecret fields only; apiKey is supplied separately.
      QByteArray apiKey;
      QJsonArray tools;
      QString systemPrompt;
      int verifyTimeoutMs = 15 * 60 * 1000;
      int readyTimeoutMs = 120000;
      int closeTimeoutMs = 3000;
      // #2076: cancelling must let in-flight host tools finish their cleanup;
      // the transport close window (closeTimeoutMs) stays short because a
      // shutdown no longer waits for anything.
      int cancelTimeoutMs = 30000;
      QJsonObject checkpointBinding;
      QJsonObject checkpoint;
    };

    explicit PiProcess( QObject *parent = nullptr );
    ~PiProcess() override;
    void start( const Config &config );
    bool sendPrompt( const QString &runId, const QString &text );
    bool completeTool( const QString &runId, const QString &callId, const QJsonObject &result );
    /// Only a completed successful run in Ready state is checkpointable.
    /// IDs are unique within the session. Native-idle confirmation is the caller's responsibility.
    bool requestCheckpoint( const QString &runId, const QString &checkpointId );
    void cancel();
    void close();
    State state() const;
    QString stderrTail() const;
    static QString toolRevision( const QJsonArray &tools );

  signals:
    void stateChanged( sicnu::agent::runtime::PiProcess::State state );
    void ready( const QJsonObject &event );
    /// Only validated product events; no untrusted protocol/profile diagnostics.
    void eventReceived( const QJsonObject &event );
    void checkpointReady( const QString &runId, const QString &checkpointId, const QJsonObject &checkpoint );
    void checkpointFailed( const QString &runId, const QString &checkpointId, const QString &code );
    void failed( const QString &code, const QString &safeMessage );
    void closed();

  private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace sicnu::agent::runtime
