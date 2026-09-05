/***************************************************************************
 * exprs/external_process.cpp
 ***************************************************************************/
#include "exprs/external_process.h"

#ifdef _WIN32
// Windows MSVC build seam (Tier 3): POSIX fork/exec is unavailable. Provide a
// stub so the compilation guard passes; external tools are unsupported here.
namespace exprs
{
bool ExternalProcess::validateArgv( const std::vector<std::string> &argv, std::string &error )
{
    if ( argv.empty() || argv.front().empty() )
    {
        error = "argv must start with a program name";
        return false;
    }
    error = "external process execution is not supported on Windows builds";
    return false;
}

ExternalProcessResult ExternalProcess::run( const ExternalProcessRequest &request )
{
    ExternalProcessResult result;
    (void)request;
    result.error = "external process execution is not supported on Windows builds";
    return result;
}
} // namespace exprs
#else

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <map>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#if defined( __APPLE__ ) || defined( __FreeBSD__ ) || defined( __OpenBSD__ ) || defined( __NetBSD__ )
// POSIX environ — not declared by <unistd.h> on macOS/BSD.
extern char **environ;
#endif

namespace exprs {

namespace {

constexpr int kTerminateGraceMs = 2000;

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

class BoundedSink
{
public:
    BoundedSink( std::string &target, long limit, bool &truncatedFlag )
        : mTarget( target )
        , mLimit( limit > 0 ? limit : 1 )
        , mTruncated( truncatedFlag )
    {
    }

    void consume( const char *data, ssize_t length )
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

} // namespace exprs

#endif // !_WIN32
