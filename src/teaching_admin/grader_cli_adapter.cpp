#include "grader_cli_adapter.h"
#include "json_util.h"
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

} // namespace

GraderCliGrade gradeFromTranscript( int exitCode, const QJsonObject &doc,
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
    // Highest-weight failed assertion carries the top deduction (ties: first
    // in rules order) — the same definition as the canonical CSV emitter
    // (src/cli/lab_batch_runner.cpp topDeduction), not merely the first entry.
    int topIdx = -1;
    double topWeight = -1.0;
    for ( int i = 0; i < deductions.size(); ++i )
    {
        const QJsonObject d = deductions.at( i ).toObject();
        const double w = d.value( QStringLiteral( "weight" ) ).toDouble( -1.0 );
        if ( topIdx < 0 || w > topWeight )
        {
            topIdx = i;
            topWeight = w;
        }
    }
    if ( topIdx >= 0 )
        g.topDeduction = deductions.at( topIdx ).toObject()
                           .value( QStringLiteral( "assertion_id" ) )
                           .toString();
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

QJsonObject GraderCliGrade::toJson() const
{
    QJsonObject o{
        { QStringLiteral( "exit_code" ), exitCode },
        { QStringLiteral( "message" ), message },
        { QStringLiteral( "report_digest" ), reportDigest },
        { QStringLiteral( "score" ), score },
        { QStringLiteral( "started" ), started },
        { QStringLiteral( "status" ), status },
        { QStringLiteral( "timed_out" ), timedOut },
        { QStringLiteral( "top_deduction" ), topDeduction },
        { QStringLiteral( "verdict" ), verdict },
    };
    if ( status == QLatin1String( kTeachingUnavailableVerdict ) )
        o.insert( QStringLiteral( "unavailable_reason" ), unavailableReason );
    return sortKeys( o );
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
    if ( sr.crashed )
    {
        GraderCliGrade crashed = unavailableGrade( QStringLiteral( "grader_crashed" ), sr.error );
        crashed.started = true; // the process ran; it just died abnormally
        return crashed;
    }

    // The real CLI writes the transcript to --out and mirrors it on stdout;
    // prefer the file, fall back to stdout. The file read is budgeted by the
    // same maxBytes forwarded as --max-bytes: an unbounded readAll() here
    // would let a hostile/buggy grader bypass the #1466 capture caps through
    // the transcript channel — and an oversized transcript is a typed
    // refusal, never a parsed grade.
    QByteArray raw;
    {
        const qint64 budget = cfg.maxBytes > 0 ? cfg.maxBytes : GraderCliConfig{}.maxBytes;
        const BoundedReadResult read = readBounded( transcriptPath, budget );
        if ( read.oversize )
            return unavailableGrade(
              QStringLiteral( "grader_transcript_oversize" ),
              QStringLiteral( "grader transcript exceeds the %1-byte budget" ).arg( budget ) );
        // Unreadable WITH partial bytes = the file existed but changed size
        // between the stat and the read (growth window): digesting a
        // truncated prefix as the document would grade wrong bytes (round-1
        // review). Unreadable with NO bytes (never written / unopenable)
        // falls through to the stdout mirror below — a CLI that only prints
        // the transcript is a legal grader shape.
        if ( read.unreadable && !read.bytes.isEmpty() )
            return unavailableGrade(
              QStringLiteral( "grader_transcript_unreadable" ),
              QStringLiteral( "grader transcript changed size while being read" ) );
        raw = read.bytes;
    }
    if ( raw.isEmpty() )
        raw = sr.stdoutBytes;
    const QJsonDocument doc = QJsonDocument::fromJson( raw );
    if ( !doc.isObject() )
        return unavailableGrade( QStringLiteral( "grader_transcript_invalid" ),
                                 QStringLiteral( "no parsable transcript on --out or stdout" ) );

    return gradeFromTranscript( sr.exitCode, doc.object(),
                                QString::fromUtf8( sr.stderrBytes ).trimmed() );
}

GradeCallable cliGradeCallable( const GraderCliConfig &cfg )
{
    return [cfg]( const SubmissionItem &item ) {
        BatchRowResult row;
        row.studentId = item.studentId;
        // labId left empty: runBatchAssessment backfills it from the batch
        // config, so a rules *path* in the grader config never leaks into
        // the report rows as a lab id.
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

namespace
{

/// Head+tail content fingerprint (full content up to 4 MiB): deterministic,
/// sensitive to any edit of a classroom-sized rules file, and bounded for
/// arbitrarily large binaries. Mirrors batch_assessment's artifact stamp
/// philosophy - a fingerprint, not a dedup.
QString sampledContentSha256( const QString &path )
{
    QFile f( path );
    if ( !f.open( QIODevice::ReadOnly ) )
        return QString();
    constexpr qint64 kFingerprintWindow = 4 * 1024 * 1024;
    QByteArray bytes = f.read( kFingerprintWindow );
    const qint64 size = f.size();
    if ( size > 2 * kFingerprintWindow && f.seek( size - kFingerprintWindow ) )
        bytes += f.read( kFingerprintWindow );
    return sha256Hex( bytes );
}

QString shortFp( const QString &material )
{
    return QStringLiteral( "fp:" ) + sha256Hex( material.toUtf8() ).left( 12 );
}

} // namespace

QString gradingRulesFingerprint( const GraderCliConfig &cfg )
{
    if ( cfg.labIdOrRulesPath.isEmpty() )
        return QStringLiteral( "none" );
    const QFileInfo info( cfg.labIdOrRulesPath );
    if ( info.exists() && info.isFile() )
    {
        const QString content = sampledContentSha256( cfg.labIdOrRulesPath );
        if ( !content.isEmpty() )
            return QStringLiteral( "sha256:" ) + content;
        // Exists but unreadable: an UNSTABLE-looking but distinct marker is
        // still better than the labid form - a rules file we cannot read is
        // not a stable authority either. Size+name keeps it deterministic.
        return QStringLiteral( "unreadable:" ) + QString::number( info.size() );
    }
    return QStringLiteral( "labid:" ) + cfg.labIdOrRulesPath;
}

QString graderCliIdentity( const QString &explicitCliPath )
{
    const QString cli = resolveGraderCli( explicitCliPath );
    if ( cli.isEmpty() )
        return QStringLiteral( "none" );
    const QFileInfo info( cli );
    const QString content = sampledContentSha256( cli );
    // No mtime on purpose: a touch of an identical binary keeps grades; a
    // rebuild changes bytes and therefore the hash. Path is part of the
    // identity so two different grader builds in different directories do
    // not silently share checkpoints.
    return QStringLiteral( "path:" ) + QDir::toNativeSeparators( info.absoluteFilePath() )
           + QStringLiteral( ";size:" ) + QString::number( info.size() )
           + ( content.isEmpty() ? QString() : QStringLiteral( ";sha256:" ) + content );
}

GradingVersionLabels gradingVersionLabels( const QJsonObject &rubricOrRules,
                                           const QJsonObject &labSpec, const QString &labId,
                                           const QString &rulesFingerprint )
{
    GradingVersionLabels labels;
    // Rubric label: explicit authored version, else a short fingerprint of
    // the authored content, else the rules-file fingerprint (the authority
    // the grader actually reads), else an honest "none". Never a constant.
    const QString rubricVersionMember =
      rubricOrRules.value( QStringLiteral( "version" ) ).toString();
    if ( !rubricVersionMember.isEmpty() )
        labels.rubricVersion = rubricVersionMember;
    else if ( !rubricOrRules.isEmpty() )
        labels.rubricVersion = shortFp( QString::fromUtf8( canonicalJsonBytes( rubricOrRules ) ) );
    else if ( !rulesFingerprint.isEmpty() )
        labels.rubricVersion = shortFp( rulesFingerprint );
    else
        labels.rubricVersion = QStringLiteral( "none" );

    const QString labVersionMember = labSpec.value( QStringLiteral( "version" ) ).toString();
    if ( !labVersionMember.isEmpty() )
        labels.labVersion = labVersionMember;
    else if ( !labSpec.isEmpty() )
        labels.labVersion = shortFp( QString::fromUtf8( canonicalJsonBytes( labSpec ) ) );
    else if ( !labId.isEmpty() )
        labels.labVersion = QStringLiteral( "labid:" ) + labId;
    else
        labels.labVersion = QStringLiteral( "none" );
    return labels;
}

} // namespace sicnu::teaching_admin
