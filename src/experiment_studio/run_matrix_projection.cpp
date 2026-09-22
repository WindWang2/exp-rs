#include "experiment_studio/run_matrix_projection.h"

#include <QJsonArray>

namespace sicnu::experiment_studio
{

QString pointIdentityKey( const sicnu::study::StudyRunRow &row )
{
    return row.pointId + QLatin1Char( '#' ) + QString::number( row.replicateIndex );
}

QJsonObject RunMatrixViewModel::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "study_id" ), studyId );
    o.insert( QStringLiteral( "experiment_id" ), experimentId );
    o.insert( QStringLiteral( "total_points" ), totalPoints );
    o.insert( QStringLiteral( "recorded_count" ), recordedCount );
    o.insert( QStringLiteral( "failed_count" ), failedCount );
    o.insert( QStringLiteral( "cancelled_count" ), cancelledCount );
    o.insert( QStringLiteral( "missing_count" ), missingCount );
    QJsonArray arr;
    for ( const auto &r : rows )
        arr.append( r.toJson() );
    o.insert( QStringLiteral( "rows" ), arr );
    QJsonArray sel;
    for ( const QString &id : selectedPointIds )
        sel.append( id );
    o.insert( QStringLiteral( "selected_point_ids" ), sel );
    QJsonArray iss;
    for ( const QString &s : issues )
        iss.append( s );
    o.insert( QStringLiteral( "issues" ), iss );
    return o;
}

RunMatrixViewModel projectRunMatrix( const sicnu::study::StudyReport &report,
                                     const RunMatrixFilter &filter )
{
    RunMatrixViewModel vm;
    vm.studyId = report.studyId;
    vm.experimentId = report.experimentId;
    vm.recordedCount = report.recordedCount;
    vm.failedCount = report.failedCount;
    vm.cancelledCount = report.cancelledCount;
    vm.missingCount = report.missingCount;
    vm.totalPoints = report.runTable.size();

    for ( const auto &row : report.runTable )
    {
        if ( filter.onlyFailed && row.status != QStringLiteral( "failed" ) )
            continue;
        if ( !filter.statusEquals.isEmpty() && row.status != filter.statusEquals )
            continue;
        if ( !filter.pointIdContains.isEmpty()
             && !row.pointId.contains( filter.pointIdContains, Qt::CaseInsensitive ) )
            continue;
        if ( !filter.runIdContains.isEmpty()
             && !row.runId.contains( filter.runIdContains, Qt::CaseInsensitive ) )
            continue;
        vm.rows.append( row );
    }
    return vm;
}

} // namespace sicnu::experiment_studio
