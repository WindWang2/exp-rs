#include "class_summary.h"
#include "json_util.h"

#include <QJsonArray>
#include <QMap>

namespace sicnu::teaching_admin {

QJsonObject buildClassSummary( const BatchAssessmentReport &report )
{
    int completed = 0;
    QMap<QString, int> statusCounts;
    QMap<QString, int> errorBuckets;
    QMap<QString, int> missingEvidenceDist;
    QJsonArray scoreHist; // coarse buckets 0-10 ... 90-100
    QVector<int> buckets( 10, 0 );

    for ( const auto &row : report.rows )
    {
        statusCounts[row.status] += 1;
        if ( row.status == QLatin1String( "pass" ) || row.status == QLatin1String( "fail" ) )
            completed += 1;
        if ( row.status == QLatin1String( "error" ) || row.status == QLatin1String( "corrupted" )
             || row.status == QLatin1String( "crash" ) || row.status == QLatin1String( "timeout" ) )
            errorBuckets[row.status] += 1;
        if ( row.missingEvidence )
            missingEvidenceDist[QStringLiteral( "missing" )] += 1;
        else
            missingEvidenceDist[QStringLiteral( "present" )] += 1;

        if ( row.score >= 0.0 )
        {
            int b = static_cast<int>( row.score / 10.0 );
            if ( b < 0 )
                b = 0;
            if ( b > 9 )
                b = 9;
            buckets[b] += 1;
        }
    }
    for ( int i = 0; i < 10; ++i )
    {
        scoreHist.append( QJsonObject{
            { QStringLiteral( "bucket" ), QStringLiteral( "%1-%2" ).arg( i * 10 ).arg( i * 10 + 10 ) },
            { QStringLiteral( "count" ), buckets[i] },
        } );
    }

    QJsonObject statuses;
    for ( auto it = statusCounts.begin(); it != statusCounts.end(); ++it )
        statuses.insert( it.key(), it.value() );
    QJsonObject errors;
    for ( auto it = errorBuckets.begin(); it != errorBuckets.end(); ++it )
        errors.insert( it.key(), it.value() );
    QJsonObject missing;
    for ( auto it = missingEvidenceDist.begin(); it != missingEvidenceDist.end(); ++it )
        missing.insert( it.key(), it.value() );

    return sortKeys( QJsonObject{
        { QStringLiteral( "schema" ), QString::fromLatin1( kClassSummarySchema ) },
        { QStringLiteral( "lab_id" ), report.labId },
        { QStringLiteral( "total" ), report.total },
        { QStringLiteral( "completion_count" ), completed },
        { QStringLiteral( "completion_rate" ),
          report.total > 0 ? double( completed ) / double( report.total ) : 0.0 },
        { QStringLiteral( "status_counts" ), statuses },
        { QStringLiteral( "common_errors" ), errors },
        { QStringLiteral( "rubric_score_distribution" ), scoreHist },
        { QStringLiteral( "missing_evidence_distribution" ), missing },
        { QStringLiteral( "versions" ),
          QJsonObject{
              { QStringLiteral( "rubric" ), report.rubricVersion },
              { QStringLiteral( "lab" ), report.labVersion },
              { QStringLiteral( "software" ), report.softwareVersion },
          } },
        { QStringLiteral( "privacy_note" ),
          QStringLiteral( "No cloud profiles / politics / privacy analytics; local class stats only." ) },
    } );
}

} // namespace sicnu::teaching_admin
