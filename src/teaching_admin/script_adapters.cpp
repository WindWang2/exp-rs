#include "script_adapters.h"

#include "json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>

namespace sicnu::teaching_admin {

namespace
{

/// "Found on PATH" must not mean "accepted": a PATH-resolved candidate is
/// only trustworthy once it actually runs. On Windows the WindowsApps
/// execution alias resolves like a real python.exe but launches, prints the
/// Store nag and exits 9009 when no interpreter is installed — and it can
/// shadow ANY candidate name, including "python" (#1474 review). A
/// mismatched-stdlib interpreter dies at `import re` (the SRE failure this
/// resolver exists to avoid, #1431). Probing `import re` rejects both
/// shapes: launch ok but non-zero exit, or no launch at all.
bool interpreterStarts( const QString &program )
{
    QProcess probe;
    probe.start( program, QStringList{ QStringLiteral( "-c" ), QStringLiteral( "import re" ) } );
    if ( !probe.waitForStarted( 3000 ) )
        return false;
    if ( !probe.waitForFinished( 10000 ) )
    {
        probe.kill();
        probe.waitForFinished( 3000 );
        return false;
    }
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

// Version-pin epilogue shared by every transcript outcome of
// verifyOfflineBundle: the manifest is metadata the teacher pinned at export
// time. A mismatch is a typed failed verification — the digest authority
// itself is unchanged (the canonical script).
BundleVerifyResult finalizeBundleVerify( BundleVerifyResult r, const QString &bundleDir,
                                         const QString &expectedVersion )
{
    const BundleManifestInfo manifest = inspectBundleManifest( bundleDir );
    r.manifestSchema = manifest.schema;
    r.bundleVersion = manifest.bundleVersion;
    if ( !expectedVersion.isEmpty() && manifest.ok && manifest.bundleVersion != expectedVersion )
    {
        r.ok = false;
        r.findings.append( QStringLiteral( "version_mismatch: declared %1, expected %2" )
                             .arg( manifest.bundleVersion, expectedVersion ) );
        r.summary = QStringLiteral( "version_mismatch" );
    }
    return r;
}

} // namespace

/// A caller that names no interpreter means "this stack's own Python".
/// The harness pins that interpreter through the environment: PYTHONHOME and
/// PYTHONPATH describe the CMake-configured one (cmake/SicnuTestEnv.cmake,
/// TEST_INFRA.md #730), and src/python/isolated/python_worker_process.cpp
/// resolves SICNU_PYTHON_EXECUTABLE / PYTHONEXECUTABLE for exactly that
/// reason. A bare "python3" is merely whatever PATH offers: when that is a
/// DIFFERENT interpreter than PYTHONHOME describes, the child inherits a
/// stdlib it was never built against — PYTHONPATH wins over the home stdlib
/// in sys.path, so it loads one version's `re` package and another version's
/// `_sre` extension and dies at `import re` ("SRE module mismatch") before
/// any script line runs (master run 36568313972: the teaching admin dock
/// cancel smoke graded 0 of 20 rows this way). Resolve the same pin here so
/// every adapter IS the interpreter the environment describes.
///
/// Unpinned deployments (a packaged app, an MSVC build outside the harness):
/// probe PATH for a real interpreter and return its ABSOLUTE path, the same
/// candidate-list idea as python_worker_process.cpp. On Windows the order
/// matters: "python3" there is usually the WindowsApps Store alias stub —
/// but "python" can resolve to the same stub, and the stub is only
/// distinguishable by RUNNING it (it launches, prints the Store nag, exits
/// 9009). Every candidate is therefore verified to actually start and load
/// the stdlib before being accepted (#1474 review); unusable ones are
/// skipped so a later real candidate still wins. The bare "python3" survives
/// only as the typed last resort when nothing verifiable is discoverable
/// (POSIX keeps python3 first: it is the canonical name there).
QString resolveDefaultPythonInterpreter()
{
    static const char *const kPins[] = { "SICNU_PYTHON_EXECUTABLE", "PYTHONEXECUTABLE" };
    for ( const char *pin : kPins )
    {
        const QString value = qEnvironmentVariable( pin ).trimmed();
        if ( !value.isEmpty() && QFileInfo::exists( value ) )
            return value;
    }
    // One verified result per process: this resolver is re-evaluated as a
    // default argument on every adapter call, and each probe round spawns a
    // process per candidate. Failures are never cached — a PATH that gains a
    // real interpreter mid-session must still be discovered.
    static QMutex cacheMutex;
    static QString cachedVerified;
    {
        const QMutexLocker lock( &cacheMutex );
        if ( !cachedVerified.isEmpty() )
            return cachedVerified;
    }
#ifdef Q_OS_WIN
    static const char *const kCandidates[] = { "python", "python.exe", "python3", "python3.exe" };
#else
    static const char *const kCandidates[] = { "python3", "python" };
#endif
    QStringList tried;
    for ( const char *candidate : kCandidates )
    {
        const QString resolved =
          QStandardPaths::findExecutable( QString::fromLatin1( candidate ) );
        // "python" and "python.exe" routinely resolve to the same file —
        // probe each distinct path once per round.
        if ( resolved.isEmpty() || tried.contains( resolved ) )
            continue;
        tried.append( resolved );
        if ( interpreterStarts( resolved ) )
        {
            const QMutexLocker lock( &cacheMutex );
            cachedVerified = resolved;
            return resolved;
        }
    }
    return QStringLiteral( "python3" );
}

QJsonObject ScriptRunResult::toJson() const
{
    return QJsonObject{
        { QStringLiteral( "started" ), started },
        { QStringLiteral( "crashed" ), crashed },
        { QStringLiteral( "exit_code" ), exitCode },
        { QStringLiteral( "timed_out" ), timedOut },
        { QStringLiteral( "stdout_bytes" ), stdoutBytes.size() },
        { QStringLiteral( "stderr_bytes" ), stderrBytes.size() },
        // Bounded-capture honesty (#1466): the reader must be able to tell a
        // complete stream from a retained head.
        { QStringLiteral( "stdout_truncated" ), stdoutTruncated },
        { QStringLiteral( "stderr_truncated" ), stderrTruncated },
        { QStringLiteral( "error" ), error },
        // Include truncated stdout for JSON contracts (structured adapters only).
        { QStringLiteral( "stdout_utf8" ), QString::fromUtf8( stdoutBytes.left( 64 * 1024 ) ) },
        { QStringLiteral( "stderr_utf8" ), QString::fromUtf8( stderrBytes.left( 16 * 1024 ) ) },
    };
}

ScriptRunResult runScript( const ScriptRunRequest &req )
{
    ScriptRunResult out;
    if ( req.program.isEmpty() )
    {
        out.error = QStringLiteral( "empty_program" );
        return out;
    }
    QProcess proc;
    if ( !req.workingDirectory.isEmpty() )
        proc.setWorkingDirectory( req.workingDirectory );
    proc.start( req.program, req.arguments );
    if ( !proc.waitForStarted( 5000 ) )
    {
        out.error = QStringLiteral( "start_failed: " ) + proc.errorString();
        return out;
    }
    out.started = true;

    // Bounded capture (#1466): drain both channels as data arrives instead
    // of letting QProcess buffer the whole stream — waitForFinished pumps
    // these signals while it waits. The retained HEAD feeds the JSON
    // contracts (a truncated transcript fails its own parse, fail-closed);
    // the truncated flags keep the loss observable in toJson().
    qint64 stdoutTotal = 0;
    qint64 stderrTotal = 0;
    const auto drainStdout = [&]() {
        const QByteArray chunk = proc.readAllStandardOutput();
        stdoutTotal += chunk.size();
        if ( out.stdoutBytes.size() < req.stdoutLimitBytes )
            out.stdoutBytes.append(
              chunk.left( req.stdoutLimitBytes - out.stdoutBytes.size() ) );
    };
    const auto drainStderr = [&]() {
        const QByteArray chunk = proc.readAllStandardError();
        stderrTotal += chunk.size();
        if ( out.stderrBytes.size() < req.stderrLimitBytes )
            out.stderrBytes.append(
              chunk.left( req.stderrLimitBytes - out.stderrBytes.size() ) );
    };
    QObject::connect( &proc, &QProcess::readyReadStandardOutput,
                      [ &drainStdout ]() { drainStdout(); } );
    QObject::connect( &proc, &QProcess::readyReadStandardError,
                      [ &drainStderr ]() { drainStderr(); } );
    // Final drain + flags on every exit path: the last chunk can sit
    // un-signalled, and the flags must describe the totals, not the head.
    const auto finalizeOutput = [&]() {
        drainStdout();
        drainStderr();
        out.stdoutTruncated = stdoutTotal > out.stdoutBytes.size();
        out.stderrTruncated = stderrTotal > out.stderrBytes.size();
    };

    if ( !proc.waitForFinished( req.timeoutMs ) )
    {
        proc.kill();
        proc.waitForFinished( 3000 );
        out.timedOut = true;
        out.exitCode = -1;
        finalizeOutput();
        out.error = QStringLiteral( "timeout" );
        return out;
    }
    // exitCode() is not meaningful for an abnormal exit (Qt docs): a grader
    // that crashed must never be read as a normal exit-0 verdict.
    if ( proc.exitStatus() != QProcess::NormalExit )
    {
        out.crashed = true;
        out.exitCode = -1;
        finalizeOutput();
        out.error = QStringLiteral( "process crashed abnormally" );
        return out;
    }
    out.exitCode = proc.exitCode();
    finalizeOutput();
    return out;
}

ScriptRunResult runGenLabPacks( const QString &repoRoot, const QString &pythonExe )
{
    ScriptRunRequest req;
    req.program = pythonExe;
    req.arguments = { QStringLiteral( "scripts/gen_lab_packs.py" ) };
    req.workingDirectory = repoRoot;
    req.timeoutMs = 180000;
    return runScript( req );
}

QJsonObject BundleVerifyResult::toJson() const
{
    QJsonArray findingsArr;
    for ( const auto &f : findings )
        findingsArr.append( f );
    return QJsonObject{
        { QStringLiteral( "verifiable" ), verifiable },
        { QStringLiteral( "ok" ), ok },
        { QStringLiteral( "exit_code" ), exitCode },
        { QStringLiteral( "summary" ), summary },
        { QStringLiteral( "manifest_schema" ), manifestSchema },
        { QStringLiteral( "bundle_version" ), bundleVersion },
        { QStringLiteral( "findings" ), findingsArr },
        { QStringLiteral( "output_truncated" ), outputTruncated },
    };
}

BundleVerifyResult verifyOfflineBundle( const QString &repoRoot, const QString &bundleDir,
                                        const QString &pythonExe, const QString &expectedVersion )
{
    BundleVerifyResult r;
    const QString script = QDir( repoRoot ).filePath( QStringLiteral( "scripts/verify_bundle_manifest.py" ) );
    if ( !QFileInfo::exists( script ) )
    {
        r.verifiable = false;
        r.summary = QStringLiteral( "verifier_missing" );
        return r;
    }
    ScriptRunRequest req;
    req.program = pythonExe;
    req.arguments = { script, bundleDir };
    req.workingDirectory = repoRoot;
    req.timeoutMs = 120000;
    const ScriptRunResult sr = runScript( req );
    r.exitCode = sr.exitCode;
    r.outputTruncated = sr.stdoutTruncated || sr.stderrTruncated;
    if ( !sr.started || sr.timedOut || sr.crashed )
    {
        r.verifiable = false;
        r.summary = sr.error;
        return r;
    }
    // Exit contract: 0 verified ok, 1 verified-and-failed, 2 cannot verify
    if ( sr.exitCode == 2 )
    {
        r.verifiable = false;
        r.summary = r.outputTruncated
                      ? QStringLiteral( "output_truncated" )
                      : QString::fromUtf8( sr.stderrBytes.isEmpty() ? sr.stdoutBytes : sr.stderrBytes ).trimmed();
        return r;
    }
    r.verifiable = true;
    r.ok = ( sr.exitCode == 0 );
    if ( r.outputTruncated )
    {
        // Fail-closed (#1474 review): the transcript is a retained head, so
        // the parsed findings would be an arbitrary prefix of the verifier's
        // real output. The exit code stays the verdict; the transcript is
        // refused rather than published as complete detail. (A simultaneous
        // version_mismatch still wins the summary via the shared epilogue —
        // that finding is derived locally from the manifest, not from the
        // truncated transcript.)
        r.summary = QStringLiteral( "output_truncated" );
        return finalizeBundleVerify( r, bundleDir, expectedVersion );
    }
    const QString text = QString::fromUtf8( sr.stdoutBytes ) + QStringLiteral( "\n" )
                         + QString::fromUtf8( sr.stderrBytes );
    const QStringList lines = text.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
    for ( const QString &line : lines )
    {
        if ( line.startsWith( QLatin1String( "VERDICT" ) ) || line.startsWith( QLatin1String( "OK" ) )
             || line.startsWith( QLatin1String( "FAIL" ) ) )
            r.summary = line;
        else
            r.findings.append( line );
    }
    if ( r.summary.isEmpty() )
        r.summary = r.ok ? QStringLiteral( "verified_ok" ) : QStringLiteral( "verified_failed" );
    return finalizeBundleVerify( r, bundleDir, expectedVersion );
}

BundleManifestInfo inspectBundleManifest( const QString &bundleDir )
{
    BundleManifestInfo info;
    const QString path = QDir( bundleDir ).filePath( QStringLiteral( "manifest.json" ) );
    // Bounded read (R14-E audit F5): an oversized manifest is a typed
    // finding, not an unbounded slurp into memory.
    constexpr qint64 kMaxManifestBytes = 16 * 1024 * 1024;
    const BoundedReadResult read = readBounded( path, kMaxManifestBytes );
    if ( read.oversize )
    {
        info.issues.push_back( { QStringLiteral( "manifest_oversize" ), QStringLiteral( "manifest.json" ),
                                 QStringLiteral( "bundle manifest exceeds the 16 MiB read budget" ),
                                 QStringLiteral( "error" ) } );
        return info;
    }
    if ( read.unreadable )
    {
        info.issues.push_back( { QStringLiteral( "manifest_unreadable" ), QStringLiteral( "manifest.json" ),
                                 QStringLiteral( "bundle manifest missing or unreadable" ),
                                 QStringLiteral( "error" ) } );
        return info;
    }
    const QJsonObject manifest = QJsonDocument::fromJson( read.bytes ).object();
    if ( manifest.isEmpty() )
    {
        info.issues.push_back( { QStringLiteral( "manifest_invalid" ), QStringLiteral( "manifest.json" ),
                                 QStringLiteral( "bundle manifest is not a JSON object" ),
                                 QStringLiteral( "error" ) } );
        return info;
    }
    info.ok = true;
    info.schema = manifest.value( QStringLiteral( "schema" ) ).toString();
    info.bundleVersion = manifest.value( QStringLiteral( "bundle_version" ) ).toString();
    info.fileCount = manifest.value( QStringLiteral( "files" ) ).toArray().size();
    if ( info.bundleVersion.isEmpty() )
        info.issues.push_back( { QStringLiteral( "manifest_version_missing" ),
                                 QStringLiteral( "bundle_version" ),
                                 QStringLiteral( "manifest carries no bundle_version" ),
                                 QStringLiteral( "warning" ) } );
    return info;
}

QJsonObject BundleManifestInfo::toJson() const
{
    QJsonArray issueArr;
    for ( const auto &i : issues )
        issueArr.append( i.toJson() );
    return QJsonObject{
        { QStringLiteral( "ok" ), ok },
        { QStringLiteral( "schema" ), schema },
        { QStringLiteral( "bundle_version" ), bundleVersion },
        { QStringLiteral( "file_count" ), fileCount },
        { QStringLiteral( "issues" ), issueArr },
    };
}

QStringList validateBundleBuilderParams( const QString &buildDir, const QString &outDir,
                                         const QString &version )
{
    // #1688. On Windows the constructed argv reaches cmd.exe /c, which
    // re-parses the JOINED command line: & | ^ become command separators,
    // %VAR% expands, ! expands under delayed expansion, and redirects eat
    // < >. QProcess only quotes arguments containing spaces/quotes, so a
    // metacharacter inside a space-free argument rides through verbatim
    // ("--version 1.0&calc" runs calc; a legal C:\R&D\out is truncated at
    // the &). Paths keep every legal character (colon, slash, unicode);
    // only the interpreter-active set is refused. The same policy guards
    // the bash twin for uniformity.
    static const QString kMetacharacters = QStringLiteral( "&|<>^%!\"\n\r\t" );
    QStringList rejections;
    const auto checkPath = [&rejections]( const QString &value, const char *param ) {
        if ( value.isEmpty() )
        {
            rejections << QStringLiteral( "%1: must not be empty" ).arg( QLatin1String( param ) );
            return;
        }
        for ( const QChar &c : value )
        {
            if ( kMetacharacters.contains( c ) )
            {
                rejections << QStringLiteral( "%1: forbidden character '%2' (cmd.exe metacharacter)" )
                                .arg( QLatin1String( param ), QString( c ) );
                return;
            }
        }
    };
    checkPath( buildDir, "build_dir" );
    checkPath( outDir, "out_dir" );
    if ( !version.isEmpty() )
    {
        for ( const QChar &c : version )
        {
            const bool allowed = ( c.isLetterOrNumber() && c.toLatin1() != 0 )
                                 || c == QLatin1Char( '.' ) || c == QLatin1Char( '_' )
                                 || c == QLatin1Char( '-' );
            if ( !allowed )
            {
                rejections << QStringLiteral(
                                 "version: allowed characters are [0-9A-Za-z._-] (got '%1')" )
                                 .arg( QString( c ) );
                break;
            }
        }
    }
    return rejections;
}

ScriptRunRequest bundleBuilderRequest( const QString &repoRoot, const QString &buildDir,
                                       const QString &outDir, const QString &version, int maxMb,
                                       bool windows )
{
    ScriptRunRequest req;
    // #1688: fail closed — an injection-shaped parameter yields an EMPTY
    // request (no program), which no caller can execute, instead of an
    // argv that cmd.exe would re-parse into something else. Legal inputs
    // build exactly the same request as before.
    if ( !validateBundleBuilderParams( buildDir, outDir, version ).isEmpty() )
        return req;
    if ( windows )
    {
        req.program = QStringLiteral( "cmd.exe" );
        req.arguments = QStringList{ QStringLiteral( "/c" ),
                                     QDir( repoRoot ).filePath(
                                       QStringLiteral( "scripts/build_offline_bundle.cmd" ) ) };
    }
    else
    {
        req.program = QStringLiteral( "bash" );
        req.arguments = QStringList{ QDir( repoRoot ).filePath( QStringLiteral( "scripts/build_offline_bundle.sh" ) ) };
    }
    req.arguments += QStringList{
        QStringLiteral( "--build-dir" ),
        buildDir,
        QStringLiteral( "--out" ),
        outDir,
        QStringLiteral( "--max-mb" ),
        QString::number( maxMb ),
    };
    if ( !version.isEmpty() )
    {
        req.arguments.append( QStringLiteral( "--version" ) );
        req.arguments.append( version );
    }
    req.workingDirectory = repoRoot;
    req.timeoutMs = 600000;
    return req;
}

ScriptRunResult buildOfflineBundle( const QString &repoRoot, const QString &buildDir, const QString &outDir,
                                    const QString &version, int maxMb )
{
    const QStringList rejections = validateBundleBuilderParams( buildDir, outDir, version );
    if ( !rejections.isEmpty() )
    {
        // Typed refusal, never a process start: the caller sees the exact
        // parameter classes that were rejected (#1688).
        ScriptRunResult refused;
        refused.started = false;
        refused.error = QStringLiteral( "bundle builder parameters refused: " )
                        + rejections.join( QStringLiteral( "; " ) );
        return refused;
    }
#ifdef Q_OS_WIN
    const ScriptRunRequest req = bundleBuilderRequest( repoRoot, buildDir, outDir, version, maxMb, true );
#else
    const ScriptRunRequest req = bundleBuilderRequest( repoRoot, buildDir, outDir, version, maxMb, false );
#endif
    return runScript( req );
}

QJsonObject PackDriftCheck::toJson() const
{
    return QJsonObject{
        { QStringLiteral( "ran" ), ran },
        { QStringLiteral( "exit_code" ), exitCode },
        { QStringLiteral( "in_sync" ), inSync },
        { QStringLiteral( "drift_files" ), QJsonArray::fromStringList( driftFiles ) },
        { QStringLiteral( "summary" ), summary },
        { QStringLiteral( "output_truncated" ), outputTruncated },
    };
}

PackDriftCheck checkLabPackDrift( const QString &repoRoot, const QString &pythonExe )
{
    PackDriftCheck check;
    const QString script = QDir( repoRoot ).filePath( QStringLiteral( "scripts/gen_lab_packs.py" ) );
    if ( !QFileInfo::exists( script ) )
    {
        check.summary = QStringLiteral( "foundry_script_missing" );
        return check;
    }
    ScriptRunRequest req;
    req.program = pythonExe;
    req.arguments = { script, QStringLiteral( "--check" ) };
    req.workingDirectory = repoRoot;
    req.timeoutMs = 120000;
    const ScriptRunResult sr = runScript( req );
    check.ran = sr.started && !sr.timedOut && !sr.crashed;
    check.exitCode = sr.exitCode;
    if ( !sr.started )
    {
        check.summary = sr.error;
        return check;
    }
    if ( sr.timedOut || sr.crashed )
    {
        check.summary = sr.timedOut ? QStringLiteral( "timeout" ) : sr.error;
        return check;
    }
    check.outputTruncated = sr.stdoutTruncated || sr.stderrTruncated;
    if ( check.outputTruncated )
    {
        // Fail-closed (#1474 review): the transcript is a retained head, so
        // the DRIFT lines parsed out of it would be an arbitrary prefix —
        // "drift detected (3 files)" would under-report a whole classroom.
        // The exit code still decides inSync; the list itself is refused.
        check.inSync = ( sr.exitCode == 0 );
        check.summary = check.inSync ? QStringLiteral( "packs in sync (transcript truncated)" )
                                     : QStringLiteral( "pack drift detected (transcript truncated)" );
        return check;
    }
    const QString text = QString::fromUtf8( sr.stdoutBytes ) + QStringLiteral( "\n" )
                         + QString::fromUtf8( sr.stderrBytes );
    for ( const QString &line : text.split( QLatin1Char( '\n' ), Qt::SkipEmptyParts ) )
    {
        const QString trimmed = line.trimmed();
        if ( trimmed.startsWith( QLatin1String( "DRIFT " ) ) )
            check.driftFiles.append( trimmed.mid( 6 ).trimmed() );
    }
    check.inSync = ( sr.exitCode == 0 );
    check.summary = check.inSync ? QStringLiteral( "packs in sync" )
                                 : QStringLiteral( "pack drift detected (%1 files)" )
                                     .arg( check.driftFiles.size() );
    return check;
}

ScriptRunResult runClassroomBatch( const QString &repoRoot, const QString &cliPath, const QString &labId,
                                   const QString &submissionsDir, const QString &outPrefix, int jobs,
                                   int timeoutSec, const QString &pythonExe )
{
    ScriptRunRequest req;
    req.program = pythonExe;
    req.arguments = {
        QDir( repoRoot ).filePath( QStringLiteral( "scripts/run_classroom_batch.py" ) ),
        QStringLiteral( "--cli" ),
        cliPath,
        QStringLiteral( "--lab" ),
        labId,
        QStringLiteral( "--submissions" ),
        submissionsDir,
        QStringLiteral( "--out" ),
        outPrefix,
        QStringLiteral( "--jobs" ),
        QString::number( jobs ),
        QStringLiteral( "--timeout" ),
        QString::number( timeoutSec ),
    };
    req.workingDirectory = repoRoot;
    req.timeoutMs = std::max( 60000, timeoutSec * 1000 * 2 );
    return runScript( req );
}

} // namespace sicnu::teaching_admin
