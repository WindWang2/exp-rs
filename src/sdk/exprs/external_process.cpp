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
#include <cctype>
#include <cwctype>
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

/// Loader-injection deny-list for EXPLICIT request.environment entries
/// (R14-C FIX-7). These keys make the dynamic linker load attacker-chosen
/// code into the spawned tool — a manifest-controlled "LD_PRELOAD" must not
/// become code execution inside every child process. The baseline
/// environment never carries them; only explicit entries are checked.
/// Returns the offending key, empty when the request is clean. Names are
/// matched case-insensitively (Windows env semantics; POSIX variants with
/// odd casing are refused too — there is no honest use for them).
std::string dangerousEnvironmentKey( const ExternalProcessRequest &request )
{
    if ( !request.environment.isObject() )
        return {};
    for ( const std::string &name : request.environment.getMemberNames() )
    {
        // #1690: the child envp entry is assembled as key + "=" + value and
        // the loader/OS splits at the FIRST '='. A key that itself contains
        // '=' therefore becomes a DIFFERENT variable in the child — the key
        // "LD_PRELOAD=/x.so " (value arbitrary) spliced into the envp as
        // "LD_PRELOAD=/x.so =v" made ld.so preload /x.so while the exact-
        // match deny-list below saw only the full string. Empty names and
        // embedded NULs splice the same way (or truncate the entry); no
        // POSIX or Windows environment legitimately carries them. Refuse
        // with the same E5007 channel before any comparison. The empty key
        // reports a descriptive marker (the raw key would read as "clean").
        if ( name.empty() )
            return std::string( "<empty environment key>" );
        if ( name.find( '=' ) != std::string::npos
             || name.find( '\0' ) != std::string::npos )
            return name;
        std::string upper;
        upper.reserve( name.size() );
        for ( const char c : name )
            upper.push_back( static_cast<char>( ::toupper( static_cast<unsigned char>( c ) ) ) );
        if ( upper == "LD_PRELOAD" || upper == "LD_AUDIT" || upper == "LD_LIBRARY_PATH"
             || upper == "LD_CONFIG_FILE" || upper == "DYLD_INSERT_LIBRARIES"
             || upper == "DYLD_LIBRARY_PATH" || upper == "DYLD_FRAMEWORK_PATH"
             || upper == "DYLD_FALLBACK_LIBRARY_PATH" )
            return name;
    }
    return {};
}

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
    for ( size_t index = 0; index < request.argv.size(); ++index )
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
                return std::string( index == 0 ? "program path" : "argument path" )
                    + " escapes the workspace policy: " + argument;
            continue;
        }
        // argv[0] is exempt ONLY while it names a program rather than a
        // location (#1380): an absolute system install or a bare PATH name
        // is the documented external-tool surface, but a RELATIVE program
        // containing ".." is a path fragment and got the same containment
        // check as any other argument above.
        if ( index == 0 )
            continue;
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

/// Drive-absolute or rooted program ("C:\\x", "\\\\srv\\share\\x", "\\x"):
/// the spawn passes it through untouched, so every pre-spawn probe must too
/// (never anchored at the working directory).
bool isDriveAbsolute( const std::wstring &candidate )
{
    if ( candidate.empty() )
        return false;
    if ( candidate[0] == L'\\' )
        return true; // rooted (also covers \\?\ and UNC after joins)
    if ( candidate.size() < 3 )
        return false;
    const wchar_t drive = candidate[0];
    const bool letter = ( drive >= L'a' && drive <= L'z' ) || ( drive >= L'A' && drive <= L'Z' );
    return letter && candidate[1] == L':' && ( candidate[2] == L'\\' || candidate[2] == L'/' );
}

/// Value of @p name (L"PATH", L"PATHEXT", ...) from the child environment
/// block, falling back to the host process value when the block omits it.
/// An empty block means "the child inherits the parent environment"
/// (CreateProcessW null block), so the host fallback matches what the child
/// actually sees.
std::wstring environmentValue( const std::vector<wchar_t> &environment, const wchar_t *name )
{
    const size_t nameLength = wcslen( name );
    for ( size_t index = 0; index + 1 < environment.size(); )
    {
        const std::wstring entry( environment.data() + index );
        if ( entry.size() > nameLength + 1
             && _wcsnicmp( entry.c_str(), name, nameLength ) == 0
             && entry[ nameLength ] == L'=' )
            return entry.substr( nameLength + 1 );
        index += entry.size() + 1;
    }
    const DWORD size = ::GetEnvironmentVariableW( name, nullptr, 0 );
    if ( size == 0 )
        return std::wstring();
    std::wstring value( size, L'\0' );
    ::GetEnvironmentVariableW( name, value.data(), size );
    if ( !value.empty() && value.back() == L'\0' )
        value.pop_back();
    return value;
}

/// Bare-name suffix probing follows PATHEXT order, restricted to the
/// extensions CreateProcessW can execute DIRECTLY (.com/.exe — PE images).
/// .bat/.cmd entries of PATHEXT describe shell scripts: this spawn contract
/// never invokes a shell, so those are reported typed instead of executed
/// (the caller checks them separately for the diagnostic). Falls back to
/// the legacy .com;.exe pair when PATHEXT is unset or lists neither.
std::vector<std::wstring> executableSuffixes( const std::vector<wchar_t> &environment )
{
    std::vector<std::wstring> suffixes;
    const std::wstring pathExt = environmentValue( environment, L"PATHEXT" );
    size_t start = 0;
    while ( start <= pathExt.size() && suffixes.size() < 2 )
    {
        const size_t next = pathExt.find( L';', start );
        const size_t end = next == std::wstring::npos ? pathExt.size() : next;
        std::wstring entry = pathExt.substr( start, end - start );
        for ( wchar_t &character : entry )
            character = static_cast<wchar_t>( ::towlower( character ) );
        if ( ( entry == L".com" || entry == L".exe" )
             && std::find( suffixes.begin(), suffixes.end(), entry ) == suffixes.end() )
            suffixes.push_back( entry );
        if ( next == std::wstring::npos )
            break;
        start = next + 1;
    }
    if ( suffixes.empty() )
    {
        suffixes.push_back( L".com" );
        suffixes.push_back( L".exe" );
    }
    return suffixes;
}

/// Resolves the spawn's application to an ABSOLUTE path with the current
/// directory excluded from the search (#1380). When lpApplicationName is
/// null, Windows re-parses the first command-line token and its search
/// order includes the parent's CURRENT DIRECTORY — a planted .\name.exe
/// executes. Passing an explicit absolute lpApplicationName removes CWD
/// from the resolution entirely: absolute programs pass through, programs
/// with separators anchor at the child's (policy-gated) working
/// directory, and bare names walk the child's PATH with empty entries
/// (the CWD convention) skipped. Suffix probing follows PATHEXT order for
/// the directly-executable extensions (.com/.exe). Returns an empty string
/// when a bare name is nowhere on the PATH; @p shellScriptHit then carries
/// the first .bat/.cmd hit (the typed-refusal diagnostic for shell-script
/// shims — they are never executed by this argv-only contract).
std::wstring resolveWindowsProgram( const std::string &program,
                                    const std::vector<wchar_t> &environment,
                                    const std::string &workingDirectory,
                                    std::wstring &shellScriptHit )
{
    const std::wstring wide = utf8ToWide( program );
    if ( isDriveAbsolute( wide ) )
        return wide;
    if ( program.find( '/' ) != std::string::npos
         || program.find( '\\' ) != std::string::npos )
    {
        // Relative with separators: anchor at the child working directory
        // (the workspace policy already bounds it); std::filesystem mirrors
        // the host-cwd anchoring CreateProcessW applies to a relative
        // lpCurrentDirectory. A ROOTED-without-drive program ("/x") lands
        // on the WORKING DIRECTORY'S DRIVE ROOT — operator/ replaces
        // everything after the root name — which is exactly where
        // CreateProcessW's lpApplicationName below points.
        std::error_code error;
        std::filesystem::path anchored =
            workingDirectory.empty()
                ? std::filesystem::absolute( std::filesystem::path( program ), error )
                : std::filesystem::absolute( std::filesystem::path( workingDirectory ) / program,
                                             error );
        if ( error )
            return std::wstring();
        return anchored.wstring();
    }
    // Bare name: walk the child's PATH only.
    std::wstring searchPath = environmentValue( environment, L"PATH" );
    const std::vector<std::wstring> suffixes = executableSuffixes( environment );
    const bool hasExtension = wide.find( L'.' ) != std::wstring::npos;
    size_t start = 0;
    while ( start <= searchPath.size() )
    {
        const size_t next = searchPath.find( L';', start );
        const size_t end = next == std::wstring::npos ? searchPath.size() : next;
        const std::wstring dir = searchPath.substr( start, end - start );
        // Empty PATH entries mean the current directory — skipped: that is
        // exactly the resolution this function exists to prevent.
        if ( !dir.empty() )
        {
            const std::wstring exact = dir + L"\\" + wide;
            const DWORD attributes = ::GetFileAttributesW( exact.c_str() );
            if ( attributes != INVALID_FILE_ATTRIBUTES
                 && !( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
                return exact;
            if ( !hasExtension )
            {
                for ( const std::wstring &suffix : suffixes )
                {
                    const std::wstring withSuffix = exact + suffix;
                    const DWORD suffixAttributes = ::GetFileAttributesW( withSuffix.c_str() );
                    if ( suffixAttributes != INVALID_FILE_ATTRIBUTES
                         && !( suffixAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
                        return withSuffix;
                }
                // Shell-script shims (npm, code CLI): not executable without
                // a shell. Record the first hit for the typed refusal.
                if ( shellScriptHit.empty() )
                {
                    for ( const wchar_t *scriptSuffix : { L".cmd", L".bat" } )
                    {
                        const std::wstring script = exact + scriptSuffix;
                        const DWORD scriptAttributes = ::GetFileAttributesW( script.c_str() );
                        if ( scriptAttributes != INVALID_FILE_ATTRIBUTES
                             && !( scriptAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
                        {
                            shellScriptHit = script;
                            break;
                        }
                    }
                }
            }
        }
        if ( next == std::wstring::npos )
            break;
        start = next + 1;
    }
    return std::wstring();
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

/// run()'s pre-spawn gate (#1460): resolves argv[0] EXACTLY the way the
/// spawn will (resolveWindowsProgram — the same function run() passes to
/// CreateProcessW), so probe and spawn can never disagree on anchoring
/// (rooted "/x" landing on the drive root), PATH source (the child's
/// environment block) or PATHEXT suffix order. The probe then only adds
/// the existence check for pass-through forms. validateArgv() (advisory,
/// no request) passes an empty environment block and no working directory:
/// the resolver then anchors at the host cwd and searches the host PATH —
/// its documented diagnostics contract.
bool validateArgvAnchored( const std::vector<std::string> &argv,
                           const std::string &workingDirectory,
                           const std::vector<wchar_t> &environment,
                           std::string &error )
{
    if ( argv.empty() || argv.front().empty() )
    {
        error = "argv must start with a program name";
        return false;
    }
    const std::string &program = argv.front();
    std::wstring shellScriptHit;
    const std::wstring resolved =
        resolveWindowsProgram( program, environment, workingDirectory, shellScriptHit );
    if ( resolved.empty() )
    {
        // The current directory is never searched (#1380); a PATH hit that
        // is only a shell-script shim is reported by NAME — executing it
        // would require a shell, which this contract never provides.
        error = shellScriptHit.empty()
                    ? "program '" + program
                          + "' not found in PATH (the current directory is never searched)"
                    : "program '" + program + "' only resolves to the shell script '"
                          + wideToUtf8( shellScriptHit.c_str() )
                          + "'; this spawn contract executes binaries without a shell, so "
                            ".cmd/.bat shims are refused";
        return false;
    }
    const DWORD attributes = ::GetFileAttributesW( resolved.c_str() );
    if ( attributes == INVALID_FILE_ATTRIBUTES || ( attributes & FILE_ATTRIBUTE_DIRECTORY ) )
    {
        error = "program '" + program + "' is not an executable file";
        return false;
    }
    return true;
}

} // namespace

bool ExternalProcess::validateArgv( const std::vector<std::string> &argv, std::string &error )
{
    // Advisory diagnostics (header contract): argv[0] as the PARENT sees it.
    // No working directory or request environment here, so the resolver
    // anchors at the host cwd and walks the host PATH — exactly as before.
    return validateArgvAnchored( argv, std::string(), std::vector<wchar_t>(), error );
}

ExternalProcessResult ExternalProcess::run( const ExternalProcessRequest &request )
{
    ExternalProcessResult result;
    const auto startTime = std::chrono::steady_clock::now();

    // Workspace effect policy (#757): identical gate to POSIX, refuses
    // BEFORE anything is spawned.
    // The policy gate runs BEFORE program validation (#1380): a relative
    // argv[0] containing ".." is a path fragment, and the program probe
    // would otherwise report "not executable" instead of the typed policy
    // refusal.
    {
        const std::string escape = workspaceEffectEscape( request );
        if ( !escape.empty() )
        {
            result.refusedByPolicy = true;
            result.error = "workspace_escape (E5005): " + escape;
            return result;
        }
    }

    // Loader-injection deny-list (R14-C FIX-7): an explicit LD_PRELOAD-class
    // entry is a typed policy refusal, never a spawn.
    {
        const std::string injector = dangerousEnvironmentKey( request );
        if ( !injector.empty() )
        {
            result.refusedByPolicy = true;
            // R1-F7: the refusal covers BOTH deny-list hits (loader-class
            // names) and malformed names (empty/'='/NUL — they splice the
            // child envp entry into a different variable); say so.
            result.error = "env_injection (E5007): explicit environment key '" + injector
                           + "' is a loader-injection or malformed envp name and is refused";
            return result;
        }
    }

    // The child environment block is built BEFORE the program gate: the
    // probe must search the PATH the child will actually see (an explicit
    // request PATH overrides the host's), not the parent's.
    std::vector<wchar_t> environment = environmentBlock( request );

    std::string programError;
    // Anchored gate (#1460): the probe resolves argv[0] with the same
    // resolver the spawn uses (request working directory, child PATH,
    // PATHEXT order), so a validation pass can never diverge from the
    // spawn's own resolution.
    if ( !validateArgvAnchored( request.argv, request.workingDirectory, environment,
                                programError ) )
    {
        result.error = programError;
        return result;
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

    const std::wstring workingDirectory =
        request.workingDirectory.empty() ? std::wstring() : utf8ToWide( request.workingDirectory );

    // #1380: an explicit absolute lpApplicationName takes the current
    // directory out of the program resolution entirely (the command line
    // below still carries the caller's argv[0], so the child's $0 is
    // unchanged).
    std::wstring shellScriptHit;
    const std::wstring applicationName = resolveWindowsProgram(
        request.argv.front(), environment, request.workingDirectory, shellScriptHit );
    if ( applicationName.empty() )
    {
        result.error = shellScriptHit.empty()
                           ? "program '" + request.argv.front()
                                 + "' not found in PATH (the current directory is never searched)"
                           : "program '" + request.argv.front()
                                 + "' only resolves to the shell script '"
                                 + wideToUtf8( shellScriptHit.c_str() )
                                 + "'; this spawn contract executes binaries without a shell, "
                                   "so .cmd/.bat shims are refused";
        ::CloseHandle( stdoutRead );
        ::CloseHandle( stderrRead );
        if ( job )
            ::CloseHandle( job );
        return result;
    }

    PROCESS_INFORMATION processInfo;
    ZeroMemory( &processInfo, sizeof( processInfo ) );
    // CREATE_SUSPENDED: assign the job before the first instruction runs, so
    // the tree can never escape kill-on-close.
    const DWORD creationFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW
                                | CREATE_UNICODE_ENVIRONMENT
                                | EXTENDED_STARTUPINFO_PRESENT;
    const BOOL created =
        attributesOk
            ? ::CreateProcessW( applicationName.c_str(), commandWide.data(), nullptr, nullptr,
                                TRUE,
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
    // Resolve PATH from the CALLING process's environment, matching
    // execvpe(3) semantics ("the search path is obtained from the caller's
    // environment, not from the envp argument"). Reading envp here made a
    // child-env PATH override diverge exec resolution from the pre-spawn
    // probe, which validates against the caller's PATH (R14-C FIX-7).
    pathVal = std::getenv( "PATH" );
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

/// run()'s pre-spawn gate (#1460): same checks as the advisory
/// validateArgv(), except a RELATIVE program is probed where the spawn
/// resolves it — the child chdir()s into the request's working directory
/// before exec, so the probe anchors there too. validateArgv()
/// deliberately keeps the host-cwd probe (its documented diagnostics
/// contract); gating run() on that refused spawns that would have
/// succeeded.
bool validateArgvAnchored( const std::vector<std::string> &argv,
                           const std::string &workingDirectory, std::string &error )
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
        const auto executable = []( const std::string &candidate ) {
            return ::access( candidate.c_str(), X_OK ) == 0;
        };
        if ( !programInSearchPath( program, searchPath, ':', executable ) )
        {
            error = "program '" + program + "' not found in PATH";
            return false;
        }
    }
    else
    {
        // The child chdir()s into workingDirectory before exec; probe the
        // anchored path. Without a working directory the child inherits the
        // host cwd, and so does the probe.
        const std::string probe =
            ( workingDirectory.empty() || PathPolicy::isAbsolute( program ) )
                ? program
                : workingDirectory + "/" + program;
        if ( ::access( probe.c_str(), X_OK ) != 0 )
        {
            error = "program '" + program + "' is not executable";
            return false;
        }
    }
    return true;
}

} // namespace

bool programInSearchPath( const std::string &program, const std::string &searchPath,
                          char separator,
                          const std::function<bool( const std::string & )> &isExecutable )
{
    size_t start = 0;
    while ( start <= searchPath.size() )
    {
        const size_t next = searchPath.find( separator, start );
        const size_t end = next == std::string::npos ? searchPath.size() : next;
        const std::string entry = searchPath.substr( start, end - start );
        // An empty entry is the CURRENT DIRECTORY (the POSIX exec
        // convention) — the previous splice built "/program" and probed the
        // filesystem root instead of the publisher's working directory.
        const std::string candidate =
            ( entry.empty() ? std::string( "." ) : entry ) + "/" + program;
        if ( isExecutable( candidate ) )
            return true;
        if ( next == std::string::npos )
            break;
        start = next + 1;
    }
    return false;
}

bool ExternalProcess::validateArgv( const std::vector<std::string> &argv, std::string &error )
{
    // Advisory diagnostics (header contract): argv[0] as the PARENT sees it.
    // No working directory here, so a relative program anchors at the host
    // cwd exactly as before.
    return validateArgvAnchored( argv, std::string(), error );
}

ExternalProcessResult ExternalProcess::run( const ExternalProcessRequest &request )
{
    ExternalProcessResult result;
    const auto startTime = std::chrono::steady_clock::now();

    // Workspace effect policy (#757): refuses BEFORE spawning anything when
    // a resolved effect escapes SICNU_MCP_WORKSPACE (or the declared extra
    // roots). Manifest constants cannot bypass it — the resolved values are
    // what get checked.
    // The policy gate runs BEFORE program validation (#1380): a relative
    // argv[0] containing ".." is a path fragment, and the program probe
    // would otherwise report "not executable" instead of the typed policy
    // refusal.
    {
        const std::string escape = workspaceEffectEscape( request );
        if ( !escape.empty() )
        {
            result.refusedByPolicy = true;
            result.error = "workspace_escape (E5005): " + escape;
            return result;
        }
    }

    // Loader-injection deny-list (R14-C FIX-7): an explicit LD_PRELOAD-class
    // entry is a typed policy refusal, never a spawn.
    {
        const std::string injector = dangerousEnvironmentKey( request );
        if ( !injector.empty() )
        {
            result.refusedByPolicy = true;
            // R1-F7: the refusal covers BOTH deny-list hits (loader-class
            // names) and malformed names (empty/'='/NUL — they splice the
            // child envp entry into a different variable); say so.
            result.error = "env_injection (E5007): explicit environment key '" + injector
                           + "' is a loader-injection or malformed envp name and is refused";
            return result;
        }
    }

    std::string programError;
    // Anchored gate (#1460): the child chdir()s into the request's working
    // directory before exec, so a relative argv[0] is probed there — not
    // against the host cwd the advisory validateArgv() reports on.
    if ( !validateArgvAnchored( request.argv, request.workingDirectory, programError ) )
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
        // A partial failure leaves the earlier pipes open: EMFILE/ENFILE can
        // strike on the second or third call, leaking up to 4 fds (the
        // Windows branch and the fork() cleanup below close everything).
        // Close whatever succeeded — a pipe that failed keeps its -1
        // initializer. The message is built first so close() cannot clobber
        // the errno it was captured from.
        for ( int fd : { stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1],
                         errorPipe[0], errorPipe[1] } )
            if ( fd >= 0 )
                ::close( fd );
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

    sigset_t blockMask, origMask;
    sigemptyset( &blockMask );
    for ( int sig : { SIGTERM, SIGINT, SIGQUIT, SIGABRT, SIGSEGV, SIGPIPE } )
        sigaddset( &blockMask, sig );
    ::sigprocmask( SIG_BLOCK, &blockMask, &origMask );

    const pid_t pid = ::fork();
    if ( pid < 0 )
    {
        ::sigprocmask( SIG_SETMASK, &origMask, nullptr );
        result.error = "fork() failed: " + std::string( std::strerror( errno ) );
        for ( int fd : { stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1],
                         errorPipe[0], errorPipe[1] } )
            ::close( fd );
        return result;
    }

    if ( pid == 0 )
    {
        // Child: reset signals to default so parent/Catch2 handlers do not intercept signals before exec.
        struct sigaction sa;
        std::memset( &sa, 0, sizeof( sa ) );
        sa.sa_handler = SIG_DFL;
        sigemptyset( &sa.sa_mask );
        for ( int sig : { SIGTERM, SIGINT, SIGQUIT, SIGABRT, SIGSEGV, SIGPIPE } )
            ::sigaction( sig, &sa, nullptr );
        ::sigprocmask( SIG_SETMASK, &origMask, nullptr );

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
    ::sigprocmask( SIG_SETMASK, &origMask, nullptr );
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
    bool exited = false;
    // A grandchild holding inherited write ends can keep the pipes open
    // forever after the child exited (daemonize/double-fork tools); bound the
    // post-exit drain so run() always returns and a completed child is never
    // misreported as timedOut (issue #1041, mirrors the Windows
    // kPostExitDrainGraceMs).
    std::chrono::steady_clock::time_point postExitDrainDeadline{};
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

        // Child liveness (WNOHANG): the normal loop must notice the child's
        // exit even while a descendant keeps the pipes open — pipe EOF alone
        // is NOT an exit signal (issue #1041).
        if ( !exited )
        {
            int status = 0;
            const pid_t done = ::waitpid( pid, &status, WNOHANG );
            if ( done == pid )
            {
                reapStatus( status );
                exited = true;
                postExitDrainDeadline = std::chrono::steady_clock::now()
                                        + std::chrono::milliseconds( kPostExitDrainGraceMs );
            }
            else if ( done < 0 && errno == ECHILD )
            {
                // Auto-reaped (SIGCHLD ignored by the embedding process): the
                // child is gone and no status can be collected — report the
                // unknown status honestly (exitCode stays -1) instead of
                // inventing one from an uninitialized waitpid buffer.
                reaped = true;
                exited = true;
                postExitDrainDeadline = std::chrono::steady_clock::now()
                                        + std::chrono::milliseconds( kPostExitDrainGraceMs );
            }
        }

        // Timeout/cancel only govern a LIVE child; once it exited, the
        // bounded drain below decides when to stop (mirrors Windows).
        if ( !exited )
        {
            if ( !cancelled && request.isCancelled && request.isCancelled() )
                cancelled = true;
            if ( !timedOut && std::chrono::steady_clock::now() >= deadline )
                timedOut = true;
        }

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

        // Child exited but a descendant still holds the write ends: drain
        // buffered tail output for a bounded grace, then stop — never spin
        // to the (possibly hour-long) timeout (issue #1041).
        if ( exited && std::chrono::steady_clock::now() >= postExitDrainDeadline )
            break;

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
        // After the child exited the drain grace (not the request deadline)
        // bounds the wait; using the deadline could busy-poll at 1 ms once it
        // has passed.
        const auto until = exited ? postExitDrainDeadline : deadline;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            until - std::chrono::steady_clock::now() );
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
