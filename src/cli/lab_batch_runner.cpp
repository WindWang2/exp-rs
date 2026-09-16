/***************************************************************************
 * src/cli/lab_batch_runner.cpp — D7 batch grading engine, v2 (see header)
 ***************************************************************************/
#include "lab_batch_runner.h"

#include "exprs/exit_codes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <exception>
#include <map>
#include <string>

namespace sicnu::cli {

namespace {

constexpr const char *kCsvHeader =
  "student_id,lab_id,score,verdict,top_deduction,artifact_path";
constexpr const char *kUtf8Bom = "\xEF\xBB\xBF";
constexpr const char *kErrorVerdict = "error";

/// One submission = one regular top-level file. Hidden files, directories and
/// the CSV/summary targets never count (grade_all writes next to submissions).
QStringList discoverSubmissions( const QDir &dir, const QStringList &excludedAbsolute )
{
    QStringList names =
      dir.entryList( QDir::Files | QDir::Readable | QDir::NoDotAndDotDot, QDir::Name );
    names.erase(
      std::remove_if(
        names.begin(), names.end(),
        [&dir, &excludedAbsolute]( const QString &name )
        {
            if ( name.startsWith( QLatin1Char( '.' ) ) )
                return true;
            QString absolute = dir.absoluteFilePath( name );
#ifdef Q_OS_WIN
            absolute = absolute.toLower();
#endif
            return excludedAbsolute.contains( absolute );
        } ),
      names.end() );
    return names;
}

void appendCsvField( std::string &row, const QString &field )
{
    if ( !row.empty() )
        row.push_back( ',' );
    std::string value = field.toStdString();
    const bool needsQuoting = value.find_first_of( ",\"\r\n" ) != std::string::npos;
    if ( !needsQuoting )
    {
        row += value;
        return;
    }
    row.push_back( '"' );
    std::string escaped;
    escaped.reserve( value.size() + 8 );
    for ( const char c : value )
    {
        if ( c == '"' )
            escaped.push_back( '"' );
        escaped.push_back( c );
    }
    row += escaped;
    row.push_back( '"' );
}

/// Highest-weight failed assertion carries the CSV's top_deduction column
/// (ties: first in rules order). A pass with no deductions yields "".
QString topDeduction( const sicnu::agent::OutputVerifier::LabGradeResult &result )
{
    const sicnu::agent::OutputVerifier::LabDeduction *top = nullptr;
    for ( const auto &deduction : result.deductions )
        if ( !top || deduction.weight > top->weight )
            top = &deduction;
    return top ? top->assertionId : QString();
}

std::string csvRowFor( const QString &studentId, const QString &labId,
                       const QString &score, const QString &verdict,
                       const QString &topDeduction, const QString &artifactPath )
{
    std::string row;
    appendCsvField( row, studentId );
    appendCsvField( row, labId );
    appendCsvField( row, score );
    appendCsvField( row, verdict );
    appendCsvField( row, topDeduction );
    appendCsvField( row, artifactPath );
    row += "\r\n";
    return row;
}

/// Streaming sha256 + size of one submission (bounded chunks; the artifact is
/// never materialized). Returns an empty digest when the file is unreadable.
QString submissionDigest( const QString &path, qint64 *bytesOut )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return QString();
    QCryptographicHash hash( QCryptographicHash::Sha256 );
    qint64 total = 0;
    char buffer[65536];
    while ( !file.atEnd() )
    {
        const qint64 read = file.read( buffer, sizeof( buffer ) );
        if ( read < 0 )
            return QString();
        hash.addData( buffer, static_cast<int>( read ) );
        total += read;
    }
    *bytesOut = total;
    return QString::fromLatin1( hash.result().toHex() );
}

/// Atomic text write: QSaveFile stages a temp file in the SAME directory and
/// commit() renames it over the target, so a crash never leaves a torn
/// summary next to the graded CSV.
bool atomicWrite( const QString &path, const QByteArray &text )
{
    QSaveFile out( path );
    if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    if ( out.write( text ) != text.size() )
        return false;
    return out.commit();
}

QString htmlEscape( const QString &text )
{
    QString out = text;
    out.replace( QLatin1Char( '&' ), QLatin1String( "&amp;" ) );
    out.replace( QLatin1Char( '<' ), QLatin1String( "&lt;" ) );
    out.replace( QLatin1Char( '>' ), QLatin1String( "&gt;" ) );
    out.replace( QLatin1Char( '"' ), QLatin1String( "&quot;" ) );
    return out;
}

QByteArray summaryHtmlFor( const QString &labId, const QString &submissionsDir,
                           const LabBatchSummary &summary )
{
    QString html;
    html += QStringLiteral( "<!DOCTYPE html>\n<html lang=\"zh\">\n<head>\n" );
    html += QStringLiteral( "<meta charset=\"utf-8\">\n" );
    html += QStringLiteral( "<title>lab batch summary — %1</title>\n" ).arg( htmlEscape( labId ) );
    html += QStringLiteral( "</head>\n<body>\n" );
    html += QStringLiteral( "<h1>lab batch summary — %1</h1>\n" ).arg( htmlEscape( labId ) );
    html += QStringLiteral( "<p>submissions: %1 &middot; graded: %2 &middot; isolated: %3 "
                            "&middot; duplicates: %4</p>\n" )
              .arg( summary.total )
              .arg( summary.graded )
              .arg( summary.isolated )
              .arg( summary.duplicates );
    if ( summary.cancelled )
        html += QStringLiteral( "<p><strong>cancelled</strong> — CSV holds every graded row</p>\n" );
    if ( summary.cappedByMaxSubmissions )
        html += QStringLiteral( "<p><strong>capped</strong> by max-submissions</p>\n" );
    html += QStringLiteral( "<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">\n" );
    html += QStringLiteral(
      "<tr><th>student_id</th><th>score</th><th>verdict</th><th>top_deduction</th>"
      "<th>duplicate_of</th><th>roster</th><th>artifact</th></tr>\n" );
    for ( const LabBatchRow &row : summary.rows )
    {
        const QString score =
          row.score >= 0.0 ? QString::number( row.score, 'f', 1 ) : QString();
        html += QStringLiteral( "<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td>"
                                "<td>%6</td><td>%7</td></tr>\n" )
                  .arg( htmlEscape( row.studentId ),
                        htmlEscape( score ),
                        htmlEscape( row.verdict ),
                        htmlEscape( row.topDeduction ),
                        htmlEscape( row.duplicateOf ),
                        row.rosterMatch ? QStringLiteral( "ok" ) : QStringLiteral( "unknown" ),
                        htmlEscape( row.artifactPath ) );
    }
    html += QStringLiteral( "</table>\n" );
    if ( !summary.missingRosterIds.isEmpty() )
    {
        html += QStringLiteral( "<h2>missing submissions</h2>\n<ul>\n" );
        for ( const QString &id : summary.missingRosterIds )
            html += QStringLiteral( "<li>%1</li>\n" ).arg( htmlEscape( id ) );
        html += QStringLiteral( "</ul>\n" );
    }
    html += QStringLiteral( "<p dir=\"ltr\" style=\"color:#666\">%1</p>\n" )
              .arg( htmlEscape( submissionsDir ) );
    html += QStringLiteral( "</body>\n</html>\n" );
    return html.toUtf8();
}

} // namespace

bool LabRoster::load( const QString &path, LabRoster *roster, QString *error )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        *error = QStringLiteral( "cannot read roster: %1" ).arg( path );
        return false;
    }
    QByteArray raw = file.readAll();
    if ( raw.startsWith( "\xEF\xBB\xBF" ) )
        raw.remove( 0, 3 );

    QMap<QString, QString> entries;
    const QList<QByteArray> lines = raw.split( '\n' );
    for ( int i = 0; i < lines.size(); ++i )
    {
        QByteArray line = lines.at( i );
        while ( line.endsWith( '\r' ) )
            line.chop( 1 );
        if ( line.isEmpty() )
            continue;
        const QString text = QString::fromUtf8( line );
        if ( text.startsWith( QLatin1Char( '#' ) ) )
            continue;
        // The header row (student_id,display_name) is contractual and never
        // a data line; tolerated for hand-written rosters.
        if ( i == 0 && text.startsWith( QLatin1String( "student_id" ) ) )
            continue;
        const int comma = text.indexOf( QLatin1Char( ',' ) );
        if ( comma <= 0 )
        {
            *error = QStringLiteral( "roster line %1 is not student_id,display_name" ).arg( i + 1 );
            return false;
        }
        const QString id = text.left( comma ).trimmed();
        const QString display = text.mid( comma + 1 ).trimmed();
        if ( id.isEmpty() )
        {
            *error = QStringLiteral( "roster line %1 has an empty student_id" ).arg( i + 1 );
            return false;
        }
        if ( entries.contains( id ) )
        {
            *error = QStringLiteral( "roster has duplicate student_id %1" ).arg( id );
            return false;
        }
        entries.insert( id, display );
    }
    if ( entries.isEmpty() )
    {
        *error = QStringLiteral( "roster is empty" );
        return false;
    }
    roster->m_entries = entries;
    return true;
}

bool LabRoster::contains( const QString &studentId ) const
{
    return m_entries.contains( studentId );
}

QStringList LabRoster::ids() const
{
    return m_entries.keys();
}

QString LabRoster::displayName( const QString &studentId ) const
{
    return m_entries.value( studentId );
}

LabBatchSummary LabBatchRunner::run( const QString &submissionsDir, const QString &labId,
                                     const QString &csvPath, const LabGradeFn &grade,
                                     const LabBatchOptions &options )
{
    LabBatchSummary summary;

    LabRoster roster;
    const bool hasRoster = !options.rosterPath.isEmpty();
    if ( hasRoster )
    {
        QString rosterError;
        if ( !LabRoster::load( options.rosterPath, &roster, &rosterError ) )
        {
            summary.usageError = true;
            summary.usageMessage = rosterError;
            return summary;
        }
    }

    const QDir dir( submissionsDir );
    if ( !dir.exists() )
    {
        summary.usageError = true;
        summary.usageMessage = QStringLiteral( "cannot read submissions directory: %1" )
                                 .arg( submissionsDir );
        return summary;
    }

    QStringList excluded;
    excluded << QFileInfo( csvPath ).absoluteFilePath();
    if ( !options.jsonPath.isEmpty() )
        excluded << QFileInfo( options.jsonPath ).absoluteFilePath();
    if ( !options.htmlPath.isEmpty() )
        excluded << QFileInfo( options.htmlPath ).absoluteFilePath();
#ifdef Q_OS_WIN
    // Windows paths are case-insensitive: "Grades.CSV" must still be excluded
    // when the CSV was requested as "grades.csv".
    for ( QString &entry : excluded )
        entry = entry.toLower();
    QStringList lowered;
    lowered.reserve( excluded.size() );
#endif

    QStringList submissions = discoverSubmissions( dir, excluded );
    summary.total = submissions.size();
    if ( options.maxSubmissions >= 0 && submissions.size() > options.maxSubmissions )
    {
        summary.cappedByMaxSubmissions = true;
        submissions = submissions.mid( 0, options.maxSubmissions );
    }

    QFile csv( csvPath );
    if ( !csv.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        summary.usageError = true;
        summary.usageMessage = QStringLiteral( "cannot write CSV: %1" ).arg( csvPath );
        summary.total = 0;
        return summary;
    }
    csv.write( kUtf8Bom );
    csv.write( kCsvHeader );
    csv.write( "\r\n" );
    csv.flush();

    std::map<QString, QString> firstOwnerByDigest; // content digest -> first student id
    for ( const QString &name : submissions )
    {
        if ( options.cancelled && options.cancelled() )
        {
            summary.cancelled = true;
            break;
        }

        const QString artifactPath = dir.absoluteFilePath( name );
        const QString studentId = QFileInfo( name ).completeBaseName();

        LabBatchRow row;
        row.studentId = studentId;
        row.labId = labId;
        row.artifactPath = artifactPath;
        row.sha256 = submissionDigest( artifactPath, &row.bytes );

        // Identity: identical content is graded (still its own artifact) but
        // flagged duplicate_of the first submitter — the teacher decides.
        if ( !row.sha256.isEmpty() )
        {
            const auto owner = firstOwnerByDigest.find( row.sha256 );
            if ( owner != firstOwnerByDigest.end() )
            {
                row.duplicateOf = owner->second;
                ++summary.duplicates;
            }
            else
            {
                firstOwnerByDigest.emplace( row.sha256, studentId );
            }
        }
        if ( hasRoster )
        {
            row.rosterMatch = roster.contains( studentId );
            if ( !row.rosterMatch )
                ++summary.unknownRoster;
        }

        std::string csvRow;
        try
        {
            const auto result = grade( artifactPath );
            if ( result.graded )
            {
                ++summary.graded;
                row.score = result.score;
                row.verdict = result.verdict;
                row.topDeduction = topDeduction( result );
                csvRow = csvRowFor( studentId, labId, QString::number( result.score, 'f', 1 ),
                                    result.verdict, row.topDeduction, artifactPath );
            }
            else
            {
                // Unverifiable artifact: honest empty score, error text as the
                // deduction column. Not counted as isolated — grading worked,
                // the submission simply carries no gradeable raster.
                row.verdict = result.verdict;
                row.topDeduction = result.error;
                csvRow = csvRowFor( studentId, labId, QString(), result.verdict,
                                    result.error, artifactPath );
            }
        }
        catch ( const std::exception &e )
        {
            ++summary.isolated;
            row.verdict = QLatin1String( kErrorVerdict );
            row.topDeduction = QString::fromUtf8( e.what() );
            csvRow = csvRowFor( studentId, labId, QString(), QLatin1String( kErrorVerdict ),
                                QString::fromUtf8( e.what() ), artifactPath );
        }
        catch ( ... )
        {
            ++summary.isolated;
            row.verdict = QLatin1String( kErrorVerdict );
            row.topDeduction = QStringLiteral( "unknown non-exception failure" );
            csvRow = csvRowFor( studentId, labId, QString(), QLatin1String( kErrorVerdict ),
                                QStringLiteral( "unknown non-exception failure" ), artifactPath );
        }

        csv.write( csvRow.c_str() );
        // Streaming contract: the row is on disk before the next submission
        // is graded (incremental flush — a crash mid-class keeps prior rows).
        csv.flush();
        summary.rows.append( row );
    }

    if ( hasRoster )
    {
        QSet<QString> submitted;
        for ( const LabBatchRow &row : summary.rows )
            submitted.insert( row.studentId );
        for ( const QString &id : roster.ids() )
        {
            if ( !submitted.contains( id ) )
            {
                summary.missingRosterIds.append( id );
                ++summary.missingRoster;
            }
        }
        summary.missingRosterIds.sort();
    }

    // Deterministic summaries, written atomically after the run.
    if ( !options.jsonPath.isEmpty() )
    {
        const QJsonObject body = summaryBodyJson( labId, submissionsDir, summary );
        const QJsonDocument doc( body );
        if ( !atomicWrite( options.jsonPath, doc.toJson( QJsonDocument::Indented ) ) )
        {
            summary.usageError = true;
            summary.usageMessage =
              QStringLiteral( "cannot write JSON summary: %1" ).arg( options.jsonPath );
        }
    }
    if ( !options.htmlPath.isEmpty() )
    {
        if ( !atomicWrite( options.htmlPath,
                           summaryHtmlFor( labId, submissionsDir, summary ) ) )
        {
            summary.usageError = true;
            summary.usageMessage =
              QStringLiteral( "cannot write HTML summary: %1" ).arg( options.htmlPath );
        }
    }

    return summary;
}

QJsonObject LabBatchRunner::summaryBodyJson( const QString &labId, const QString &submissionsDir,
                                             const LabBatchSummary &summary )
{
    QJsonObject body;
    body.insert( QStringLiteral( "schema" ), QStringLiteral( "sicnu.lab.batch-summary/1" ) );
    body.insert( QStringLiteral( "lab_id" ), labId );
    body.insert( QStringLiteral( "submissions_dir" ), submissionsDir );
    body.insert( QStringLiteral( "total" ), summary.total );
    body.insert( QStringLiteral( "graded" ), summary.graded );
    body.insert( QStringLiteral( "isolated" ), summary.isolated );
    body.insert( QStringLiteral( "duplicates" ), summary.duplicates );
    body.insert( QStringLiteral( "unknown_roster" ), summary.unknownRoster );
    body.insert( QStringLiteral( "missing_roster" ), summary.missingRoster );
    body.insert( QStringLiteral( "capped_by_max_submissions" ), summary.cappedByMaxSubmissions );
    body.insert( QStringLiteral( "cancelled" ), summary.cancelled );

    QJsonArray rows;
    // student_id order (deterministic regardless of filesystem discovery order)
    QVector<LabBatchRow> sorted = summary.rows;
    std::sort( sorted.begin(), sorted.end(),
               []( const LabBatchRow &a, const LabBatchRow &b )
               { return a.studentId < b.studentId; } );
    for ( const LabBatchRow &row : sorted )
    {
        QJsonObject entry;
        entry.insert( QStringLiteral( "student_id" ), row.studentId );
        if ( row.score >= 0.0 )
            entry.insert( QStringLiteral( "score" ), row.score );
        entry.insert( QStringLiteral( "verdict" ), row.verdict );
        if ( !row.topDeduction.isEmpty() )
            entry.insert( QStringLiteral( "top_deduction" ), row.topDeduction );
        entry.insert( QStringLiteral( "sha256" ), row.sha256 );
        entry.insert( QStringLiteral( "bytes" ), static_cast<qint64>( row.bytes ) );
        if ( !row.duplicateOf.isEmpty() )
            entry.insert( QStringLiteral( "duplicate_of" ), row.duplicateOf );
        if ( !row.rosterMatch )
            entry.insert( QStringLiteral( "roster" ), QStringLiteral( "unknown" ) );
        entry.insert( QStringLiteral( "artifact_path" ), row.artifactPath );
        rows.append( entry );
    }
    body.insert( QStringLiteral( "rows" ), rows );

    if ( !summary.missingRosterIds.isEmpty() )
    {
        QJsonArray missing;
        for ( const QString &id : summary.missingRosterIds )
            missing.append( id );
        body.insert( QStringLiteral( "missing_submissions" ), missing );
    }
    return body;
}

int batchExitCodeFor( const LabBatchSummary &summary )
{
    namespace exprs_ns = exprs;
    if ( summary.usageError )
        return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
    if ( summary.isolated > 0 || summary.cancelled || summary.cappedByMaxSubmissions )
        return exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError );
    return exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok );
}

} // namespace sicnu::cli
