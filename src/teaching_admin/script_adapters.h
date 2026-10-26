// script_adapters.h — structured subprocess adapters (never free-text log scrape).
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::teaching_admin {

struct ScriptRunRequest
{
    QString program;          ///< harness-pinned python3 / bash / absolute tool
    QStringList arguments;
    QString workingDirectory;
    int timeoutMs = 120000;
    /// Bounded capture (#1466): retained head per stream, in the spirit of
    /// ExternalProcess's stdout/stderr limits — a runaway child (a whole
    /// classroom batch) must not be able to OOM the admin console through
    /// unbounded QProcess buffers. Excess is drained and flagged, never
    /// buffered whole.
    int stdoutLimitBytes = 8 * 1024 * 1024;
    int stderrLimitBytes = 1 * 1024 * 1024;
};

struct ScriptRunResult
{
    bool started = false;
    bool timedOut = false;
    bool crashed = false; ///< child died abnormally (signal) — exitCode meaningless
    int exitCode = -1;
    QByteArray stdoutBytes;      ///< retained head of stdout (bounded by the request)
    QByteArray stderrBytes;      ///< retained head of stderr (bounded by the request)
    bool stdoutTruncated = false; ///< stdout exceeded stdoutLimitBytes (#1466)
    bool stderrTruncated = false; ///< stderr exceeded stderrLimitBytes (#1466)
    QString error; ///< typed when process could not start

    QJsonObject toJson() const;
};

/// Run a controlled subprocess. Captures stdout/stderr; does not scrape logs
/// for semantics — callers parse known JSON/exit contracts.
ScriptRunResult runScript( const ScriptRunRequest &req );

/// The stack's own Python: SICNU_PYTHON_EXECUTABLE / PYTHONEXECUTABLE when
/// the harness (or a deployment) pins one; else a real executable discovered
/// on PATH (per-OS candidate order) and VERIFIED to actually start and load
/// the stdlib — on Windows the WindowsApps Store alias stub resolves like a
/// real interpreter for any candidate name but exits 9009, so each candidate
/// is probed (`python -c "import re"`) and unusable ones are skipped (#1474
/// review); the resolved path is absolute. The bare "python3" name survives
/// only as the last resort when nothing verifiable is discoverable. The test
/// harness exports PYTHONHOME/PYTHONPATH describing its CMake-configured
/// interpreter (cmake/SicnuTestEnv.cmake, TEST_INFRA.md #730), and
/// src/python/isolated/python_worker_process.cpp resolves the same pins for
/// exactly that reason. A bare "python3" is merely whatever PATH offers:
/// when that is a DIFFERENT interpreter than PYTHONHOME describes, the child
/// inherits a foreign stdlib — PYTHONPATH wins over the home stdlib in
/// sys.path, so the script loads one version's `re` package with another
/// version's `_sre` extension and dies at `import re` ("SRE module mismatch")
/// before any script line runs. Same fix and priority order as
/// #1431 (python worker provider); the start probe closes the Windows
/// alias/start_failed hole (#1466, #1474 review).
QString resolveDefaultPythonInterpreter();

/// Adapter: gen_lab_packs.py → structured result.
ScriptRunResult runGenLabPacks( const QString &repoRoot, const QString &pythonExe = resolveDefaultPythonInterpreter() );

/// Adapter: verify_bundle_manifest.py <bundleDir>.
struct BundleVerifyResult
{
    bool verifiable = false; ///< false when exit 2 / missing script
    bool ok = false;
    int exitCode = -1;
    QStringList findings;
    QString summary;
    /// The verifier transcript exceeded the capture cap (#1474 review): the
    /// exit-code verdict stands, but findings/summary are refused — a
    /// retained head must never be published as the complete output.
    bool outputTruncated = false;
    // Manifest metadata surfaced for the teacher (projection only — the
    // canonical verifier script stays the only digest authority).
    QString manifestSchema;
    QString bundleVersion;
    QJsonObject toJson() const;
};

BundleVerifyResult verifyOfflineBundle( const QString &repoRoot, const QString &bundleDir,
                                        const QString &pythonExe = resolveDefaultPythonInterpreter(),
                                        const QString &expectedVersion = QString() );

/// Read-only look at a bundle's manifest.json (no subprocess): schema id,
/// declared bundle_version and file count. ok=false with a typed issue when
/// the manifest is missing or unparsable.
struct BundleManifestInfo
{
    bool ok = false;
    QString schema;
    QString bundleVersion;
    int fileCount = -1;
    QVector<AdminIssue> issues;
    QJsonObject toJson() const;
};

BundleManifestInfo inspectBundleManifest( const QString &bundleDir );

/// Pure argv construction for the bundle builder (testable without
/// executing): selects build_offline_bundle.sh (bash) vs .cmd (cmd.exe) per
/// platform. @p windows mirrors Q_OS_WIN at the call site.
///
/// #1688: the request is REFUSED (empty program and arguments) when any
/// parameter fails validateBundleBuilderParams() — on Windows the argv is
/// re-parsed by cmd.exe, so metacharacters without spaces stay unquoted and
/// become command separators. Legal inputs produce exactly the request the
/// platform twin expects (unchanged shape).
ScriptRunRequest bundleBuilderRequest( const QString &repoRoot, const QString &buildDir,
                                       const QString &outDir, const QString &version, int maxMb,
                                       bool windows );

/// #1688: parameter validation shared by every bundle-builder entry point.
/// Empty result = safe on both the bash and cmd.exe paths. Each rejection
/// names the parameter and the offending character class:
/// - version (when set) must match [0-9A-Za-z._-]+;
/// - buildDir/outDir must be non-empty and free of cmd.exe metacharacters
///   (& | < > ^ % ! ") and control characters (newline/CR/tab).
QStringList validateBundleBuilderParams( const QString &buildDir, const QString &outDir,
                                         const QString &version );

/// Adapter: build_offline_bundle.sh (POSIX) / .cmd (Windows). Refuses to
/// start a process when validateBundleBuilderParams() rejects — the typed
/// failure carries the rejections verbatim (started=false).
ScriptRunResult buildOfflineBundle( const QString &repoRoot, const QString &buildDir, const QString &outDir,
                                    const QString &version = QString(), int maxMb = 250 );

/// Foundry drift check (sicnu.lab-pack authority): wraps
/// `scripts/gen_lab_packs.py --check` — exit 0 "packs in sync", exit 1 with
/// `DRIFT <path>` lines. Reuses the foundry contract; C++ never regenerates.
struct PackDriftCheck
{
    bool ran = false;
    int exitCode = -1;
    bool inSync = false;
    QStringList driftFiles;
    QString summary;
    /// The foundry transcript exceeded the capture cap (#1474 review): the
    /// exit code still decides inSync, but the DRIFT list is refused — a
    /// truncated list would under-report real classroom drift.
    bool outputTruncated = false;
    QJsonObject toJson() const;
};

PackDriftCheck checkLabPackDrift( const QString &repoRoot,
                                  const QString &pythonExe = resolveDefaultPythonInterpreter() );

/// Adapter: run_classroom_batch.py
ScriptRunResult runClassroomBatch( const QString &repoRoot, const QString &cliPath, const QString &labId,
                                   const QString &submissionsDir, const QString &outPrefix, int jobs = 2,
                                   int timeoutSec = 120, const QString &pythonExe = resolveDefaultPythonInterpreter() );

} // namespace sicnu::teaching_admin
