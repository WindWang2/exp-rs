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
        diagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                         PluginDiagnosticSeverity::Error,
                         "CreatePipe failed: " + std::to_string( ::GetLastError() ),
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
    std::wstring commandWide( commandLine.begin(), commandLine.end() );

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
        ::dup2( hostToWorker[ 0 ], 3 );
        ::dup2( workerToHost[ 1 ], 4 );
        ::fcntl( 3, F_SETFD, 0 );
        ::fcntl( 4, F_SETFD, 0 );
        for ( int fd : { hostToWorker[ 0 ], hostToWorker[ 1 ], workerToHost[ 0 ],
                         workerToHost[ 1 ] } )
            if ( fd > 4 )
                ::close( fd );
        const std::string fdRead = std::string( kIpcReadSwitch ) + fdToString( 3 );
        const std::string fdWrite = std::string( kIpcWriteSwitch ) + fdToString( 4 );
        ::execl( mOptions.workerPath.c_str(), mOptions.workerPath.c_str(), fdRead.c_str(),
                 fdWrite.c_str(), static_cast<char *>( nullptr ) );
        ::_exit( 127 );
    }
    ::close( hostToWorker[ 0 ] );
    ::close( workerToHost[ 1 ] );
    mProcessHandle = reinterpret_cast<void *>( static_cast<intptr_t>( pid ) );
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
    return true;
}

bool PluginHostProcessSession::isAlive() const
{
    return mProcessAlive;
}

void PluginHostProcessSession::killProcess( const char *reason )
{
    (void)reason;
    if ( !mProcessAlive.exchange( false ) )
        return;
#ifdef _WIN32
    if ( mJobHandle )
    {
        ::TerminateJobObject( mJobHandle, 9 );
    }
    else if ( mProcessHandle )
    {
        ::TerminateProcess( mProcessHandle, 9 );
    }
    if ( mProcessHandle )
    {
        ::WaitForSingleObject( mProcessHandle, 5000 );
        ::CloseHandle( mProcessHandle );
        mProcessHandle = nullptr;
    }
    if ( mJobHandle )
    {
        ::CloseHandle( mJobHandle );
        mJobHandle = nullptr;
    }
#else
    const pid_t pid = static_cast<pid_t>( reinterpret_cast<intptr_t>( mProcessHandle ) );
    if ( pid > 0 )
    {
        ::kill( pid, SIGKILL );
        int status = 0;
        ::waitpid( pid, &status, 0 );
    }
    mProcessHandle = nullptr;
#endif
    if ( mChannel )
        mChannel->close();
}

void PluginHostProcessSession::enforceDeadline( long long requestId )
{
    if ( mChannel )
        mChannel->cancel( requestId );
}

IpcChannel::Outcome PluginHostProcessSession::request(
    const std::string &method, const Json::Value &params, int deadlineMs,
    const IpcChannel::CancelPredicate &cancelPredicate, const IpcChannel::ProgressSink &progressSink )
{
    // Quota authority: the ceiling applies regardless of the caller's ask.
    const int effectiveDeadline = deadlineMs > 0 ? std::min( deadlineMs, mOptions.quota.requestDeadlineMs )
                                                 : mOptions.quota.requestDeadlineMs;
    return requestRaw( method, params, effectiveDeadline, cancelPredicate, progressSink );
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
        // typed timeout the caller already received.
        mChannel->cancelAll(); // broadcast cancel (v1 serial dispatch)
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

bool PluginHostProcessSession::respawnSession( PluginDiagnosticLog &diagnostics,
                                               const Json::Value &loadParams,
                                               const std::string &entrypointPath )
{
    (void)entrypointPath;
    // Restart policy: bounded respawns inside a rolling window.
    const auto now = std::chrono::steady_clock::now();
    if ( !mRestartWindowArmed )
    {
        mRestartWindowArmed = true;
        mFirstRestart = now;
        mRestartCount = 0;
    }
    if ( std::chrono::duration_cast<std::chrono::milliseconds>( now - mFirstRestart ).count()
             > mOptions.restartWindowMs )
    {
        mRestartWindowArmed = false; // window elapsed: counter resets
    }
    else if ( mRestartCount >= mOptions.maxRestarts )
    {
        diagnostics.add( PluginDiagnosticCode::HostProcessCrashed,
                         PluginDiagnosticSeverity::Error,
                         "restart policy exhausted (" + std::to_string( mOptions.maxRestarts )
                             + " respawns in " + std::to_string( mOptions.restartWindowMs )
                             + " ms); unload/reload resets it",
                         mOptions.pluginId );
        return false;
    }

    if ( mChannel )
    {
        mChannel->close();
        mChannel.reset();
    }
    killProcess( "respawn" );

    if ( !spawnWorkerProcess( diagnostics ) )
        return false;
    ++mRestartCount;
    ++mGeneration;

    // Handshake validation happens through the first plugin.load reply; the
    // hello event of the fresh worker is consumed by the channel.
    IpcChannel::Outcome outcome =
        mChannel->request( kLoadPlugin, loadParams, mOptions.handshakeTimeoutMs * 2 );
    if ( outcome.status != IpcChannel::Outcome::Status::Ok )
    {
        diagnostics.add( PluginDiagnosticCode::HostProcessCrashed,
                         PluginDiagnosticSeverity::Error,
                         "worker respawn could not reload the plugin: " + outcome.error.message,
                         mOptions.pluginId );
        killProcess( "respawn load failed" );
        return false;
    }
    return true;
}
