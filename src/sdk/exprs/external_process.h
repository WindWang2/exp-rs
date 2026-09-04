/***************************************************************************
 * exprs/external_process.h — safe external process execution
 *
 * Security contract (docs/plugins/external-process.md):
 *   - argv-only spawn: the command is exec'd directly, NEVER through a
 *     shell, so shell metacharacters are data, not syntax.
 *   - Process group isolation: the child runs in its own session (setsid);
 *     cancellation/timeout terminate the whole group, not just the leader.
 *   - Bounded output: stdout/stderr are captured up to per-stream byte
 *     limits; excess is drained and flagged, never allowed to exhaust
 *     memory.
 *   - Environment: by default the child inherits only PATH, HOME, TMPDIR
 *     and LANG. Full inheritance is opt-in (env can carry credentials).
 *   - Timeouts + cancellation: cooperative cancel callback is polled; on
 *     timeout/cancel the SIGTERM->SIGKILL ladder kills the process group.
 *   - No zombie leakage: every spawn is reaped via waitpid before run()
 *     returns.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <functional>
#include <string>
#include <vector>

namespace exprs {

struct ExternalProcessRequest
{
    std::vector<std::string> argv;         ///< argv[0] = program (no shell)
    std::string workingDirectory;          ///< optional; must exist
    Json::Value environment;               ///< object: extra env vars (name -> string)
    bool inheritEnvironment = false;       ///< full env inheritance (opt-in)
    int timeoutSeconds = 3600;             ///< wall-clock budget (<=0 = 3600)
    long stdoutLimitBytes = 8 * 1024 * 1024;
    long stderrLimitBytes = 1 * 1024 * 1024;
    /// Polled during execution; returning true cancels (SIGTERM ladder).
    std::function<bool()> isCancelled;
    /// Optional progress sink receiving stdout chunk offsets (bytes so far).
    std::function<void( long stdoutBytes, long stderrBytes )> onOutput;
};

struct ExternalProcessResult
{
    bool started = false;      ///< process was spawned successfully
    int exitCode = -1;         ///< raw waitpid exit status decoding
    int exitSignal = 0;        ///< non-zero when the child died from a signal
    bool exitedCleanly() const { return started && exitSignal == 0 && exitCode == 0; }
    bool timedOut = false;
    bool cancelled = false;
    bool truncatedStdout = false;
    bool truncatedStderr = false;
    std::string stdOut;        ///< bounded capture
    std::string stdErr;        ///< bounded capture
    std::string error;         ///< spawn failure or diagnostic message
    long durationMs = 0;
};

class ExternalProcess
{
public:
    /// Synchronous run. Always reaps the child before returning.
    static ExternalProcessResult run( const ExternalProcessRequest &request );

    /// Diagnostics helper: verifies argv[0] is an existing absolute path or
    /// resolves in the PARENT's PATH. Advisory only — resolution at exec
    /// time uses the CHILD environment (a manifest may override PATH); the
    /// security boundary is argv-only spawn, never this check.
    static bool validateArgv( const std::vector<std::string> &argv, std::string &error );
};

} // namespace exprs
