// worker_process_guard.h — OS-level containment for worker processes
// (Execution Plane 8.0, WP-C).
//
// The worker protocol's stdin-EOF contract makes a worker exit when the host
// dies — but only while the worker is still reading its stdin. A worker stuck
// inside a misbehaving operator never returns to its read loop and survives
// the host, leaving an orphan process (7.0 known limitation #2). Worse, an
// operator that spawns helper processes of its own leaves those behind even
// when the worker itself exits.
//
// The guard binds each worker to a kill-on-close OS construct so the whole
// tree dies with the host:
//   - POSIX: the child calls setsid(2) at spawn (QProcess CreateNewSession;
//     Qt >= 6.7, the project's floor is 6.8), making it a session AND process
//     group leader (pgid == pid). Escalation/teardown signal the whole group
//     (kill -pid), so operator-spawned grandchildren are covered. The
//     pre-6.7 fallback is setpgid(0,0) via the child process modifier.
//   - Windows: the child is assigned to a Job Object with
//     JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE right after start. Closing the
//     handle — at teardown, or whenever the host process dies for any
//     reason — makes the kernel terminate the whole tree. Assignment happens
//     just after CreateProcess (QProcess starts the child synchronously on
//     Windows); a child spawning helpers within that first millisecond would
//     predate the assignment — accepted, documented window.
//
// The guard is an executor-side helper. It is NOT a scheduler, adds no wire
// traffic, and changes no worker behavior.
#pragma once

#include <QtGlobal>

class QProcess;

namespace sicnu::processing
{

class WorkerProcessGuard
{
  public:
    WorkerProcessGuard() = default;
    ~WorkerProcessGuard();
    WorkerProcessGuard( const WorkerProcessGuard & ) = delete;
    WorkerProcessGuard &operator=( const WorkerProcessGuard & ) = delete;

    /// Configures @p process for containment BEFORE start(): POSIX gets the
    /// new-session/process-group spawn parameters; Windows configures nothing
    /// yet (the job assignment happens in armAfterStart).
    void attach( QProcess &process );

    /// Must be called after start() succeeded (waitForStarted()). On Windows
    /// creates the Job Object and assigns the child. Returns false when the
    /// containment could not be armed (escalation then falls back to plain
    /// QProcess kill — same behavior as pre-8.0, never a silent "contained"
    /// claim).
    bool armAfterStart( QProcess &process );

    /// True when OS containment is armed (POSIX: group leader spawn requested;
    /// Windows: job assignment succeeded).
    bool isArmed() const { return m_armed; }

    /// Terminates the contained tree with a bounded grace ladder:
    /// group/job termination signal → wait → hard kill → wait. Returns true
    /// when the process observed an exit (QProcess state reaped) within the
    /// waits. Falls back to plain QProcess::terminate()/kill() semantics when
    /// containment is not armed. Never throws; safe on a thread different
    /// from the spawner (the kill syscalls are thread-agnostic; reaping via
    /// QProcess::waitForFinished is reentrant, matching existing pool usage).
    bool terminateTree( QProcess &process, int graceWaitMs = 1000, int killWaitMs = 3000 );

  private:
    /// Windows: the Job Object HANDLE (void* to keep the header
    /// platform-clean). POSIX: unused. A plain member (not a pimpl) so the
    /// guard stays default-constructible in-place inside pool workers
    /// without instantiating deleters on an incomplete type.
    void *m_platformHandle = nullptr;
    bool m_armed = false;
};

} // namespace sicnu::processing
