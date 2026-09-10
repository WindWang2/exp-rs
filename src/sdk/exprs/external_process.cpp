/***************************************************************************
 * exprs/external_process.cpp — safe external process execution
 *
 * Isolation runtime 5.0: the Windows branch is a first-class implementation
 * (CreateProcessW + job objects), replacing the historical typed refusal.
 * Both branches share the security contract of the header comment above:
 * argv-only spawn (never a shell), bounded output capture, timeout and
 * cancellation that tear down the WHOLE tree, env allow-list by default and
 * the workspace effect policy (E5005) checked BEFORE any spawn.
 *
 * Platform honesty (docs/plugins/external-process.md): POSIX gets a true
 * SIGTERM->SIGKILL ladder (2 s grace); Windows has no SIGTERM for arbitrary
 * console processes, so timeout/cancel escalate straight to
 * TerminateJobObject (forced kill). The result flags match; the graceful
 * window does not exist there. Forced termination is reported as
 * exitSignal 9 (SIGKILL marker) + exitCode -1 on both platforms.
 ***************************************************************************/
#include "exprs/external_process.h"

#include "exprs/path_policy.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
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
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>

#if defined( __APPLE__ ) || defined( __FreeBSD__ ) || defined( __OpenBSD__ ) || defined( __NetBSD__ )
// POSIX environ — not declared by <unistd.h> on macOS/BSD.
extern char **environ;
#endif
#endif

namespace exprs {

// ===========================================================================
// Shared helpers (both platforms)
// ===========================================================================
namespace {
constexpr int kTerminateGraceMs = 2000;
constexpr int kPostExitDrainGraceMs = 2000;
class BoundedSink
{
public:
    BoundedSink( std::string &target, long limit, bool &truncatedFlag )
        : mTarget( target )
        , mLimit( limit > 0 ? limit : 1 )
        , mTruncated( truncatedFlag )
    {
    }

    void consume( const char *data, long long length )
    {
        mTotal += length;
        if ( mTarget.size() < static_cast<size_t>( mLimit ) )
        {
            const long remaining = mLimit - static_cast<long>( mTarget.size() );
            const long take = std::min<long>( remaining, static_cast<long>( length ) );
            mTarget.append( data, static_cast<size_t>( take ) );
        }
        if ( mTotal > mLimit )
            mTruncated = true;
    }

    long total() const { return mTotal; }

private:
    std::string &mTarget;
    long mLimit;
    bool &mTruncated;
    long mTotal = 0;
};

/// Issue #757: workspace effect policy. Validates the RESOLVED execution
/// effects (working directory, argv paths, env-value paths) against the
/// active workspace root — independent of who supplied the value (request
/// parameter or manifest constant). Empty string = policy inactive/allowed.
std::string workspaceEffectEscape( const ExternalProcessRequest &request )
{
    const std::string root = PathPolicy::workspaceRoot();
    if ( root.empty() )
        return {};
    const auto allowed = [&]( const std::string &path ) {
        if ( PathPolicy::resolvesInsideRoot( root, path ) )
            return true;
        for ( const std::string &extra : request.additionalAllowedRoots )
        {
            if ( !extra.empty() && PathPolicy::resolvesInsideRoot( extra, path ) )
                return true;
        }
        return false;
    };
    if ( !request.workingDirectory.empty() && !allowed( request.workingDirectory ) )
        return "working directory escapes the workspace policy: " + request.workingDirectory;
    // The child resolves relative paths against ITS working directory
    // (chdir before exec) — policy checks must use the same base, not the
    // host process cwd.
    const std::string base =
        !request.workingDirectory.empty()
            ? ( PathPolicy::isAbsolute( request.workingDirectory )
                    ? request.workingDirectory
                    : std::filesystem::current_path().generic_string() + "/"
                          + request.workingDirectory )
            : std::filesystem::current_path().generic_string();
    for ( size_t index = 1; index < request.argv.size(); ++index )
    {
        const std::string &argument = request.argv[index];
        if ( argument.empty() )
            continue;
        // Absolute arguments and relative arguments containing ".." name
        // filesystem locations; both are policy-checked. Plain relative
        // arguments resolve inside the (contained) working directory.
        bool pathish = PathPolicy::isAbsolute( argument );
        if ( !pathish && argument.find( ".." ) != std::string::npos )
        {
            pathish = true;
            if ( !allowed( base + "/" + argument ) )
                return "argument path escapes the workspace policy: " + argument;
            continue;
        }
        if ( pathish && !allowed( argument ) )
            return "argument path escapes the workspace policy: " + argument;
    }
    if ( request.environment.isObject() )
    {
        for ( const std::string &key : request.environment.getMemberNames() )
        {
            const Json::Value &value = request.environment[key];
            if ( value.isString() && PathPolicy::isAbsolute( value.asString() )
                 && !allowed( value.asString() ) )
                return "environment value escapes the workspace policy: " + key + "="
                       + value.asString();
        }
    }
    return {};
}


} // namespace (shared helpers)

// ===========================================================================
// Windows implementation (isolation runtime 5.0)
// ===========================================================================
#ifdef _WIN32

namespace {

std::wstring utf8ToWide( const std::string &text )
{
    if ( text.empty() )
        return std::wstring();
    const int size = MultiByteToWideChar( CP_UTF8, 0, text.c_str(),
                                          static_cast<int>( text.size() ), nullptr, 0 );
    std::wstring wide( static_cast<size_t>( size > 0 ? size : 0 ), L'\0' );
    if ( size > 0 )
        MultiByteToWideChar( CP_UTF8, 0, text.c_str(), static_cast<int>( text.size() ),
                             wide.data(), size );
    return wide;
}

std::string wideToUtf8( const wchar_t *text )
{
    if ( !text || !*text )
        return std::string();
    const int size = WideCharToMultiByte( CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr );
    std::string utf8( static_cast<size_t>( size > 0 ? size - 1 : 0 ), '\0' );
    if ( size > 1 )
        WideCharToMultiByte( CP_UTF8, 0, text, -1, utf8.data(), size, nullptr, nullptr );
    return utf8;
}

/// Quotes one argv element with MSVCRT command-line rules: backslash runs
/// before a quote are doubled-plus-one, interior quotes are escaped, and
/// elements containing space/tab/quote are wrapped in quotes. The child's
/// CRT reverses exactly this, so shell metacharacters stay data.
std::string quoteWindowsArg( const std::string &argument )
{
    if ( !argument.empty()
         && argument.find_first_of( " \t\"" ) == std::string::npos )
        return argument;

    std::string quoted;
    quoted.reserve( argument.size() + 3 );
    quoted.push_back( '"' );
    size_t backslashes = 0;
    for ( const char character : argument )
    {
        if ( character == '\\' )
        {
            ++backslashes;
            continue;
        }
        if ( character == '"' )
        {
            quoted.append( backslashes * 2 + 1, '\\' );
            quoted.push_back( '"' );
        }
        else
        {
            quoted.append( backslashes, '\\' );
            quoted.push_back( character );
        }
        backslashes = 0;
    }
    quoted.append( backslashes * 2, '\\' );
    quoted.push_back( '"' );
    return quoted;
}

/// Current environment as a map (wide, case-preserved).
std::map<std::string, std::string> currentEnvironment()
{
    std::map<std::string, std::string> result;
    wchar_t *block = ::GetEnvironmentStringsW();
    if ( !block )
        return result;
    const wchar_t *cursor = block;
    while ( *cursor )
    {
        const std::string entry = wideToUtf8( cursor );
        const size_t equals = entry.find( '=' );
        if ( equals != std::string::npos && equals > 0 )
            result[ entry.substr( 0, equals ) ] = entry.substr( equals + 1 );
        cursor += wcslen( cursor ) + 1;
    }
    ::FreeEnvironmentStringsW( block );
    return result;
}

/// Builds the child environment block (sorted, double-NUL terminated wide
/// string). Minimal baseline PATH/USERPROFILE/TEMP/LANG + explicit entries,
/// or full inheritance when request.inheritEnvironment is set — the same
/// allow-list contract as the POSIX branch.
std::vector<wchar_t> environmentBlock( const ExternalProcessRequest &request )
{
    std::map<std::string, std::string> merged;
    if ( request.inheritEnvironment )
        merged = currentEnvironment();

    if ( request.environment.isObject() )
    {
        for ( const std::string &name : request.environment.getMemberNames() )
        {
            const Json::Value &value = request.environment[ name ];
            if ( value.isString() )
                merged[ name ] = value.asString();
        }
    }
    if ( !request.inheritEnvironment )
    {
        for ( const auto &entry : currentEnvironment() )
        {
            if ( entry.first == "PATH" || entry.first == "USERPROFILE"
                 || entry.first == "TEMP" || entry.first == "LANG" )
                merged.emplace( entry.first, entry.second ); // explicit entries win
        }
    }

    // Case-insensitive sort by name — CreateProcessW expects a sorted block.
    std::vector<std::pair<std::string, std::string>> entries( merged.begin(), merged.end() );
    std::sort( entries.begin(), entries.end(),
               []( const std::pair<std::string, std::string> &a,
                   const std::pair<std::string, std::string> &b ) {
                   std::string lowerA;
                   lowerA.reserve( a.first.size() );
                   for ( const char c : a.first )
                       lowerA.push_back(
                           static_cast<char>( ::tolower( static_cast<unsigned char>( c ) ) ) );
                   std::string lowerB;
                   lowerB.reserve( b.first.size() );
                   for ( const char c : b.first )
                       lowerB.push_back(
                           static_cast<char>( ::tolower( static_cast<unsigned char>( c ) ) ) );
                   return lowerA < lowerB;
               } );

    std::vector<wchar_t> block;
    for ( const auto &entry : entries )
    {
        const std::wstring wide = utf8ToWide( entry.first ) + L"=" + utf8ToWide( entry.second );
        block.insert( block.end(), wide.begin(), wide.end() );
        block.push_back( L'\0' );
    }
    if ( block.empty() )
        return block;
    block.push_back( L'\0' );
    return block;
}

/// Drains one pipe into a bounded sink. Returns true while the pipe is open.
bool drainPipe( HANDLE pipe, BoundedSink &sink )
{
    DWORD available = 0;
    while ( ::PeekNamedPipe( pipe, nullptr, 0, nullptr, &available, nullptr ) )
    {
        if ( available == 0 )
            return true;
        char buffer[ 16384 ];
        DWORD toRead = available;
        if ( toRead > sizeof( buffer ) )
            toRead = sizeof( buffer );
        DWORD got = 0;
        if ( !::ReadFile( pipe, buffer, toRead, &got, nullptr ) )
            return false;
        if ( got == 0 )
            return true;
        sink.consume( buffer, static_cast<long long>( got ) );
    }
    return false; // pipe closed (child exited)
}

} // namespace

bool ExternalProcess::validateArgv( const std::vector<std::string> &argv, std::string &error )
{
    if ( argv.empty() || argv.front().empty() )
    {
        error = "argv must start with a program name";
        return false;
    }
    const std::string &program = argv.front();
    if ( program.find( '/' ) == std::string::npos
         && program.find( '\\' ) == std::string::npos
         && program.find( ':' ) == std::string::npos )
    {
        // Bare name: verify presence on PATH for diagnostics (the security
        // boundary is argv-only spawn, never this check).
        wchar_t pathBuffer[ MAX_PATH ];
        std::wstring bare = utf8ToWide( program );
        wchar_t *filePart = nullptr;
        const DWORD searched = ::SearchPathW( nullptr, bare.c_str(), L".exe",
                                              MAX_PATH, pathBuffer, &filePart );
        if ( searched == 0 || searched >= MAX_PATH )
        {
            error = "program '" + program + "' not found in PATH";
            return false;
        }
    }
    else
    {
        const DWORD attributes = ::GetFileAttributesW( utf8ToWide( program ).c_str() );
        if ( attributes == INVALID_FILE_ATTRIBUTES || ( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
        {
            error = "program '" + program + "' is not an executable file";
            return false;
        }
    }
    return true;
}

ExternalProcessResult ExternalProcess::run( const ExternalProcessRequest &request )
{
    ExternalProcessResult result;
    const auto startTime = std::chrono::steady_clock::now();

    std::string programError;
    if ( !validateArgv( request.argv, programError ) )
    {
        result.error = programError;
        return result;
    }

    // Workspace effect policy (#757): identical gate to POSIX, refuses
    // BEFORE anything is spawned.
    {
        const std::string escape = workspaceEffectEscape( request );
        if ( !escape.empty() )
        {
            result.refusedByPolicy = true;
            result.error = "workspace_escape (E5005): " + escape;
            return result;
        }
    }

    // Command line (argv-only: no shell anywhere in this path).
    std::string commandLine;
    for ( size_t index = 0; index < request.argv.size(); ++index )
    {
        if ( index )
            commandLine.push_back( ' ' );
        commandLine += quoteWindowsArg( request.argv[ index ] );
    }
    std::wstring commandWide = utf8ToWide( commandLine );
    if ( commandWide.size() >= 32767 )
    {
        result.error = "command line exceeds the 32767 character CreateProcessW limit";
        return result;
    }

    // Output pipes. Only the write ends are inheritable (the STARTUPINFO
    // handle list below restricts inheritance to exactly these two).
    SECURITY_ATTRIBUTES inherit;
    inherit.nLength = sizeof( inherit );
    inherit.bInheritHandle = TRUE;
    inherit.lpSecurityDescriptor = nullptr;
    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if ( !::CreatePipe( &stdoutRead, &stdoutWrite, &inherit, 0 )
         || !::CreatePipe( &stderrRead, &stderrWrite, &inherit, 0 ) )
    {
        const DWORD pipeError = ::GetLastError();
        for ( HANDLE handle : { stdoutRead, stdoutWrite, stderrRead, stderrWrite } )
            if ( handle )
                ::CloseHandle( handle );
        result.error = "CreatePipe failed: " + std::to_string( pipeError );
        return result;
    }
    // The read ends must NOT leak into the child or a grandchild holding
    // them open would keep our drain loop from ever seeing EOF.
    ::SetHandleInformation( stdoutRead, HANDLE_FLAG_INHERIT, 0 );
    ::SetHandleInformation( stderrRead, HANDLE_FLAG_INHERIT, 0 );

    // Job object: the whole tree dies together (kill-on-close) — the Windows
    // counterpart of the POSIX setsid()+process-group kill.
    HANDLE job = ::CreateJobObjectW( nullptr, nullptr );
    if ( job )
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
        ZeroMemory( &limits, sizeof( limits ) );
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject( job, JobObjectExtendedLimitInformation, &limits,
                                   sizeof( limits ) );
    }

    STARTUPINFOEXW startupInfo;
    ZeroMemory( &startupInfo, sizeof( startupInfo ) );
    startupInfo.StartupInfo.cb = sizeof( startupInfo );
    startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = nullptr;
    startupInfo.StartupInfo.hStdOutput = stdoutWrite;
    startupInfo.StartupInfo.hStdError = stderrWrite;

    // Restrict inherited handles to exactly the two pipe write ends.
    HANDLE inheritList[ 2 ] = { stdoutWrite, stderrWrite };
    SIZE_T attributeSize = 0;
    ::InitializeProcThreadAttributeList( nullptr, 1, 0, &attributeSize );
    std::vector<char> attributeBuffer( attributeSize );
    startupInfo.lpAttributeList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( attributeBuffer.data() );
    const bool attributesOk =
        ::InitializeProcThreadAttributeList( startupInfo.lpAttributeList, 1, 0, &attributeSize )
        && ::UpdateProcThreadAttribute(
            startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritList,
            sizeof( inheritList ), nullptr, nullptr );

    std::vector<wchar_t> environment = environmentBlock( request );
    const std::wstring workingDirectory =
        request.workingDirectory.empty() ? std::wstring() : utf8ToWide( request.workingDirectory );

    PROCESS_INFORMATION processInfo;
    ZeroMemory( &processInfo, sizeof( processInfo ) );
    // CREATE_SUSPENDED: assign the job before the first instruction runs, so
    // the tree can never escape kill-on-close.
    const DWORD creationFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW
                                | CREATE_UNICODE_ENVIRONMENT
                                | EXTENDED_STARTUPINFO_PRESENT;
    const BOOL created =
        attributesOk
            ? ::CreateProcessW( nullptr, commandWide.data(), nullptr, nullptr, TRUE,
                                creationFlags,
                                environment.empty() ? nullptr : environment.data(),
                                workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                &startupInfo.StartupInfo, &processInfo )
            : FALSE;
    if ( attributesOk )
        ::DeleteProcThreadAttributeList( startupInfo.lpAttributeList );
    ::CloseHandle( stdoutWrite );
    ::CloseHandle( stderrWrite );
    if ( !created )
    {
        const DWORD spawnError = ::GetLastError();
        result.error = attributesOk
                           ? "CreateProcessW failed with error " + std::to_string( spawnError )
                           : "process attribute list initialization failed";
        ::CloseHandle( stdoutRead );
        ::CloseHandle( stderrRead );
        if ( job )
            ::CloseHandle( job );
        return result;
    }

    // The job holds the process reference for kill-on-close; assigning a
    // suspended process leaves no window where the tree escapes.
    if ( job )
        ::AssignProcessToJobObject( job, processInfo.hProcess );
    ::ResumeThread( processInfo.hThread );
    ::CloseHandle( processInfo.hThread );

    result.started = true;

    BoundedSink stdoutSink( result.stdOut, request.stdoutLimitBytes, result.truncatedStdout );
    BoundedSink stderrSink( result.stdErr, request.stderrLimitBytes, result.truncatedStderr );

    const int timeoutSeconds = request.timeoutSeconds > 0 ? request.timeoutSeconds : 3600;
    const auto deadline = startTime + std::chrono::seconds( timeoutSeconds );

    bool cancelled = false;
    bool timedOut = false;
    bool killed = false;
    bool exited = false;
    bool stdoutOpen = true;
    bool stderrOpen = true;
    bool sawStdoutEof = false;
    bool sawStderrEof = false;
    // A grandchild holding inherited write ends can keep the pipes open
    // forever after the child exited; bound the post-exit drain so run()
    // always returns (documented: tail output beyond the grace is lost).
    std::chrono::steady_clock::time_point postExitDrainDeadline{};

    while ( !exited || stdoutOpen || stderrOpen )
    {
        if ( stdoutOpen && !drainPipe( stdoutRead, stdoutSink ) )
        {
            sawStdoutEof = true;
            stdoutOpen = false;
        }
        if ( stderrOpen && !drainPipe( stderrRead, stderrSink ) )
        {
            sawStderrEof = true;
            stderrOpen = false;
        }

        if ( request.onOutput )
            request.onOutput( stdoutSink.total(), stderrSink.total() );

        if ( !exited )
        {
            if ( !cancelled && request.isCancelled && request.isCancelled() )
                cancelled = true;
            if ( !timedOut && std::chrono::steady_clock::now() >= deadline )
                timedOut = true;

            const DWORD wait = ::WaitForSingleObject( processInfo.hProcess, 50 );
            if ( wait == WAIT_OBJECT_0 )
            {
                exited = true;
                postExitDrainDeadline = std::chrono::steady_clock::now()
                                        + std::chrono::milliseconds( kPostExitDrainGraceMs );
            }
            else if ( ( cancelled || timedOut ) && !killed )
            {
                // No SIGTERM equivalent for arbitrary console processes:
                // forced kill of the whole tree (documented difference).
                killed = true;
                if ( job )
                    ::TerminateJobObject( job, 9 );
                else
                    ::TerminateProcess( processInfo.hProcess, 9 );
            }
        }

        // Once the process exited, drain until both pipes hit EOF so no
        // buffered output is lost — bounded by the post-exit grace (a
        // grandchild inheriting the write ends must not hang run()).
        if ( exited && ( ( sawStdoutEof && sawStderrEof ) || !stdoutOpen && !stderrOpen ) )
            break;
        if ( exited && std::chrono::steady_clock::now() >= postExitDrainDeadline )
            break;
    }

    ::CloseHandle( stdoutRead );
    ::CloseHandle( stderrRead );

    DWORD exitCodeValue = DWORD( -1 );
    if ( !::GetExitCodeProcess( processInfo.hProcess, &exitCodeValue ) )
    {
        result.error = result.error.empty() ? "GetExitCodeProcess failed" : result.error;
    }
    ::CloseHandle( processInfo.hProcess );
    if ( job )
        ::CloseHandle( job ); // kill-on-close sweeps anything still alive

    if ( timedOut || cancelled )
    {
        result.timedOut = timedOut;
        result.cancelled = cancelled;
        result.exitCode = -1;
        result.exitSignal = 9; // SIGKILL marker for forced termination
        result.error = timedOut
                           ? "external process exceeded the " + std::to_string( timeoutSeconds )
                                 + "s timeout"
                           : "external process was cancelled";
    }
    else
    {
        result.exitCode = static_cast<int>( exitCodeValue );
        result.exitSignal = 0;
    }

    result.durationMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()
                                                               - startTime )
            .count() );
    return result;
}

#else // POSIX ==============================================================

namespace {

#if defined( __APPLE__ ) || defined( __FreeBSD__ ) || defined( __OpenBSD__ ) || defined( __NetBSD__ )
/// Darwin/BSD lack execvpe(3). Resolve PATH from envp then execve.
int execvpeCompat( const char *file, char *const argv[], char *const envp[] )
{
    if ( !file || !file[0] )
    {
        errno = ENOENT;
        return -1;
    }
    if ( std::strchr( file, '/' ) )
        return ::execve( file, argv, envp );

    const char *pathVal = nullptr;
    for ( char *const *entry = envp; entry && *entry; ++entry )
    {
        if ( std::strncmp( *entry, "PATH=", 5 ) == 0 )
        {
            pathVal = *entry + 5;
            break;
        }
    }
    if ( !pathVal || !pathVal[0] )
        pathVal = "/usr/bin:/bin";

    std::string pathCopy( pathVal );
    char *save = nullptr;
    for ( char *dir = strtok_r( pathCopy.data(), ":", &save ); dir;
          dir = strtok_r( nullptr, ":", &save ) )
    {
        std::string candidate = std::string( dir ) + "/" + file;
        ::execve( candidate.c_str(), argv, envp );
        if ( errno != ENOENT && errno != ENOTDIR )
            return -1;
    }
    errno = ENOENT;
    return -1;
}
#else
int execvpeCompat( const char *file, char *const argv[], char *const envp[] )
{
    return ::execvpe( file, argv, envp );
}
#endif


/// Builds the final child environment: minimal baseline (PATH, HOME, TMPDIR,
/// LANG) + explicit entries, or the full parent environment when
/// inheritEnvironment is set (env blocks commonly carry credentials, so full
/// inheritance is opt-in).
std::vector<std::string> childEnvironment( const ExternalProcessRequest &request )
{
    if ( request.inheritEnvironment )
    {
        // Explicit entries REPLACE same-named inherited variables (glibc
        // honours the first occurrence, so appending would be a silent
        // no-op and contradict the documented override contract).
        std::map<std::string, std::string> merged;
        for ( char **current = environ; current && *current; ++current )
        {
            const std::string entry( *current );
            const size_t equals = entry.find( '=' );
            if ( equals != std::string::npos )
                merged[entry.substr( 0, equals )] = entry.substr( equals + 1 );
        }
        if ( request.environment.isObject() )
        {
            for ( const std::string &name : request.environment.getMemberNames() )
            {
                const Json::Value &value = request.environment[name];
                if ( value.isString() )
                    merged[name] = value.asString();
            }
        }
        std::vector<std::string> envp;
        for ( const auto &entry : merged )
            envp.push_back( entry.first + "=" + entry.second );
        return envp;
    }

    std::vector<std::pair<std::string, std::string>> entries;
    for ( const char *name : { "PATH", "HOME", "TMPDIR", "LANG" } )
    {
        const char *value = std::getenv( name );
        if ( value )
            entries.emplace_back( name, value );
    }
    if ( request.environment.isObject() )
    {
        for ( const std::string &name : request.environment.getMemberNames() )
        {
            const Json::Value &value = request.environment[name];
            if ( !value.isString() )
                continue;
            bool replaced = false;
            for ( auto &entry : entries )
            {
                if ( entry.first == name )
                {
                    entry.second = value.asString();
                    replaced = true;
                }
            }
            if ( !replaced )
                entries.emplace_back( name, value.asString() );
        }
    }
    std::vector<std::string> envp;
    for ( const auto &entry : entries )
        envp.push_back( entry.first + "=" + entry.second );
    return envp;
}

// BoundedSink moved to the shared section above

void killProcessGroup( pid_t pid, int signalNumber )
{
    // The child called setsid(), so its process group id equals its pid.
    ::kill( -pid, signalNumber );
    ::kill( pid, signalNumber );
}

/// Reads everything currently available from @p fd. Returns false when the
/// descriptor has hit EOF or failed and should be closed.
bool drainFd( int fd, BoundedSink &sink )
{
    char buffer[16384];
    while ( true )
    {
        const ssize_t chunk = ::read( fd, buffer, sizeof( buffer ) );
        if ( chunk > 0 )
        {
            sink.consume( buffer, chunk );
            continue;
        }
        if ( chunk == 0 )
            return false; // EOF
        if ( errno == EAGAIN || errno == EWOULDBLOCK )
            return true;
        if ( errno == EINTR )
            continue;
        return false;
    }
}

// workspaceEffectEscape moved to the shared section above

} // namespace

bool ExternalProcess::validateArgv( const std::vector<std::string> &argv, std::string &error )
{
    if ( argv.empty() || argv.front().empty() )
    {
        error = "argv must start with a program name";
        return false;
    }
    const std::string &program = argv.front();
    if ( program.find( '/' ) == std::string::npos )
    {
        // Resolved via PATH at exec time; verify presence now for diagnostics.
        const char *path = std::getenv( "PATH" );
        const std::string searchPath = path ? path : "/usr/bin:/bin";
        size_t start = 0;
        bool found = false;
        while ( start <= searchPath.size() )
        {
            const size_t colon = searchPath.find( ':', start );
            const size_t end = colon == std::string::npos ? searchPath.size() : colon;
            const std::string candidate =
                searchPath.substr( start, end - start ) + "/" + program;
            if ( ::access( candidate.c_str(), X_OK ) == 0 )
            {
                found = true;
                break;
            }
            if ( colon == std::string::npos )
                break;
            start = colon + 1;
        }
        if ( !found )
        {
            error = "program '" + program + "' not found in PATH";
            return false;
        }
    }
    else if ( ::access( program.c_str(), X_OK ) != 0 )
    {
        error = "program '" + program + "' is not executable";
        return false;
    }
    return true;
}

ExternalProcessResult ExternalProcess::run( const ExternalProcessRequest &request )
{
    ExternalProcessResult result;
    const auto startTime = std::chrono::steady_clock::now();

    std::string programError;
    if ( !validateArgv( request.argv, programError ) )
    {
        result.error = programError;
        return result;
    }

    // Workspace effect policy (#757): refuses BEFORE spawning anything when
    // a resolved effect escapes SICNU_MCP_WORKSPACE (or the declared extra
    // roots). Manifest constants cannot bypass it — the resolved values are
    // what get checked.
    {
        const std::string escape = workspaceEffectEscape( request );
        if ( !escape.empty() )
        {
            result.refusedByPolicy = true;
            result.error = "workspace_escape (E5005): " + escape;
            return result;
        }
    }

    int stdoutPipe[2] = { -1, -1 };
    int stderrPipe[2] = { -1, -1 };
    int errorPipe[2] = { -1, -1 };
    if ( ::pipe( stdoutPipe ) != 0 || ::pipe( stderrPipe ) != 0 || ::pipe( errorPipe ) != 0 )
    {
        result.error = "pipe() failed: " + std::string( std::strerror( errno ) );
        return result;
    }

    const std::vector<std::string> environment = childEnvironment( request );
    std::vector<char *> argvPointers;
    argvPointers.reserve( request.argv.size() + 1 );
    for ( const std::string &arg : request.argv )
        argvPointers.push_back( const_cast<char *>( arg.c_str() ) );
    argvPointers.push_back( nullptr );

    std::vector<char *> envPointers;
    envPointers.reserve( environment.size() + 1 );
    for ( const std::string &entry : environment )
        envPointers.push_back( const_cast<char *>( entry.c_str() ) );
    envPointers.push_back( nullptr );

    const pid_t pid = ::fork();
    if ( pid < 0 )
    {
        result.error = "fork() failed: " + std::string( std::strerror( errno ) );
        for ( int fd : { stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1],
                         errorPipe[0], errorPipe[1] } )
            ::close( fd );
        return result;
    }

    if ( pid == 0 )
    {
        // Child: own session/process group so the parent can kill the whole
        // tree on timeout/cancel.
        ::setsid();
        ::dup2( stdoutPipe[1], STDOUT_FILENO );
        ::dup2( stderrPipe[1], STDERR_FILENO );
        for ( int fd : { stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1],
                         errorPipe[0] } )
            ::close( fd );
        ::fcntl( errorPipe[1], F_SETFD, FD_CLOEXEC );
        if ( !request.workingDirectory.empty()
             && ::chdir( request.workingDirectory.c_str() ) != 0 )
        {
            const int chdirError = errno;
            ssize_t ignored = ::write( errorPipe[1], &chdirError, sizeof( chdirError ) );
            (void)ignored;
            ::_exit( 126 );
        }
        execvpeCompat( argvPointers[0], argvPointers.data(), envPointers.data() );
        const int execError = errno;
        ssize_t ignored = ::write( errorPipe[1], &execError, sizeof( execError ) );
        (void)ignored;
        ::_exit( 127 );
    }

    // Parent --------------------------------------------------------------
    ::close( stdoutPipe[1] );
    ::close( stderrPipe[1] );
    ::close( errorPipe[1] );
    ::fcntl( stdoutPipe[0], F_SETFL, O_NONBLOCK );
    ::fcntl( stderrPipe[0], F_SETFL, O_NONBLOCK );
    ::fcntl( errorPipe[0], F_SETFL, O_NONBLOCK );

    result.started = true;

    BoundedSink stdoutSink( result.stdOut, request.stdoutLimitBytes, result.truncatedStdout );
    BoundedSink stderrSink( result.stdErr, request.stderrLimitBytes, result.truncatedStderr );

    int readFds[3] = { stdoutPipe[0], stderrPipe[0], errorPipe[0] };
    int execFailure = 0;
    bool execFailed = false;
    bool cancelled = false;
    bool timedOut = false;
    bool reaped = false;
    const int timeoutSeconds = request.timeoutSeconds > 0 ? request.timeoutSeconds : 3600;
    const auto deadline = startTime + std::chrono::seconds( timeoutSeconds );

    auto reapStatus = [&]( int status ) {
        result.exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
        result.exitSignal = WIFSIGNALED( status ) ? WTERMSIG( status ) : 0;
        reaped = true;
    };

    while ( true )
    {
        // Drain child output; closed descriptors stop being polled.
        if ( readFds[0] >= 0 && !drainFd( readFds[0], stdoutSink ) )
        {
            ::close( readFds[0] );
            readFds[0] = -1;
        }
        if ( readFds[1] >= 0 && !drainFd( readFds[1], stderrSink ) )
        {
            ::close( readFds[1] );
            readFds[1] = -1;
        }
        if ( readFds[2] >= 0 )
        {
            char errorBuffer[64];
            while ( true )
            {
                const ssize_t chunk = ::read( readFds[2], errorBuffer, sizeof( errorBuffer ) );
                if ( chunk == static_cast<ssize_t>( sizeof( execFailure ) ) )
                {
                    std::memcpy( &execFailure, errorBuffer, sizeof( execFailure ) );
                    execFailed = true;
                    continue;
                }
                if ( chunk > 0 )
                    continue;
                if ( chunk < 0 && errno == EINTR )
                    continue;
                break;
            }
            // exec error arrives as a short write; treat EOF as completion.
            bool eof = false;
            {
                // peek for EOF by polling with 0 timeout
                struct pollfd probe { readFds[2], POLLIN, 0 };
                const int ready = ::poll( &probe, 1, 0 );
                if ( ready > 0 && ( probe.revents & ( POLLHUP | POLLERR | POLLNVAL ) ) )
                    eof = true;
                if ( ready == 0 )
                {
                    // no data; keep open
                }
                else if ( ready > 0 && ( probe.revents & POLLIN ) )
                {
                    // still data pending; handled next loop
                }
            }
            if ( eof )
            {
                ::close( readFds[2] );
                readFds[2] = -1;
            }
        }

        if ( request.onOutput )
            request.onOutput( stdoutSink.total(), stderrSink.total() );

        if ( readFds[0] < 0 && readFds[1] < 0 && readFds[2] < 0 )
            break;

        if ( !cancelled && request.isCancelled && request.isCancelled() )
            cancelled = true;
        if ( !timedOut && std::chrono::steady_clock::now() >= deadline )
            timedOut = true;

        if ( cancelled || timedOut )
        {
            killProcessGroup( pid, SIGTERM );
            const auto graceDeadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds( kTerminateGraceMs );
            while ( std::chrono::steady_clock::now() < graceDeadline )
            {
                int status = 0;
                const pid_t done = ::waitpid( pid, &status, WNOHANG );
                if ( done == pid )
                {
                    reapStatus( status );
                    break;
                }
                usleep( 20000 );
            }
            if ( !reaped )
            {
                killProcessGroup( pid, SIGKILL );
                int status = 0;
                while ( ::waitpid( pid, &status, 0 ) < 0 && errno == EINTR )
                {
                }
                reapStatus( status );
            }
            result.timedOut = timedOut;
            result.cancelled = cancelled;
            result.error = timedOut
                               ? "external process exceeded the " + std::to_string( timeoutSeconds )
                                     + "s timeout"
                               : "external process was cancelled";
            break;
        }

        struct pollfd fds[3];
        int count = 0;
        for ( int candidate : readFds )
        {
            if ( candidate < 0 )
                continue;
            fds[count].fd = candidate;
            fds[count].events = POLLIN;
            fds[count].revents = 0;
            ++count;
        }
        if ( count == 0 )
            break;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now() );
        const int pollTimeout = static_cast<int>(
            std::min<long long>( 200, std::max<long long>( 1, remaining.count() ) ) );
        const int ready = ::poll( fds, static_cast<nfds_t>( count ), pollTimeout );
        if ( ready < 0 && errno != EINTR )
        {
            result.error = "poll() failed: " + std::string( std::strerror( errno ) );
            break;
        }
    }

    for ( int fd : readFds )
    {
        if ( fd >= 0 )
            ::close( fd );
    }

    if ( !result.timedOut && !result.cancelled )
    {
        if ( execFailed )
        {
            result.error = "failed to start '" + request.argv.front() + "'"
                           + ( execFailure ? ": " + std::string( std::strerror( execFailure ) )
                                           : "" );
            result.exitCode = 127;
        }
        else if ( !reaped )
        {
            int status = 0;
            while ( ::waitpid( pid, &status, 0 ) < 0 && errno == EINTR )
            {
            }
            reapStatus( status );
        }
    }

    result.durationMs = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()
                                                               - startTime )
            .count() );
    return result;
}

#endif // !_WIN32

} // namespace exprs
