// exclusive_path_lock.h — crash-robust exclusive advisory lock over one
// file path (R27-T2, issue #1766 / RIO-5).
//
// ResumableTileRun (and any stateful scratch/journal driver) must be the
// SINGLE owner of its state paths: two concurrent executes against one
// statePath interleave journal appends, re-consume tiles and wipe each
// other's directories. The lock that enforces this must survive its holder
// CRASHING without cleanup — which rules out both QLockFile (Qt-free is a
// hard library boundary for sicnu_runtime) and PID-file schemes (stale
// after a crash, heuristics to steal). OS advisory locks are released by
// the kernel when the owning process dies: exactly the deterministic
// recovery the resume contract needs.
//
//   POSIX:    open(O_RDWR|O_CREAT|O_CLOEXEC) + flock(LOCK_EX|LOCK_NB)
//   Windows:  CreateFileW + LockFileEx(EXCLUSIVE|FAIL_IMMEDIATELY)
//
// Contract:
//   * tryAcquire is non-blocking: exactly one holder per path per moment;
//     everyone else gets a typed refusal (fail-closed — this library never
//     silently queues).
//   * the lock file is created once and NEVER unlinked: unlinking lets a
//     racing opener lock a fresh inode beside a still-held deleted one
//     (two "owners"). A zero-byte residue file is the accepted cost.
//   * crash of the holder releases the lock at process death (kernel);
//     the residue file stays but locks nothing.
//   * re-entrant acquisition by the SAME process through a SECOND instance
//     conflicts too (separate open descriptions), so two threads in one
//     app are mutually excluded the same way two processes are.
//   * scope: LOCAL filesystems. Linux NFS flock() is client-local by
//     default — two processes on DIFFERENT hosts sharing a scratch dir can
//     both "acquire" (fcntl/F_SETLK would be the network-visible form;
//     out of scope until a network-scratch use case exists).
#pragma once

#include <string>

namespace sicnu::runtime::chunk
{

class ExclusivePathLock
{
  public:
    ExclusivePathLock() = default;
    ~ExclusivePathLock();

    ExclusivePathLock( const ExclusivePathLock & ) = delete;
    ExclusivePathLock &operator=( const ExclusivePathLock & ) = delete;
    ExclusivePathLock( ExclusivePathLock &&other ) noexcept;
    ExclusivePathLock &operator=( ExclusivePathLock &&other ) noexcept;

    /// Typed refusal reason when tryAcquire fails.
    enum class Refusal
    {
        None = 0,       ///< acquired
        HeldElsewhere,  ///< another open description holds the OS lock
        OpenFailed,     ///< could not open/create the lock file (typed OS error)
        LockFailed      ///< the lock syscall itself failed (typed OS error)
    };

    /// Attempts to take the exclusive OS lock on @p lockFilePath
    /// (created when missing). Never blocks. On success the lock is held
    /// until release()/destruction. @p errorOut receives the OS error text
    /// for OpenFailed/LockFailed.
    Refusal tryAcquire( const std::string &lockFilePath, std::string &errorOut );

    /// True while this instance holds the lock.
    bool held() const { return m_fd != kInvalid; }

    /// Releases the OS lock (idempotent, non-throwing).
    void release();

    /// Stable name of a refusal for diagnostics/tests.
    static const char *refusalName( Refusal r );

  private:
#if defined( _WIN32 )
    using Handle = void *;
    static constexpr Handle kInvalid = nullptr;
    Handle m_handle = kInvalid;
#else
    using Handle = int;
    static constexpr Handle kInvalid = -1;
    Handle m_fd = kInvalid;
#endif
    std::string m_path;
};

} // namespace sicnu::runtime::chunk
