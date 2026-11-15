#pragma once

#include "agent/sicnu_agent_export.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace sicnu::agent::runtime
{

/// Stable, nonsecret identities supplied by the current product host. No model
/// endpoint, profile, API key, prompt text or approval is accepted here.
struct SICNU_AGENT_EXPORT ConversationCheckpointBinding
{
  QString projectId;
  QString conversationId;
  QString sdkReceiptSha256;
  QString toolRevision;
  QString systemPromptSha256;
};

struct SICNU_AGENT_EXPORT ConversationCheckpointWriteResult
{
  /// A verify failure can follow publication; committed reports that distinction.
  bool committed = false;
  /// Empty on success, otherwise a fixed code for the product's translated UI.
  QString error;

  explicit operator bool() const { return committed && error.isEmpty(); }
};

/// Observed execution identity only. It is neither approval nor evidence of a
/// result. An empty task jobId remains unknown until a later observation.
struct SICNU_AGENT_EXPORT ConversationExecutionReference
{
  enum class Kind { Task, Pipeline };
  Kind kind = Kind::Task;
  qint64 nativeId = -1;
  QString jobId;
  QString processBootId;
};

struct SICNU_AGENT_EXPORT ConversationCheckpointLoadResult
{
  /// False means fresh state. A marker-only conversation has found == true.
  bool found = false;
  /// Compact JSON, already bound and checksum-verified. Empty if no checkpoint.
  /// The Node wrapper must still perform its full SDK entry/tree validation.
  QByteArray checkpointJson;
  QString checkpointSha256;
  /// An interrupted run is evidence for native reconciliation, never a prompt
  /// or an instruction to re-execute a tool. It contains no execution arguments.
  QString interruptedRunId;
  QList<ConversationExecutionReference> executionRefs;
  QString error;

  explicit operator bool() const { return error.isEmpty(); }
};

/// Product-owned persistence only. No settings/profile discovery and no model,
/// task, approval or replay callbacks. The caller supplies an absolute private
/// product directory and redacts secrets before passing validated Node data.
///
/// One atomic state file contains both the last completed checkpoint and its
/// incomplete-run marker. Failed validation/overflow leaves old bytes intact.
/// Writes use the existing platform sidecar durability authority and a per-file
/// process lock. A corrupt/foreign state is refused, never overwritten or reset.
class SICNU_AGENT_EXPORT ConversationCheckpointStore final
{
  public:
    static constexpr qsizetype kMaxCheckpointBytes = 1024 * 1024;
    static constexpr qsizetype kMaxExecutionRefs = 256;
    /// The checkpoint cap excludes this closed envelope's bounded metadata.
    static constexpr qsizetype kMaxStoreBytes = kMaxCheckpointBytes + 256 * 1024;

    explicit ConversationCheckpointStore( const QString &absoluteDirectory );

    /// The caller requests this only after SDK settled/idle AND native host idle.
    /// checkpointJson must already pass the Node module's full entry validation
    /// and secret redaction. This facade checks the closed product envelope,
    /// stable bindings, size and final serialized payload checksum.
    ConversationCheckpointWriteResult saveCompleted(
      const ConversationCheckpointBinding &binding, const QByteArray &checkpointJson ) const;

    /// Persist before submitting a new prompt or admitting its first native
    /// task. Preserves the previous checkpoint; a different unresolved marker
    /// must be explicitly acknowledged before another run can begin.
    ConversationCheckpointWriteResult markRunStarted(
      const ConversationCheckpointBinding &binding, const QString &runId ) const;

    /// Merge safe native identities into the exact incomplete run. No task
    /// parameters, approvals, logs, profile or result payload is stored. Empty
    /// refs never prove zero executions; the dispatch-to-observation crash
    /// window remains explicitly unknown to recovery callers.
    ConversationCheckpointWriteResult recordOwnedExecutions(
      const ConversationCheckpointBinding &binding, const QString &runId,
      const QList<ConversationExecutionReference> &refs ) const;

    /// Called after the host has reconciled an interruption. Clears only the
    /// exact named marker, retaining the checkpoint and performing no execution.
    ConversationCheckpointWriteResult acknowledgeInterruption(
      const ConversationCheckpointBinding &binding, const QString &runId ) const;

    /// Read-only. Does not create the directory, remove markers or start work.
    ConversationCheckpointLoadResult load( const ConversationCheckpointBinding &binding ) const;

  private:
    QString m_directory;
};

} // namespace sicnu::agent::runtime
