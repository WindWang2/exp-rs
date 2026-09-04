// src/workflow/workflow_run_lock.h — cross-process ownership of a WorkflowRun
//
// Issue #727: a WorkflowRun being executed by a live process must not be
// reconciled to Interrupted (or resumed) by another process; after the owner
// dies, the run must become recoverable again — automatically, without
// stale-lock heuristics.
//
// Design: one lock file per run next to its checkpoint,
//   <checkpointDir>/checkpoint_<runId>.lock
// On Q_OS_UNIX the lock is an advisory flock(2) held on an open descriptor
// for the whole execution: the kernel releases it on ANY process death, so
// there is no stale state to recover, and ownership follows the open file
// description — a reused PID can never be mistaken for the original owner.
// On other platforms a QLockFile (Qt) with the same interface is used; its
// stale detection is Qt's own (pid + machine id), kept behind the identical
// API so callers stay platform-neutral.
//
// The lock file also carries owner metadata (pid, hostname, boot id, token,
// acquisition time) written by the holder for display/diagnostics. Liveness
// is ALWAYS decided by the lock primitive, never by the recorded pid.
// Lock files are never unlinked (an unlink-while-held race would reopen the
// ownership hole); they are ~40 bytes, one per run ever executed.
#pragma once

#include <QString>

#include <memory>

class QLockFile;

namespace sicnu::workflow {

class WorkflowRunLock
{
  public:
    enum class TryResult
    {
        Acquired,         ///< we now own the run
        HeldByLiveOwner,  ///< another live process owns the run
        Error             ///< could not create/open the lock file
    };

    static QString lockPathForRun( const QString &checkpointDirectory, const std::string &runId );

    explicit WorkflowRunLock( const QString &lockFilePath );
    ~WorkflowRunLock();
    WorkflowRunLock( const WorkflowRunLock & ) = delete;
    WorkflowRunLock &operator=( const WorkflowRunLock & ) = delete;

    /// Non-blocking acquisition. On HeldByLiveOwner, @a heldByPid (when
    /// non-null) receives the pid recorded by the current holder, if any.
    TryResult tryAcquire( QString *heldByPid = nullptr );
    /// Release. Safe to call when not held; called by the destructor.
    void release();
    bool isHeld() const;

    /// Owner metadata written by the current holder (empty when none) — for
    /// display and diagnostics only, never for liveness decisions.
    QString ownerInfoLine() const;

    struct OwnerProbe
    {
        enum class State { NoHolder, LiveOwner, Unknown };
        State state = State::Unknown;
        qint64 pid = 0;  ///< recorded holder pid (0 when unknown)
    };
    /// Lock-free probe for read-only surfaces (--list-runs): NoHolder means
    /// nobody holds the lock (no live owner); LiveOwner means a process is
    /// holding it right now.
    static OwnerProbe probeOwner( const QString &lockFilePath );

  private:
    QString m_path;
#if defined( Q_OS_UNIX )
    int m_fd = -1;
#else
    std::unique_ptr<QLockFile> m_qtLock;
#endif
};

} // namespace sicnu::workflow
