#include "script_adapters.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <algorithm>

namespace sicnu::teaching_admin {

QJsonObject ScriptRunResult::toJson() const
{
    return QJsonObject{
        { QStringLiteral( "started" ), started },
        { QStringLiteral( "crashed" ), crashed },
        { QStringLiteral( "exit_code" ), exitCode },
        { QStringLiteral( "timed_out" ), timedOut },
        { QStringLiteral( "stdout_bytes" ), stdoutBytes.size() },
        { QStringLiteral( "stderr_bytes" ), stderrBytes.size() },
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
    if ( !proc.waitForFinished( req.timeoutMs ) )
    {
        proc.kill();
        proc.waitForFinished( 3000 );
        out.timedOut = true;
        out.exitCode = -1;
        out.stdoutBytes = proc.readAllStandardOutput();
        out.stderrBytes = proc.readAllStandardError();
        out.error = QStringLiteral( "timeout" );
        return out;
    }
    // exitCode() is not meaningful for an abnormal exit (Qt docs): a grader
    // that crashed must never be read as a normal exit-0 verdict.
    if ( proc.exitStatus() != QProcess::NormalExit )
    {
        out.crashed = true;
        out.exitCode = -1;
        out.stdoutBytes = proc.readAllStandardOutput();
        out.stderrBytes = proc.readAllStandardError();
        out.error = QStringLiteral( "process crashed abnormally" );
        return out;
    }
    out.exitCode = proc.exitCode();
    out.stdoutBytes = proc.readAllStandardOutput();
    out.stderrBytes = proc.readAllStandardError();
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
        r.summary = QString::fromUtf8( sr.stderrBytes.isEmpty() ? sr.stdoutBytes : sr.stderrBytes ).trimmed();
        return r;
    }
    r.verifiable = true;
    r.ok = ( sr.exitCode == 0 );
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

    // Version pin: the manifest is metadata the teacher pinned at export
    // time. A mismatch is a typed failed verification — the digest authority
    // itself is unchanged (the canonical script).
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

BundleManifestInfo inspectBundleManifest( const QString &bundleDir )
{
    BundleManifestInfo info;
    const QString path = QDir( bundleDir ).filePath( QStringLiteral( "manifest.json" ) );
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
    {
        info.issues.push_back( { QStringLiteral( "manifest_unreadable" ), QStringLiteral( "manifest.json" ),
                                 QStringLiteral( "bundle manifest missing or unreadable" ),
                                 QStringLiteral( "error" ) } );
        return info;
    }
    const QJsonObject manifest = QJsonDocument::fromJson( f.readAll() ).object();
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

ScriptRunRequest bundleBuilderRequest( const QString &repoRoot, const QString &buildDir,
                                       const QString &outDir, const QString &version, int maxMb,
                                       bool windows )
{
    ScriptRunRequest req;
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
