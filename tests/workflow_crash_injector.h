// tests/workflow_crash_injector.h — parent-side fixture for the Track 10
// crash-recovery matrix. Spawns tests/workflow_crash_helper, waits for
// barrier files, kills with SIGKILL at the chosen injection point, and reads
// the on-disk truth back INDEPENDENTLY of the recovery code (raw JSON for
// the committed-step set; WorkflowCheckpointManager::loadCheckpoint for the
// parse gate). RAII: a live child is killed on scope exit so a failing
// assertion can never leak a lock-holding process into the next test.
#pragma once

#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <json/json.h>

#include <memory>

class QProcess;

namespace sicnu::workflow {

class WorkflowCrashInjector
{
  public:
    /// @param scratchDir shared run directory (barriers + checkpoints + outputs).
    explicit WorkflowCrashInjector( const QString &scratchDir );
    ~WorkflowCrashInjector();

    /// Resolves the helper binary (compile-time path, injected by CMake).
    static QString helperPath();

    /// Spawns @p args and waits (bounded) for @p barrierName to appear.
    /// Returns false on start failure or timeout (child is killed first).
    bool spawnUntilBarrier( const QStringList &args, const std::string &barrierName,
                            int timeoutMs = 60000 );
    /// Barrier existence probe (polling primitive reused by tests).
    bool barrierExists( const std::string &barrierName ) const;
    /// SIGKILL + reap. Idempotent; safe on an exited child.
    void killChild();
    bool childRunning();

    /// The runId line the helper prints ("RUN <runId> <pipelineId>"), or the
    /// empty string when not (yet) seen.
    QString runIdLine() const;

    // ---- on-disk truth readers (independent of any recovery code) --------
    /// Raw JSON read of the run's checkpoint; null member when absent.
    Json::Value readCheckpointJson( const std::string &runId ) const;
    /// Committed set = stepIds whose stepPlan status is "Completed" in the
    /// checkpoint file on disk (the journal's committed set).
    QStringList committedStepsOnDisk( const std::string &runId ) const;
    /// Top-level "state" string of the on-disk checkpoint.
    QString checkpointStateOnDisk( const std::string &runId ) const;
    /// checkpoint_<runId>.json.tmp.* residue count (torn-save evidence).
    int tmpResidueCount() const;
    /// Runs WorkflowCheckpointManager::loadCheckpoint — the same parse gate
    /// recovery uses — for the "previous intact version survives" asserts.
    bool checkpointLoads( const std::string &runId ) const;
    QString checkpointPath( const std::string &runId ) const;
    QString archivedCheckpointPath( const std::string &runId ) const;

  private:
    QString barrierPath( const std::string &barrierName ) const;

    QString m_dir;
    std::unique_ptr<QProcess> m_process;
    QString m_runIdLine;
};

} // namespace sicnu::workflow
