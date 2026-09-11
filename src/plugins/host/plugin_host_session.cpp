/***************************************************************************
 * src/plugins/host/plugin_host_session.cpp
 ***************************************************************************/
#include "plugin_host_session.h"

#include "plugin_host_protocol.h"

#include "exprs/ipc_stream.h"
#include "exprs/version.h"

#include <condition_variable>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace exprs;
using namespace sicnu::plugins;
using namespace sicnu::plugins::hostprotocol;

namespace {

constexpr int kKillGraceMs = 3000;

#ifdef _WIN32
std::string handleToString( void *handle )
{
    char buffer[ 32 ];
    std::snprintf( buffer, sizeof( buffer ), "%llx",
                   static_cast<unsigned long long>( reinterpret_cast<uintptr_t>( handle ) ) );
    return buffer;
}
#else
std::string fdToString( int fd )
{
    return std::to_string( fd );
}
#endif

} // namespace

// -- ConcurrencyGate ---------------------------------------------------------

bool ConcurrencyGate::acquire( int timeoutMs )
{
    std::unique_lock<std::mutex> lock( mMutex );
    const unsigned long long ticket = mNextTicket++;
    mWaiters.push_back( ticket );
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds( timeoutMs > 0 ? timeoutMs : 0 );
    for ( ;; )
    {
        // FIFO: only the front waiter may take a freed slot.
        if ( mWaiters.front() == ticket && mActive < mSlots )
        {
            mWaiters.pop_front();
            ++mActive;
            return true;
        }
        if ( mWaiters.front() != ticket )
        {
            // Someone ahead of us is waiting; they take precedence.
            mCv.wait( lock );
            continue;
        }
        if ( std::chrono::steady_clock::now() >= deadline )
        {
            // Bounded refusal: leave the queue (we are the front, so this
            // cannot starve anyone behind us).
            mWaiters.pop_front();
            mCv.notify_all();
            return false;
        }
        mCv.wait_until( lock, deadline );
    }
}

void ConcurrencyGate::release()
{
    std::lock_guard<std::mutex> lock( mMutex );
    --mActive;
    mCv.notify_all();
}

PluginHostProcessSession::~PluginHostProcessSession()
{
    killProcess( "session destroyed" );
}

std::shared_ptr<PluginHostProcessSession> PluginHostProcessSession::spawn(
    const SpawnOptions &options, PluginDiagnosticLog &diagnostics )
{
    if ( options.workerPath.empty() || options.pluginId.empty() )
    {
        diagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                         PluginDiagnosticSeverity::Error,
                         "host-process runtime is not configured (no worker binary)", options.pluginId );
        return {};
    }
#ifdef _WIN32
    const DWORD attributes = ::GetFileAttributesA( options.workerPath.c_str() );
    if ( attributes == INVALID_FILE_ATTRIBUTES )
    {
        diagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                         PluginDiagnosticSeverity::Error,
                         "worker binary not found: " + options.workerPath, options.pluginId );
        return {};
    }
#endif

    auto session = std::shared_ptr<PluginHostProcessSession>( new PluginHostProcessSession() );
    session->mOptions = options;
    session->mGate.setWidth( options.quota.maxRequestConcurrency );
    if ( !session->spawnWorkerProcess( diagnostics ) )
        return {};
    if ( !session->awaitHandshake( diagnostics ) )
    {
        session->killProcess( "handshake failed" );
        return {};
    }
    return session;
}

bool PluginHostProcessSession::spawnWorkerProcess( PluginDiagnosticLog &diagnostics )
{
    // ---- protocol pipes: host->worker (worker reads), worker->host (worker
    // writes). Only the CHILD ends are inheritable; the handshake validates
    // the whole chain before any plugin code runs in the worker.
#ifdef _WIN32
    SECURITY_ATTRIBUTES inherit;
    inherit.nLength = sizeof( inherit );
    inherit.bInheritHandle = TRUE;
    inherit.lpSecurityDescriptor = nullptr;
    HANDLE hostToWorkerRead = nullptr;
    HANDLE hostToWorkerWrite = nullptr;
    HANDLE workerToHostRead = nullptr;
    HANDLE workerToHostWrite = nullptr;
    if ( !::CreatePipe( &hostToWorkerRead, &hostToWorkerWrite, &inherit, 0 )
         || !::CreatePipe( &workerToHostRead, &workerToHostWrite, &inherit, 0 ) )
    {
        // Partial failure: close whatever exists (respawn retries land here).
        const DWORD pipeError = ::GetLastError();
        for ( HANDLE handle : { hostToWorkerRead, hostToWorkerWrite, workerToHostRead,
                                workerToHostWrite } )
            if ( handle )
                ::CloseHandle( handle );
        diagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                         PluginDiagnosticSeverity::Error,
                         "CreatePipe failed: " + std::to_string( pipeError ),
                         mOptions.pluginId );
        return false;
    }
    ::SetHandleInformation( hostToWorkerWrite, HANDLE_FLAG_INHERIT, 0 );
    ::SetHandleInformation( workerToHostRead, HANDLE_FLAG_INHERIT, 0 );

    mJobHandle = ::CreateJobObjectW( nullptr, nullptr );
    if ( mJobHandle )
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        ZeroMemory( &limits, sizeof( limits ) );
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if ( mOptions.quota.maxChildProcesses > 0 )
        {
            limits.BasicLimitInformation.ActiveProcessLimit =
                static_cast<DWORD>( mOptions.quota.maxChildProcesses );
            limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        }
        if ( mOptions.quota.workerMemoryBytes > 0 )
        {
            limits.ProcessMemoryLimit =
                static_cast<SIZE_T>( mOptions.quota.workerMemoryBytes );
            limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        }
        ::SetInformationJobObject( mJobHandle, JobObjectExtendedLimitInformation, &limits,
                                   sizeof( limits ) );
        if ( mOptions.quota.workerCpuRatePercent > 0 )
        {
            JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuRate;
            ZeroMemory( &cpuRate, sizeof( cpuRate ) );
            cpuRate.CpuRate = static_cast<DWORD>( mOptions.quota.workerCpuRatePercent ) * 100;
            cpuRate.ControlFlags =
                JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
            ::SetInformationJobObject( mJobHandle, JobObjectCpuRateControlInformation, &cpuRate,
                                       sizeof( cpuRate ) );
        }
    }

    const std::string commandLine = "\"" + mOptions.workerPath + "\" " + kIpcReadSwitch
                                    + handleToString( hostToWorkerRead ) + " " + kIpcWriteSwitch
                                    + handleToString( workerToHostWrite );
    // Proper UTF-8 widening: naive char-to-wchar_t widening breaks every
    // non-ASCII install path (localized user directories are the norm).
    const int wideLen = MultiByteToWideChar( CP_UTF8, 0, commandLine.c_str(),
                                             static_cast<int>( commandLine.size() ), nullptr, 0 );
    std::wstring commandWide( wideLen > 0 ? static_cast<size_t>( wideLen ) : 0, L'\0' );
    if ( wideLen > 0 )
        MultiByteToWideChar( CP_UTF8, 0, commandLine.c_str(),
                             static_cast<int>( commandLine.size() ), commandWide.data(),
                             wideLen );

    STARTUPINFOEXW startupInfo;
    ZeroMemory( &startupInfo, sizeof( startupInfo ) );
    startupInfo.StartupInfo.cb = sizeof( startupInfo );
    HANDLE inheritList[ 2 ] = { hostToWorkerRead, workerToHostWrite };
    SIZE_T attributeSize = 0;
    ::InitializeProcThreadAttributeList( nullptr, 1, 0, &attributeSize );
    std::vector<char> attributeBuffer( attributeSize );
    startupInfo.lpAttributeList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( attributeBuffer.data() );
    const bool attributesOk =
        ::InitializeProcThreadAttributeList( startupInfo.lpAttributeList, 1, 0, &attributeSize )
        && ::UpdateProcThreadAttribute( startupInfo.lpAttributeList, 0,
                                        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritList,
                                        sizeof( inheritList ), nullptr, nullptr );

    PROCESS_INFORMATION processInfo;
    ZeroMemory( &processInfo, sizeof( processInfo ) );
    const BOOL created =
        attributesOk
            ? ::CreateProcessW( nullptr, commandWide.data(), nullptr, nullptr, TRUE,
                                CREATE_SUSPENDED | CREATE_NO_WINDOW
                                    | EXTENDED_STARTUPINFO_PRESENT,
                                nullptr, nullptr, &startupInfo.StartupInfo, &processInfo )
            : FALSE;
    if ( attributesOk )
        ::DeleteProcThreadAttributeList( startupInfo.lpAttributeList );
    if ( !created )
    {
        const DWORD error = ::GetLastError();
        diagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                         PluginDiagnosticSeverity::Error,
                         attributesOk ? "spawning exprs_plugin_host_worker failed with error "
                                          + std::to_string( error )
                                      : "worker process attribute list initialization failed",
                         mOptions.pluginId );
        ::CloseHandle( hostToWorkerRead );
        ::CloseHandle( hostToWorkerWrite );
        ::CloseHandle( workerToHostRead );
        ::CloseHandle( workerToHostWrite );
        if ( mJobHandle )
        {
            ::CloseHandle( mJobHandle );
            mJobHandle = nullptr;
        }
        return false;
    }
    if ( mJobHandle )
        ::AssignProcessToJobObject( mJobHandle, processInfo.hProcess );
    ::ResumeThread( processInfo.hThread );
    ::CloseHandle( processInfo.hThread );
    mProcessHandle = processInfo.hProcess;
    mProcessAlive = true;

    // The launcher's copies of the CHILD-side pipe ends MUST be closed now:
    // as long as we hold them, the pipes never break when the worker dies
    // and crash detection (EOF) never fires — the exact external-process
    // bug class external_process.cpp already closes correctly.
    ::CloseHandle( hostToWorkerRead );
    ::CloseHandle( workerToHostWrite );

    // Protocol channel over the launcher-side ends; capture the handshake.
    mChannel = std::make_unique<IpcChannel>(
        makeIpcHandleStream( workerToHostRead, hostToWorkerWrite ) );
    mChannel->setEventSink( [this]( const Ipc::Envelope &event ) {
        if ( event.method != kWorkerHello )
            return;
        std::lock_guard<std::mutex> lock( mHelloMutex );
        mHello = event.params;
        mHelloReceived = true;
        mHelloCv.notify_all();
    } );
    return true;
#else
    int hostToWorker[ 2 ] = { -1, -1 };
    int workerToHost[ 2 ] = { -1, -1 };
    if ( ::pipe( hostToWorker ) != 0 || ::pipe( workerToHost ) != 0 )
    {
        diagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                         PluginDiagnosticSeverity::Error, "pipe() failed",
                         mOptions.pluginId );
        return false;
    }

    // Everything the child touches is computed BEFORE fork: the child
    // between fork and exec must stay allocation-free (a multithreaded
    // parent's heap cannot be safely touched there).
    long long memoryLimit = mOptions.quota.workerMemoryBytes;
    const std::string fdRead = std::string( kIpcReadSwitch ) + fdToString( 3 );
    const std::string fdWrite = std::string( kIpcWriteSwitch ) + fdToString( 4 );
    const std::string workerPath = mOptions.workerPath;
    std::vector<char> execPath( workerPath.begin(), workerPath.end() );
    execPath.push_back( '\0' );
    std::vector<char> execRead( fdRead.begin(), fdRead.end() );
    execRead.push_back( '\0' );
    std::vector<char> execWrite( fdWrite.begin(), fdWrite.end() );
    execWrite.push_back( '\0' );

    // Parent-side memory-ceiling sanity: a request above the hard limit
    // would silently apply no bound in the child.
    if ( memoryLimit > 0 )
    {
        struct ::rlimit addressSpace;
        if ( ::getrlimit( RLIMIT_AS, &addressSpace ) == 0
             && addressSpace.rlim_max != RLIM_INFINITY
             && static_cast<unsigned long long>( memoryLimit )
                    > static_cast<unsigned long long>( addressSpace.rlim_max ) )
        {
            diagnostics.add( PluginDiagnosticCode::ManifestInvalidField,
                             PluginDiagnosticSeverity::Warning,
                             "quota workerMemoryBytes exceeds the RLIMIT_AS hard limit ("
                                 + std::to_string( addressSpace.rlim_max )
                                 + "); no memory bound will apply",
                             mOptions.pluginId );
        }
    }

    const pid_t pid = ::fork();
    if ( pid < 0 )
    {
        diagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                         PluginDiagnosticSeverity::Error, "fork() failed",
                         mOptions.pluginId );
        return false;
    }
    if ( pid == 0 )
    {
        // Process group of our own: the launcher's kill ladder can take
        // down worker-spawned grandchildren with one kill(-pid). Must run
        // before anything that could fail and leak the group.
        ::setpgid( 0, 0 );
        ::dup2( hostToWorker[ 0 ], 3 );
        ::dup2( workerToHost[ 1 ], 4 );
        ::fcntl( 3, F_SETFD, 0 );
        ::fcntl( 4, F_SETFD, 0 );
        for ( int fd : { hostToWorker[ 0 ], hostToWorker[ 1 ], workerToHost[ 0 ],
                         workerToHost[ 1 ] } )
            if ( fd > 4 )
                ::close( fd );
        if ( memoryLimit > 0 )
        {
            // Coarse best-effort bound (address space, not RSS); enforced
            // by the kernel before exec. Documented in capabilities.md.
            struct ::rlimit addressSpace;
            addressSpace.rlim_cur = static_cast<rlim_t>( memoryLimit );
            addressSpace.rlim_max = static_cast<rlim_t>( memoryLimit );
            ::setrlimit( RLIMIT_AS, &addressSpace );
        }
        ::execl( execPath.data(), execPath.data(), execRead.data(), execWrite.data(),
                 static_cast<char *>( nullptr ) );
        ::_exit( 127 );
    }
    // Host-side ends never leak into later spawns: with several concurrent
    // workers, a child that inherited OTHER sessions' pipe ends could write
    // frames into their streams and would defeat their EOF crash detection.
    ::fcntl( hostToWorker[ 1 ], F_SETFD, FD_CLOEXEC );
    ::fcntl( workerToHost[ 0 ], F_SETFD, FD_CLOEXEC );
    ::close( hostToWorker[ 0 ] );
    ::close( workerToHost[ 1 ] );
    mProcessHandle = reinterpret_cast<void *>( static_cast<intptr_t>( pid ) );
    mProcessGroupId = static_cast<long long>( pid );
    mProcessAlive = true;
    mChannel = std::make_unique<IpcChannel>(
        makeIpcHandleStream( reinterpret_cast<void *>( workerToHost[ 0 ] ),
                             reinterpret_cast<void *>( hostToWorker[ 1 ] ) ) );
    mChannel->setEventSink( [this]( const Ipc::Envelope &event ) {
        if ( event.method != kWorkerHello )
            return;
        std::lock_guard<std::mutex> lock( mHelloMutex );
        mHello = event.params;
        mHelloReceived = true;
        mHelloCv.notify_all();
    } );
    return true;
#endif
}

bool PluginHostProcessSession::awaitHandshake( PluginDiagnosticLog &diagnostics )
{
    std::unique_lock<std::mutex> lock( mHelloMutex );
    const bool received = mHelloCv.wait_for( lock, std::chrono::milliseconds( mOptions.handshakeTimeoutMs ),
                                             [this] { return mHelloReceived || !mProcessAlive; } );
    if ( !received || !mHelloReceived )
    {
        diagnostics.add( PluginDiagnosticCode::IpcProtocolError,
                         PluginDiagnosticSeverity::Error,
                         "worker did not send worker.hello within "
                             + std::to_string( mOptions.handshakeTimeoutMs ) + " ms",
                         mOptions.pluginId );
        return false;
    }
    std::string reason;
    const int peerMajor = mHello.get( "protocolMajor", 0 ).asInt();
    const int peerMinor = mHello.get( "protocolMinor", 0 ).asInt();
    if ( !Ipc::isProtocolCompatible( hostProtocolVersionMajor(), hostProtocolVersionMinor(),
                                     peerMajor, peerMinor, reason ) )
    {
        diagnostics.add( PluginDiagnosticCode::IpcProtocolVersionMismatch,
                         PluginDiagnosticSeverity::Error,
                         "worker protocol " + std::to_string( peerMajor ) + "."
                             + std::to_string( peerMinor ) + " incompatible: " + reason,
                         mOptions.pluginId );
        return false;
    }
    // The plugin API/ABI axes must ALSO agree: the worker maps the binary
    // with the same V1 ABI gates the in-process loader enforces.
    const std::string workerApi = mHello.get( "apiVersion", "" ).asString();
    const int workerAbi = mHello.get( "abiVersion", 0 ).asInt();
    if ( workerApi != EXP_RS_PLUGIN_API_VERSION || workerAbi != pluginAbiVersion() )
    {
        diagnostics.add( PluginDiagnosticCode::AbiVersionMismatch,
                         PluginDiagnosticSeverity::Error,
                         "worker SDK (api " + workerApi + ", abi "
                             + std::to_string( workerAbi ) + ") does not match the host SDK (api "
                             + std::string( EXP_RS_PLUGIN_API_VERSION ) + ", abi "
                             + std::to_string( pluginAbiVersion() ) + ")",
                         mOptions.pluginId );
        return false;
    }
    // Protocol 1.1: informational worker dispatch width (a v1.0 worker
    // omits the field → 1, i.e. serialized dispatch; the host gate still
    // protects the host, the worker just serves slower).
    const int workerConcurrent = mHello.get( "maxConcurrentRequests", 1 ).asInt();
    {
        std::lock_guard<std::mutex> stateLock( mStateMutex );
        mWorkerMaxConcurrent = workerConcurrent > 0 ? workerConcurrent : 1;
    }
    return true;
}

bool PluginHostProcessSession::isAlive() const
{
    return mProcessAlive;
}

int PluginHostProcessSession::effectiveConcurrency() const
{
    std::lock_guard<std::mutex> stateLock( mStateMutex );
    return std::min( mOptions.quota.maxRequestConcurrency, mWorkerMaxConcurrent );
}

void PluginHostProcessSession::killProcess( const char *reason )
{
    (void)reason;
    // NOTE: this must do useful work even when the worker already died by
    // ITSELF (crash): the process group still holds worker-spawned
    // grandchildren, and the Windows job handle must close so kill-on-close
    // reaps them. The early-return guard of v1 orphaned exactly that.
#ifdef _WIN32
    const bool wasAlive = mProcessAlive.exchange( false );
    if ( wasAlive )
    {
        if ( mJobHandle )
            ::TerminateJobObject( mJobHandle, 9 );
        else if ( mProcessHandle )
            ::TerminateProcess( mProcessHandle, 9 );
        if ( mProcessHandle )
            ::WaitForSingleObject( mProcessHandle, 5000 );
        // ONLY the exchange winner touches the handles: a concurrent loser
        // must not close a handle this thread is waiting on (documented UB,
        // recycled-handle hazard). Kill-on-close of the job reaps survivors.
        if ( mProcessHandle )
        {
            ::CloseHandle( mProcessHandle );
            mProcessHandle = nullptr;
        }
        if ( mJobHandle )
        {
            // Closing the last job handle fires JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE:
            // every survivor in the job (worker-spawned children included) dies.
            ::CloseHandle( mJobHandle );
            mJobHandle = nullptr;
        }
    }
#else
    const bool wasAlive = mProcessAlive.exchange( false );
    const pid_t pid = static_cast<pid_t>( reinterpret_cast<intptr_t>( mProcessHandle ) );
    if ( wasAlive && pid > 0 )
    {
        // Process-group kill first: worker-spawned grandchildren (fork/exec
        // inside plugin code) die WITH the worker, not as orphans. The
        // group exists because the child ran setpgid(0,0) before exec.
        if ( mProcessGroupId > 0 )
            ::kill( static_cast<pid_t>( -mProcessGroupId ), SIGKILL );
        ::kill( pid, SIGKILL );
        int status = 0;
        ::waitpid( pid, &status, 0 );
        mProcessHandle = nullptr;
        mProcessGroupId = -1;
    }
    else if ( mProcessGroupId > 0 )
    {
        // Worker died by itself (crash): reap the rest of its group. POSIX
        // kills are idempotent (ESRCH), so a concurrent loser of the
        // liveness exchange can safely run this too.
        ::kill( static_cast<pid_t>( -mProcessGroupId ), SIGKILL );
        mProcessGroupId = -1;
    }
#endif
    if ( mChannel )
        mChannel->close();
}

void PluginHostProcessSession::escalateTimeout( long long requestId )
{
    // The channel already sent the per-id cancel frame when its local wait
    // timed out (protocol 1.1); this escalation owns grace and force: wait
    // the grace window (a cooperative worker may still finish or exit),
    // then kill when this was the last request in flight, poison otherwise.
    (void)requestId;

    const int grace = mOptions.killGraceMs > 0 ? mOptions.killGraceMs : kKillGraceMs;
    const auto graceDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( grace );
    while ( std::chrono::steady_clock::now() < graceDeadline && mProcessAlive )
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );

    bool forceKill = false;
    {
        std::lock_guard<std::mutex> stateLock( mStateMutex );
        if ( !mProcessAlive )
            return;                       // died during grace: nothing to force
        if ( mInFlight <= 1 )
        {
            forceKill = true;             // last request in flight: v1 ladder
            mPoisoned = false;
        }
        else
        {
            mPoisoned = true;             // peers in flight: kill when drained
            return;                       // kill-on-drain happens in request()
        }
    }
    if ( forceKill )
        killProcess( "request deadline ladder" );
}

IpcChannel::Outcome PluginHostProcessSession::request(
    const std::string &method, const Json::Value &params, int deadlineMs,
    const IpcChannel::CancelPredicate &cancelPredicate, const IpcChannel::ProgressSink &progressSink )
{
    // Quota authority: the ceiling applies regardless of the caller's ask.
    const int effectiveDeadline = deadlineMs > 0 ? std::min( deadlineMs, mOptions.quota.requestDeadlineMs )
                                                 : mOptions.quota.requestDeadlineMs;

    if ( !mChannel || !mProcessAlive )
    {
        IpcChannel::Outcome outcome;
        outcome.status = IpcChannel::Outcome::Status::ChannelClosed;
        outcome.error.code = "E6005";
        outcome.error.message = "worker process is not running";
        outcome.error.retryable = true;
        return outcome;
    }

    // Drain-check first: a poisoned worker whose peers have finished must
    // die before anything else is sent (the next request then fails E6005
    // and the proxy's restart policy brings a fresh worker).
    if ( mPoisoned )
    {
        std::lock_guard<std::mutex> stateLock( mStateMutex );
        if ( mInFlight == 0 && mPoisoned )
        {
            mPoisoned = false;
            killProcess( "poisoned worker drained" );
        }
    }
    if ( !mProcessAlive )
    {
        IpcChannel::Outcome outcome;
        outcome.status = IpcChannel::Outcome::Status::ChannelClosed;
        outcome.error.code = "E6005";
        outcome.error.message = "worker process is not running";
        outcome.error.retryable = true;
        return outcome;
    }

    // Bounded FIFO gate = maxRequestConcurrency (exact enforcement of the
    // quota; overflow refuses typed instead of queueing without bound).
    const int gateBudget = effectiveDeadline;
    if ( !mGate.acquire( gateBudget ) )
    {
        IpcChannel::Outcome outcome;
        outcome.status = IpcChannel::Outcome::Status::Error;
        outcome.error.code = "E6007";
        outcome.error.message = "plugin request concurrency limit ("
                                + std::to_string( mGate.width() )
                                + ") is saturated; request refused (overload protection)";
        return outcome;
    }

    {
        std::lock_guard<std::mutex> stateLock( mStateMutex );
        ++mInFlight;
    }

    IpcChannel::Outcome outcome;
    outcome = mChannel->request( method, params, effectiveDeadline, cancelPredicate, progressSink );

    if ( outcome.status == IpcChannel::Outcome::Status::Timeout )
    {
        // Per-id cancel already went out (channel); this waits the grace
        // window and then kills (sole request) or poisons (peers in flight).
        escalateTimeout( 0 );
        outcome.error.message += "; the request was cancelled (worker killed or scheduled for kill)";
    }

    {
        bool killDrained = false;
        {
            std::lock_guard<std::mutex> stateLock( mStateMutex );
            --mInFlight;
            if ( mInFlight == 0 && mPoisoned && mProcessAlive )
            {
                mPoisoned = false;
                killDrained = true;
            }
        }
        if ( killDrained )
            killProcess( "poisoned worker drained" );
        mGate.release();
    }

    if ( outcome.status == IpcChannel::Outcome::Status::Timeout )
    {
        outcome.error.message += "; the request was cancelled and the worker was killed or poisoned";
    }
    else if ( outcome.status == IpcChannel::Outcome::Status::ChannelClosed && mProcessAlive )
    {
        // Channel EOF while we believed the process lived: confirm death.
#ifdef _WIN32
        const DWORD wait = ::WaitForSingleObject( mProcessHandle, 0 );
        if ( wait == WAIT_OBJECT_0 )
        {
            mProcessAlive = false;
            if ( mJobHandle )
            {
                ::CloseHandle( mJobHandle );
                mJobHandle = nullptr;
            }
            ::CloseHandle( mProcessHandle );
            mProcessHandle = nullptr;
        }
#else
        const pid_t pid = static_cast<pid_t>( reinterpret_cast<intptr_t>( mProcessHandle ) );
        int status = 0;
        const pid_t done = ::waitpid( pid, &status, WNOHANG );
        if ( done == pid )
        {
            mProcessAlive = false;
            mProcessHandle = nullptr;
            // mProcessGroupId deliberately survives: killProcess reaps the
            // worker's process group on the self-dead path (grandchildren).
        }
#endif
        if ( !mProcessAlive )
            killProcess( "confirmed dead after channel close" );
    }
    return outcome;
}

IpcChannel::Outcome PluginHostProcessSession::requestRaw(
    const std::string &method, const Json::Value &params, int deadlineMs,
    const IpcChannel::CancelPredicate &cancelPredicate, const IpcChannel::ProgressSink &progressSink )
{
    if ( !mChannel || !mProcessAlive )
    {
        IpcChannel::Outcome outcome;
        outcome.status = IpcChannel::Outcome::Status::ChannelClosed;
        outcome.error.code = "E6005";
        outcome.error.message = "worker process is not running";
        outcome.error.retryable = true;
        return outcome;
    }

    // Liveness watchdog: if the process dies mid-request the channel fails
    // the wait with ChannelClosed; a hung process is handled by the ladder.
    IpcChannel::Outcome outcome =
        mChannel->request( method, params, deadlineMs, cancelPredicate, progressSink );

    if ( outcome.status == IpcChannel::Outcome::Status::Timeout )
    {
        // Ladder: cancel frame, grace, forced kill. The outcome stays the
        // typed timeout the caller already received. Control requests are
        // never poisoned-away: they are lifecycle-critical, so a stalled
        // worker dies here regardless of other traffic.
        mChannel->cancelAll();
        const auto graceDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( kKillGraceMs );
        while ( std::chrono::steady_clock::now() < graceDeadline && mProcessAlive )
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        if ( mProcessAlive )
        {
            killProcess( "request deadline ladder" );
        }
        outcome.error.message += "; worker did not answer the cancel frame and was killed";
    }
    else if ( outcome.status == IpcChannel::Outcome::Status::ChannelClosed && mProcessAlive )
    {
        // Channel EOF while we believed the process lived: confirm death.
#ifdef _WIN32
        const DWORD wait = ::WaitForSingleObject( mProcessHandle, 0 );
        if ( wait == WAIT_OBJECT_0 )
        {
            mProcessAlive = false;
            if ( mJobHandle )
            {
                ::CloseHandle( mJobHandle );
                mJobHandle = nullptr;
            }
            ::CloseHandle( mProcessHandle );
            mProcessHandle = nullptr;
        }
#else
        const pid_t pid = static_cast<pid_t>( reinterpret_cast<intptr_t>( mProcessHandle ) );
        int status = 0;
        const pid_t done = ::waitpid( pid, &status, WNOHANG );
        if ( done == pid )
        {
            mProcessAlive = false;
            mProcessHandle = nullptr;
            // mProcessGroupId deliberately survives: killProcess reaps the
            // worker's process group on the self-dead path (grandchildren).
        }
#endif
        if ( !mProcessAlive )
            killProcess( "confirmed dead after channel close" );
    }
    return outcome;
}

bool PluginHostProcessSession::shutdown( int timeoutMs, PluginDiagnosticLog &diagnostics )
{
    if ( !mChannel || !mProcessAlive )
    {
        killProcess( "shutdown on dead session" );
        return true;
    }
    IpcChannel::Outcome outcome = mChannel->request(
        kShutdownPlugin, Json::Value( Json::objectValue ), timeoutMs > 0 ? timeoutMs : 10000 );
    if ( outcome.status == IpcChannel::Outcome::Status::Ok )
    {
        // Give the worker a moment to exit by itself; then reap.
        const auto graceDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( kKillGraceMs );
        bool exited = false;
        while ( std::chrono::steady_clock::now() < graceDeadline )
        {
#ifdef _WIN32
            if ( ::WaitForSingleObject( mProcessHandle, 50 ) == WAIT_OBJECT_0 )
            {
                exited = true;
                break;
            }
#else
            int status = 0;
            if ( ::waitpid( static_cast<pid_t>( reinterpret_cast<intptr_t>( mProcessHandle ) ),
                            &status, WNOHANG )
                 != 0 )
            {
                exited = true;
                break;
            }
#endif
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
        mProcessAlive = false;
#ifndef _WIN32
        if ( exited && mProcessGroupId > 0 )
        {
            // The worker exited cleanly, but plugin-spawned grandchildren
            // survive a plain exit (they would be reparented to init).
            // Windows kills them via job close at this same point; reap the
            // group here for parity.
            ::kill( static_cast<pid_t>( -mProcessGroupId ), SIGKILL );
        }
#endif
        if ( !exited )
        {
            killProcess( "shutdown grace elapsed" );
            diagnostics.add( PluginDiagnosticCode::LibraryLoadFailed,
                             PluginDiagnosticSeverity::Warning,
                             "worker ignored the shutdown reply window and was killed",
                             mOptions.pluginId );
            return false;
        }
#ifdef _WIN32
        if ( mProcessHandle )
        {
            ::CloseHandle( mProcessHandle );
            mProcessHandle = nullptr;
        }
        if ( mJobHandle )
        {
            ::CloseHandle( mJobHandle );
            mJobHandle = nullptr;
        }
#else
        mProcessHandle = nullptr;
        mProcessGroupId = -1;
#endif
        if ( mChannel )
            mChannel->close();
        return true;
    }

    // Shutdown refused/timeout: kill ladder.
    killProcess( "shutdown request failed" );
    diagnostics.add( PluginDiagnosticCode::LibraryLoadFailed,
                     PluginDiagnosticSeverity::Warning,
                     "worker did not answer plugin.shutdown and was killed",
                     mOptions.pluginId );
    return false;
}
