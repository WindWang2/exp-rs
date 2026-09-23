#include "grader_cli_adapter.h"
#include "script_adapters.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

namespace sicnu::teaching_admin {

namespace {

constexpr const char *kGradeTranscriptSchema = "sicnu.lab.grade/1";

bool isHexDigest( const QString &s )
{
    if ( s.size() != 64 )
        return false;
    for ( const QChar &c : s )
    {
        if ( !( ( c >= QLatin1Char( '0' ) && c <= QLatin1Char( '9' ) )
                || ( c >= QLatin1Char( 'a' ) && c <= QLatin1Char( 'f' ) )
                || ( c >= QLatin1Char( 'A' ) && c <= QLatin1Char( 'F' ) ) ) )
            return false;
    }
    return true;
}

GraderCliGrade unavailableGrade( const QString &reason, const QString &message = QString() )
{
    GraderCliGrade g;
    g.status = QStringLiteral( "unavailable" );
    g.verdict = QStringLiteral( "unavailable" );
    g.score = -1.0;
    g.unavailableReason = reason;
    g.message = message.isEmpty() ? reason : message;
    return g;
}

/// Cross-checks the CLI exit contract against the transcript verdict; a
/// process that claims pass with exit 1 (or similar) is a broken authority,
/// not a grade.
GraderCliGrade mapTranscript( const GraderCliConfig &cfg, int exitCode, const QJsonObject &doc,
                              const QString &usageText )
{
    GraderCliGrade g;
    g.started = true;
    g.exitCode = exitCode;

    if ( doc.value( QStringLiteral( "schema" ) ).toString() != QLatin1String( kGradeTranscriptSchema ) )
        return unavailableGrade( QStringLiteral( "grader_transcript_invalid" ),
                                 QStringLiteral( "transcript schema is not " ) + QLatin1String( kGradeTranscriptSchema ) );

    const QJsonObject report = doc.value( QStringLiteral( "report" ) ).toObject();
    const QString verdict = report.value( QStringLiteral( "verdict" ) ).toString();
    const QString digest = doc.value( QStringLiteral( "digest" ) ).toString();
    if ( !isHexDigest( digest ) )
        return unavailableGrade( QStringLiteral( "grader_transcript_invalid" ),
                                 QStringLiteral( "transcript digest missing or malformed" ) );
    g.reportDigest = digest;

    const QJsonArray deductions = report.value( QStringLiteral( "deductions" ) ).toArray();
    if ( !deductions.isEmpty() )
        g.topDeduction = deductions.at( 0 ).toObject().value( QStringLiteral( "assertion_id" ) ).toString();
    const QString errorText = report.value( QStringLiteral( "error" ) ).toString();

    // Exit contract: 0 pass · 1 fail · 2 usage · 3 unverifiable.
    switch ( exitCode )
    {
        case 0:
            if ( verdict != QLatin1String( "pass" ) )
                return unavailableGrade( QStringLiteral( "grader_exit_verdict_mismatch" ),
                                         QStringLiteral( "exit 0 but verdict " ) + verdict );
            g.status = QStringLiteral( "pass" );
            g.verdict = verdict;
            g.score = report.value( QStringLiteral( "score" ) ).toDouble( -1.0 );
            g.message = QStringLiteral( "graded by sicnu_geo_rs_cli lab --grade" );
            return g;
        case 1:
            if ( verdict != QLatin1String( "fail" ) )
                return unavailableGrade( QStringLiteral( "grader_exit_verdict_mismatch" ),
                                         QStringLiteral( "exit 1 but verdict " ) + verdict );
            g.status = QStringLiteral( "fail" );
            g.verdict = verdict;
            g.score = report.value( QStringLiteral( "score" ) ).toDouble( -1.0 );
            g.message = g.topDeduction.isEmpty()
                          ? QStringLiteral( "graded by sicnu_geo_rs_cli lab --grade" )
                          : QStringLiteral( "top deduction: " ) + g.topDeduction;
            return g;
        case 2:
            // usage: bad flags, unknown lab, invalid rules, missing artifact.
            g.status = QStringLiteral( "error" );
            g.verdict = QStringLiteral( "error" );
            g.score = -1.0;
            g.message = errorText.isEmpty() ? usageText : errorText;
            if ( g.message.isEmpty() )
                g.message = QStringLiteral( "grader usage failure (exit 2)" );
            return g;
        case 3:
            // unverifiable: artifact exists but cannot be graded — typed, not
            // a zero, not an error row.
            g.status = QStringLiteral( "unavailable" );
            g.verdict = QStringLiteral( "unavailable" );
            g.score = -1.0;
            g.unavailableReason = QStringLiteral( "artifact_unverifiable" );
            g.message = errorText.isEmpty() ? QStringLiteral( "artifact unverifiable (exit 3)" )
                                            : errorText;
            return g;
        default:
            return unavailableGrade( QStringLiteral( "grader_exit_unknown" ),
                                     QStringLiteral( "unexpected grader exit code " ) + QString::number( exitCode ) );
    }
}

} // namespace

QJsonObject GraderCliGrade::toJson() const
{
    QJsonObject o{
        { QStringLiteral( "started" ), started },
        { QStringLiteral( "timed_out" ), timedOut },
        { QStringLiteral( "exit_code" ), exitCode },
        { QStringLiteral( "status" ), status },
        { QStringLiteral( "verdict" ), verdict },
        { QStringLiteral( "score" ), score },
        { QStringLiteral( "report_digest" ), reportDigest },
        { QStringLiteral( "top_deduction" ), topDeduction },
        { QStringLiteral( "message" ), message },
    };
    if ( status == QLatin1String( kTeachingUnavailableVerdict ) )
        o.insert( QStringLiteral( "unavailable_reason" ), unavailableReason );
    return o;
}

QString resolveGraderCli( const QString &explicitPath )
{
    if ( !explicitPath.isEmpty() )
        return explicitPath;
    const QByteArray env = qgetenv( "SICNU_GEO_RS_CLI" );
    if ( !env.isEmpty() && QFileInfo::exists( QString::fromLocal8Bit( env ) ) )
        return QString::fromLocal8Bit( env );
    const QString appDir = QCoreApplication::instance()
                             ? QCoreApplication::applicationDirPath()
                             : QString();
    if ( !appDir.isEmpty() )
    {
#ifdef Q_OS_WIN
        const QString candidate = QDir( appDir ).filePath( QStringLiteral( "sicnu_geo_rs_cli.exe" ) );
#else
        const QString candidate = QDir( appDir ).filePath( QStringLiteral( "sicnu_geo_rs_cli" ) );
#endif
        if ( QFileInfo::exists( candidate ) )
            return candidate;
    }
    return QString();
}

GraderCliGrade gradeViaCli( const GraderCliConfig &cfg, const QString &artifactPath )
{
    const QString cli = resolveGraderCli( cfg.cliPath );
    if ( cli.isEmpty() )
        return unavailableGrade(
            QStringLiteral( "grader_cli_missing" ),
            QStringLiteral( "sicnu_geo_rs_cli not found; set SICNU_GEO_RS_CLI — no score is fabricated" ) );

    if ( cfg.labIdOrRulesPath.isEmpty() )
        return unavailableGrade( QStringLiteral( "grader_lab_missing" ),
                                 QStringLiteral( "no lab id or rules path configured" ) );

    QTemporaryDir transcriptDir;
    if ( !transcriptDir.isValid() )
        return unavailableGrade( QStringLiteral( "transcript_dir_unavailable" ) );
    const QString transcriptPath = QDir( transcriptDir.path() ).filePath( QStringLiteral( "grade.json" ) );

    ScriptRunRequest req;
    req.program = cli;
    req.arguments = {
        QStringLiteral( "lab" ),
        QStringLiteral( "--lab" ),
        cfg.labIdOrRulesPath,
        QStringLiteral( "--grade" ),
        artifactPath,
        QStringLiteral( "--out" ),
        transcriptPath,
    };
    if ( cfg.maxBytes > 0 )
    {
        req.arguments.append( QStringLiteral( "--max-bytes" ) );
        req.arguments.append( QString::number( cfg.maxBytes ) );
    }
    req.timeoutMs = cfg.timeoutMs;

    const ScriptRunResult sr = runScript( req );
    GraderCliGrade g;
    if ( !sr.started )
        return unavailableGrade( QStringLiteral( "grader_start_failed" ), sr.error );
    if ( sr.timedOut )
        return unavailableGrade( QStringLiteral( "grader_timeout" ),
                                 QStringLiteral( "grader exceeded %1 ms" ).arg( cfg.timeoutMs ) );

    // The real CLI writes the transcript to --out and mirrors it on stdout;
    // prefer the file, fall back to stdout.
    QByteArray raw;
    {
        QFile f( transcriptPath );
        if ( f.open( QIODevice::ReadOnly ) )
            raw = f.readAll();
    }
    if ( raw.isEmpty() )
        raw = sr.stdoutBytes;
    const QJsonDocument doc = QJsonDocument::fromJson( raw );
    if ( !doc.isObject() )
        return unavailableGrade( QStringLiteral( "grader_transcript_invalid" ),
                                 QStringLiteral( "no parsable transcript on --out or stdout" ) );

    const QString usageText = QString::fromUtf8( sr.stderrBytes ).trimmed();
    return mapTranscript( cfg, sr.exitCode, doc.object(), usageText );
}

GradeCallable cliGradeCallable( const GraderCliConfig &cfg )
{
    return [cfg]( const SubmissionItem &item ) {
        BatchRowResult row;
        row.studentId = item.studentId;
        row.labId = cfg.labIdOrRulesPath;
        row.artifactPath = item.path;

        const GraderCliGrade g = gradeViaCli( cfg, item.path );
        row.status = g.status;
        row.verdict = g.verdict;
        row.score = g.score;
        row.message = g.message;
        row.graderDigest = g.reportDigest;
        row.topDeduction = g.topDeduction;
        if ( g.status == QLatin1String( kTeachingUnavailableVerdict ) )
        {
            row.unavailableReason = g.unavailableReason;
            if ( row.unavailableReason.isEmpty() )
            {
                // Defensive: an unavailable row without a reason is a bug.
                row.unavailableReason = QStringLiteral( "unspecified" );
                row.message = QStringLiteral( "unavailable without reason (bug)" );
            }
        }
        return row;
    };
}

} // namespace sicnu::teaching_admin
