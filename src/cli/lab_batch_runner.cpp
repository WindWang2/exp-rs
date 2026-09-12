/***************************************************************************
 * src/cli/lab_batch_runner.cpp — D7 batch grading engine (see header)
 ***************************************************************************/
#include "lab_batch_runner.h"

#include "exprs/exit_codes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

#include <algorithm>
#include <exception>
#include <string>

namespace sicnu::cli {

namespace {

constexpr const char *kCsvHeader =
  "student_id,lab_id,score,verdict,top_deduction,artifact_path";
constexpr const char *kUtf8Bom = "\xEF\xBB\xBF";
constexpr const char *kErrorVerdict = "error";

/// One submission = one regular top-level file. Hidden files, directories and
/// the CSV target itself never count (grade_all writes next to submissions).
QStringList discoverSubmissions( const QDir &dir, const QFileInfo &csvFile )
{
    QStringList names =
      dir.entryList( QDir::Files | QDir::Readable | QDir::NoDotAndDotDot, QDir::Name );
    names.erase(
      std::remove_if(
        names.begin(), names.end(),
        [&dir, &csvFile]( const QString &name )
        {
            if ( name.startsWith( QLatin1Char( '.' ) ) )
                return true;
            return dir.absoluteFilePath( name ) == csvFile.absoluteFilePath();
        } ),
      names.end() );
    return names;
}

void appendCsvField( std::string &row, const QString &field )
{
    if ( !row.empty() )
        row.push_back( ',' );
    std::string value = field.toStdString();
    const bool needsQuoting = value.find_first_of( ",\"\n\r" ) != std::string::npos;
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
QString topDeduction( const OutputVerifier::LabGradeResult &result )
{
    const OutputVerifier::LabDeduction *top = nullptr;
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
    row.push_back( '\n' );
    return row;
}

} // namespace

LabBatchSummary LabBatchRunner::run( const QString &submissionsDir, const QString &labId,
                                     const QString &csvPath, const LabGradeFn &grade )
{
    LabBatchSummary summary;

    const QDir dir( submissionsDir );
    if ( !dir.exists() )
    {
        summary.usageError = true;
        return summary;
    }

    const QFileInfo csvFile( csvPath );
    const QStringList submissions = discoverSubmissions( dir, csvFile );
    summary.total = submissions.size();

    QFile csv( csvPath );
    if ( !csv.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        summary.usageError = true;
        summary.total = 0;
        return summary;
    }
    csv.write( kUtf8Bom );
    csv.write( kCsvHeader );
    csv.write( "\n" );
    csv.flush();

    for ( const QString &name : submissions )
    {
        const QString artifactPath = dir.absoluteFilePath( name );
        const QString studentId = QFileInfo( name ).completeBaseName();

        std::string row;
        try
        {
            const auto result = grade( artifactPath );
            if ( result.graded )
            {
                ++summary.graded;
                const QString score =
                  QString::number( result.score, 'f', 1 );
                row = csvRowFor( studentId, labId, score, result.verdict,
                                 topDeduction( result ), artifactPath );
            }
            else
            {
                // Unverifiable artifact: honest empty score, error text as the
                // deduction column. Not counted as isolated — grading worked,
                // the submission simply carries no gradeable raster.
                row = csvRowFor( studentId, labId, QString(), result.verdict,
                                 result.error, artifactPath );
            }
        }
        catch ( const std::exception &e )
        {
            ++summary.isolated;
            row = csvRowFor( studentId, labId, QString(), QLatin1String( kErrorVerdict ),
                             QString::fromUtf8( e.what() ), artifactPath );
        }
        catch ( ... )
        {
            ++summary.isolated;
            row = csvRowFor( studentId, labId, QString(), QLatin1String( kErrorVerdict ),
                             QStringLiteral( "unknown non-exception failure" ),
                             artifactPath );
        }

        csv.write( row.c_str() );
        // Streaming contract: the row is on disk before the next submission
        // is graded (incremental flush — a crash mid-class keeps prior rows).
        csv.flush();
    }

    return summary;
}

int batchExitCodeFor( const LabBatchSummary &summary )
{
    namespace exprs_ns = exprs;
    if ( summary.usageError )
        return exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure );
    if ( summary.isolated > 0 )
        return exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError );
    return exprs_ns::exitCodeValue( exprs_ns::ExitCode::Ok );
}

} // namespace sicnu::cli
