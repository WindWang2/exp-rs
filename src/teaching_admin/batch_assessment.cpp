#include "batch_assessment.h"
#include "json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cstring>

namespace sicnu::teaching_admin {

QJsonObject BatchRowResult::toJson() const
{
    return sortKeys( QJsonObject{
        { QStringLiteral( "student_id" ), studentId },
        { QStringLiteral( "lab_id" ), labId },
        { QStringLiteral( "status" ), status },
        { QStringLiteral( "score" ), score },
        { QStringLiteral( "verdict" ), verdict },
        { QStringLiteral( "message" ), message },
        { QStringLiteral( "artifact_path" ), artifactPath },
        { QStringLiteral( "rubric_version" ), rubricVersion },
        { QStringLiteral( "lab_version" ), labVersion },
        { QStringLiteral( "software_version" ), softwareVersion },
        { QStringLiteral( "missing_evidence" ), missingEvidence },
        { QStringLiteral( "grader_digest" ), graderDigest },
        { QStringLiteral( "top_deduction" ), topDeduction },
        { QStringLiteral( "unavailable_reason" ), unavailableReason },
    } );
}

QJsonObject BatchAssessmentReport::toJson() const
{
    QJsonArray rowsArr;
    // Deterministic order by student_id
    QVector<BatchRowResult> sorted = rows;
    std::sort( sorted.begin(), sorted.end(),
               []( const BatchRowResult &a, const BatchRowResult &b ) {
                   if ( a.studentId != b.studentId )
                       return a.studentId < b.studentId;
                   return a.artifactPath < b.artifactPath;
               } );
    for ( const auto &row : sorted )
        rowsArr.append( row.toJson() );
    return sortKeys( QJsonObject{
        { QStringLiteral( "schema" ), schema },
        { QStringLiteral( "lab_id" ), labId },
        { QStringLiteral( "total" ), total },
        { QStringLiteral( "graded" ), graded },
        { QStringLiteral( "failed" ), failed },
        { QStringLiteral( "corrupted" ), corrupted },
        { QStringLiteral( "cancelled" ), cancelled },
        { QStringLiteral( "missing_evidence" ), missingEvidence },
        { QStringLiteral( "unavailable" ), unavailable },
        { QStringLiteral( "cancelled_early" ), cancelledEarly },
        { QStringLiteral( "rubric_version" ), rubricVersion },
        { QStringLiteral( "lab_version" ), labVersion },
        { QStringLiteral( "software_version" ), softwareVersion },
        { QStringLiteral( "rows" ), rowsArr },
    } );
}

SubmissionKind classifySubmission( const QString &path )
{
    const QString lower = path.toLower();
    if ( lower.endsWith( QLatin1String( ".capsule.json" ) ) || lower.contains( QLatin1String( "capsule" ) ) )
        return SubmissionKind::Capsule;
    if ( lower.endsWith( QLatin1String( ".labreport.json" ) ) || lower.endsWith( QLatin1String( ".report.json" ) ) )
        return SubmissionKind::LabReport;
    if ( lower.endsWith( QLatin1String( ".zip" ) ) || lower.endsWith( QLatin1String( ".bundle" ) ) )
        return SubmissionKind::Bundle;
    if ( lower.endsWith( QLatin1String( ".tif" ) ) || lower.endsWith( QLatin1String( ".tiff" ) )
         || lower.endsWith( QLatin1String( ".png" ) ) || lower.endsWith( QLatin1String( ".json" ) ) )
        return SubmissionKind::ArtifactFile;
    return SubmissionKind::Unknown;
}

static QString studentIdFromPath( const QString &path, const QString &root )
{
    const QFileInfo fi( path );
    const QDir rootDir( root );
    const QString rel = rootDir.relativeFilePath( path );
    const QStringList parts = rel.split( QLatin1Char( '/' ), Qt::SkipEmptyParts );
    if ( parts.size() >= 2 )
        return parts.first();
    QString base = fi.completeBaseName();
    // strip common suffixes
    for ( const char *suf : { "_submission", "_artifact", "_report" } )
    {
        if ( base.endsWith( QLatin1String( suf ) ) )
            base.chop( static_cast<int>( strlen( suf ) ) );
    }
    return base;
}

QVector<SubmissionItem> discoverSubmissions( const QString &submissionsDir )
{
    QVector<SubmissionItem> items;
    QDir root( submissionsDir );
    if ( !root.exists() )
        return items;

    // One-level student directories
    const auto subdirs = root.entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );
    for ( const QString &sub : subdirs )
    {
        QDir sd( root.filePath( sub ) );
        const auto files = sd.entryInfoList( QDir::Files, QDir::Name );
        for ( const QFileInfo &fi : files )
        {
            const QString name = fi.fileName();
            if ( name.startsWith( QLatin1String( "grades" ) )
                 || name.contains( QLatin1String( "batch" ) )
                 || name.contains( QLatin1String( "summary" ) )
                 || name.contains( QLatin1String( "feedback" ) ) )
                continue;
            SubmissionItem it;
            it.studentId = sub;
            it.path = fi.absoluteFilePath();
            it.kind = classifySubmission( it.path );
            it.bytes = fi.size();
            items.push_back( it );
        }
    }

    // Flat files in root
    const auto flat = root.entryInfoList( QDir::Files, QDir::Name );
    for ( const QFileInfo &fi : flat )
    {
        if ( fi.fileName().startsWith( QLatin1String( "grades" ) ) )
            continue;
        if ( fi.suffix() == QLatin1String( "csv" ) || fi.suffix() == QLatin1String( "json" ) )
        {
            // skip summary outputs sitting beside submissions
            if ( fi.fileName().contains( QLatin1String( "batch" ) )
                 || fi.fileName().contains( QLatin1String( "summary" ) )
                 || fi.fileName().contains( QLatin1String( "grades" ) ) )
                continue;
        }
        SubmissionItem it;
        it.path = fi.absoluteFilePath();
        it.studentId = studentIdFromPath( it.path, submissionsDir );
        it.kind = classifySubmission( it.path );
        it.bytes = fi.size();
        items.push_back( it );
    }
    std::sort( items.begin(), items.end(),
               []( const SubmissionItem &a, const SubmissionItem &b ) {
                   if ( a.studentId != b.studentId )
                       return a.studentId < b.studentId;
                   return a.path < b.path;
               } );
    return items;
}

BatchAssessmentReport runBatchAssessment( const BatchAssessmentConfig &cfg, const GradeCallable &grade,
                                          std::atomic<bool> *cancelFlag )
{
    BatchAssessmentReport report;
    report.labId = cfg.labId;
    report.rubricVersion = cfg.rubricVersion;
    report.labVersion = cfg.labVersion;
    report.softwareVersion = cfg.softwareVersion;

    auto items = discoverSubmissions( cfg.submissionsDir );
    if ( items.size() > cfg.maxSubmissions )
        items.resize( cfg.maxSubmissions );
    report.total = items.size();

    // Bounded concurrency via QtConcurrent::mapped with blocking wait — but we
    // need cancel + isolation. Use a simple serial+optional thread pool loop
    // that catches nothing across callable (callable must be noexcept-ish).
    for ( const auto &item : items )
    {
        if ( cancelFlag && cancelFlag->load() )
        {
            BatchRowResult row;
            row.studentId = item.studentId;
            row.labId = cfg.labId;
            row.status = QStringLiteral( "cancelled" );
            row.verdict = QStringLiteral( "cancelled" );
            row.message = QStringLiteral( "batch cancelled" );
            row.artifactPath = item.path;
            row.rubricVersion = cfg.rubricVersion;
            row.labVersion = cfg.labVersion;
            row.softwareVersion = cfg.softwareVersion;
            report.rows.push_back( row );
            report.cancelled += 1;
            report.cancelledEarly = true;
            continue;
        }

        BatchRowResult row;
        try
        {
            row = grade( item );
        }
        catch ( ... )
        {
            row.studentId = item.studentId;
            row.labId = cfg.labId;
            row.status = QStringLiteral( "error" );
            row.verdict = QStringLiteral( "error" );
            row.message = QStringLiteral( "grader threw; isolated" );
            row.artifactPath = item.path;
        }
        if ( row.studentId.isEmpty() )
            row.studentId = item.studentId;
        if ( row.labId.isEmpty() )
            row.labId = cfg.labId;
        if ( row.rubricVersion.isEmpty() )
            row.rubricVersion = cfg.rubricVersion;
        if ( row.labVersion.isEmpty() )
            row.labVersion = cfg.labVersion;
        if ( row.softwareVersion.isEmpty() )
            row.softwareVersion = cfg.softwareVersion;
        if ( row.artifactPath.isEmpty() )
            row.artifactPath = item.path;

        if ( row.status == QLatin1String( "corrupted" ) )
            report.corrupted += 1;
        else if ( row.status == QLatin1String( "pass" ) || row.status == QLatin1String( "fail" ) )
            report.graded += 1;
        else if ( row.status == QLatin1String( "cancelled" ) )
            report.cancelled += 1;
        else if ( row.status == QLatin1String( "unavailable" ) )
        {
            // grader-side unavailability; counted in the unavailable pass below
        }
        else
            report.failed += 1;

        if ( row.missingEvidence )
            report.missingEvidence += 1;

        // Missing evidence must never look like a silent zero
        if ( row.missingEvidence && row.score == 0.0 && row.verdict == QLatin1String( "fail" ) )
        {
            row.verdict = QStringLiteral( "unavailable" );
            row.status = QStringLiteral( "unavailable" );
            if ( row.message.isEmpty() )
                row.message = QStringLiteral( "missing evidence — not a silent zero" );
        }

        report.rows.push_back( row );
    }

    // Grader-side unavailability (CLI missing/timeout, unverifiable artifact),
    // counted from final rows: a class that could not be graded is not a
    // silent success. Missing-evidence rewrites above keep their own counter.
    for ( const auto &row : report.rows )
    {
        if ( row.status == QLatin1String( "unavailable" ) && !row.missingEvidence )
            report.unavailable += 1;
    }

    return report;
}

bool publishBatchOutputsAtomic( const BatchAssessmentReport &report, const QString &outPrefix )
{
    const QByteArray jsonBytes = QJsonDocument( report.toJson() ).toJson( QJsonDocument::Indented );
    const QString jsonPath = outPrefix + QStringLiteral( ".json" );
    const QString csvPath = outPrefix + QStringLiteral( ".csv" );

    {
        QSaveFile sf( jsonPath );
        if ( !sf.open( QIODevice::WriteOnly ) )
            return false;
        if ( sf.write( jsonBytes ) != jsonBytes.size() )
            return false;
        if ( !sf.commit() )
            return false;
    }

    QString csv;
    csv += QStringLiteral( "\xEF\xBB\xBF" ); // UTF-8 BOM
    csv += QStringLiteral( "student_id,lab_id,score,verdict,status,message,missing_evidence,grader_digest,top_deduction,unavailable_reason,rubric_version,lab_version,software_version\r\n" );
    QVector<BatchRowResult> sorted = report.rows;
    std::sort( sorted.begin(), sorted.end(),
               []( const BatchRowResult &a, const BatchRowResult &b ) {
                   if ( a.studentId != b.studentId )
                       return a.studentId < b.studentId;
                   return a.artifactPath < b.artifactPath;
               } );
    for ( const auto &row : sorted )
    {
        auto esc = []( QString s ) {
            s.replace( QLatin1Char( '"' ), QStringLiteral( "\"\"" ) );
            return QStringLiteral( "\"" ) + s + QStringLiteral( "\"" );
        };
        csv += esc( row.studentId ) + QLatin1Char( ',' );
        csv += esc( row.labId ) + QLatin1Char( ',' );
        csv += ( row.score < 0 ? QString() : QString::number( row.score ) ) + QLatin1Char( ',' );
        csv += esc( row.verdict ) + QLatin1Char( ',' );
        csv += esc( row.status ) + QLatin1Char( ',' );
        csv += esc( row.message ) + QLatin1Char( ',' );
        csv += ( row.missingEvidence ? QStringLiteral( "1" ) : QStringLiteral( "0" ) ) + QLatin1Char( ',' );
        csv += esc( row.graderDigest ) + QLatin1Char( ',' );
        csv += esc( row.topDeduction ) + QLatin1Char( ',' );
        csv += esc( row.unavailableReason ) + QLatin1Char( ',' );
        csv += esc( row.rubricVersion ) + QLatin1Char( ',' );
        csv += esc( row.labVersion ) + QLatin1Char( ',' );
        csv += esc( row.softwareVersion ) + QStringLiteral( "\r\n" );
    }

    {
        QSaveFile sf( csvPath );
        if ( !sf.open( QIODevice::WriteOnly ) )
            return false;
        const QByteArray bytes = csv.toUtf8();
        if ( sf.write( bytes ) != bytes.size() )
            return false;
        if ( !sf.commit() )
            return false;
    }
    return true;
}

QJsonObject regradeTraceability( const BatchAssessmentReport &report )
{
    return QJsonObject{
        { QStringLiteral( "lab_id" ), report.labId },
        { QStringLiteral( "rubric_version" ), report.rubricVersion },
        { QStringLiteral( "lab_version" ), report.labVersion },
        { QStringLiteral( "software_version" ), report.softwareVersion },
        { QStringLiteral( "report_digest" ), sha256Hex( canonicalJsonBytes( report.toJson() ) ) },
        { QStringLiteral( "row_count" ), report.rows.size() },
    };
}

} // namespace sicnu::teaching_admin
