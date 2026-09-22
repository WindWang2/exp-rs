#include "script_adapters.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <algorithm>

namespace sicnu::teaching_admin {

QJsonObject ScriptRunResult::toJson() const
{
    return QJsonObject{
        { QStringLiteral( "started" ), started },
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
        { QStringLiteral( "findings" ), findingsArr },
    };
}

BundleVerifyResult verifyOfflineBundle( const QString &repoRoot, const QString &bundleDir,
                                        const QString &pythonExe )
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
    if ( !sr.started || sr.timedOut )
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
    return r;
}

ScriptRunResult buildOfflineBundle( const QString &repoRoot, const QString &buildDir, const QString &outDir,
                                    const QString &version, int maxMb )
{
    ScriptRunRequest req;
    req.program = QStringLiteral( "bash" );
    req.arguments = {
        QDir( repoRoot ).filePath( QStringLiteral( "scripts/build_offline_bundle.sh" ) ),
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
    return runScript( req );
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
