// worker_process_guard.cpp — see worker_process_guard.h for the contract.
#include "worker_process_guard.h"

#include <QProcess>

#if defined( Q_OS_WIN )
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace sicnu::processing
{

WorkerProcessGuard::~WorkerProcessGuard()
{
#if defined( Q_OS_WIN )
    // Closing the job handle triggers JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE:
    // the kernel terminates any remaining processes in the tree. This also
    // covers host death — an orphaned worker cannot outlive the handle.
    if ( m_platformHandle )
    {
        const HANDLE job = static_cast<HANDLE>( m_platformHandle );
        TerminateJobObject( job, static_cast<UINT>( -1 ) );
        CloseHandle( job );
        m_platformHandle = nullptr;
    }
#endif
}

void WorkerProcessGuard::attach( QProcess &process )
{
#if !defined( Q_OS_WIN )
#if QT_VERSION >= QT_VERSION_CHECK( 6, 7, 0 )
    // setsid(2): the worker becomes a session and process-group leader, so
    // the whole operator tree signals as one group (pgid == pid).
    process.setUnixProcessParameters( QProcess::UnixProcessFlag::CreateNewSession );
    m_armed = true;
#else
    process.setChildProcessModifier( []() { ::setpgid( 0, 0 ); } );
    m_armed = true;
#endif
#else
    Q_UNUSED( process ); // Windows containment arms after start (job object)
#endif
}

bool WorkerProcessGuard::armAfterStart( QProcess &process )
{
#if defined( Q_OS_WIN )
    if ( m_platformHandle )
        return true; // already armed (defensive)
    const qint64 pid = process.processId();
    if ( pid <= 0 )
        return false;
    HANDLE job = CreateJobObjectW( nullptr, nullptr );
    if ( !job )
        return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if ( !SetInformationJobObject( job, JobObjectExtendedLimitInformation,
                                   &limits, sizeof( limits ) ) )
    {
        CloseHandle( job );
        return false;
    }
    HANDLE child = OpenProcess( PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                static_cast<DWORD>( pid ) );
    if ( !child )
    {
        CloseHandle( job );
        return false;
    }
    const BOOL assigned = AssignProcessToJobObject( job, child );
    CloseHandle( child );
    if ( !assigned )
    {
        CloseHandle( job );
        return false;
    }
    m_platformHandle = job;
    m_armed = true;
    return true;
#else
    // POSIX: the group was established at spawn; nothing to arm.
    Q_UNUSED( process );
    return m_armed;
#endif
}

bool WorkerProcessGuard::terminateTree( QProcess &process, int graceWaitMs, int killWaitMs )
{
    if ( process.state() != QProcess::Running )
        return true;

#if defined( Q_OS_WIN )
    const HANDLE job = m_platformHandle ? static_cast<HANDLE>( m_platformHandle ) : nullptr;
    if ( job )
    {
        // Job termination is immediate and tree-wide; the caller's grace
        // window was already consumed by the cooperative cancel frame.
        TerminateJobObject( job, static_cast<UINT>( -1 ) );
    }
    else
    {
        process.terminate();
    }
    if ( process.waitForFinished( graceWaitMs ) )
        return true;
    if ( job )
        TerminateJobObject( job, static_cast<UINT>( -1 ) );
    process.kill();
    return process.waitForFinished( killWaitMs );
#else
    const qint64 pid = process.processId();
    if ( pid > 0 && m_armed )
    {
        // Group signal first (covers operator-spawned helpers), then the
        // direct pid as belt-and-braces (harmless duplicate for the leader).
        ::kill( static_cast<pid_t>( -pid ), SIGTERM );
        ::kill( static_cast<pid_t>( pid ), SIGTERM );
        if ( process.waitForFinished( graceWaitMs ) )
            return true;
        ::kill( static_cast<pid_t>( -pid ), SIGKILL );
        ::kill( static_cast<pid_t>( pid ), SIGKILL );
        return process.waitForFinished( killWaitMs );
    }
    // Containment not armed: pre-8.0 behavior (escalate on the process only).
    process.terminate();
    if ( process.waitForFinished( graceWaitMs ) )
        return true;
    process.kill();
    return process.waitForFinished( killWaitMs );
#endif
}

} // namespace sicnu::processing
