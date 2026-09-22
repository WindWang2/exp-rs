#include "experiment_studio/sensitivity_projection.h"

#include <QJsonArray>

namespace sicnu::experiment_studio
{

QJsonObject SensitivityViewModel::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "study_id" ), studyId );
    QJsonArray cArr;
    for ( const auto &c : curves )
    {
        QJsonObject cj;
        cj.insert( QStringLiteral( "parameter_path" ), c.parameterPath );
        cj.insert( QStringLiteral( "metric_name" ), c.metricName );
        cj.insert( QStringLiteral( "trend" ), c.trend );
        QJsonArray pts;
        for ( const auto &p : c.points )
        {
            QJsonObject pj;
            pj.insert( QStringLiteral( "dimension_value" ), p.dimensionValue );
            pj.insert( QStringLiteral( "run_count" ), static_cast<double>( p.runCount ) );
            pj.insert( QStringLiteral( "mean" ), p.mean );
            pj.insert( QStringLiteral( "population_std_dev" ), p.populationStdDev );
            pj.insert( QStringLiteral( "min" ), p.min );
            pj.insert( QStringLiteral( "max" ), p.max );
            pts.append( pj );
        }
        cj.insert( QStringLiteral( "points" ), pts );
        cArr.append( cj );
    }
    o.insert( QStringLiteral( "curves" ), cArr );
    QJsonArray eArr;
    for ( const auto &e : envelopes )
    {
        QJsonObject ej;
        ej.insert( QStringLiteral( "metric_name" ), e.metricName );
        ej.insert( QStringLiteral( "band_count" ), e.bands.size() );
        eArr.append( ej );
    }
    o.insert( QStringLiteral( "envelopes" ), eArr );
    QJsonArray pArr;
    for ( const QString &id : paretoPointIds )
        pArr.append( id );
    o.insert( QStringLiteral( "pareto_point_ids" ), pArr );
    o.insert( QStringLiteral( "declared_best" ), declaredBest );
    o.insert( QStringLiteral( "selected_point_id" ), selectedPointId );
    o.insert( QStringLiteral( "selected_point_detail" ), selectedPointDetail );
    o.insert( QStringLiteral( "spatial_delta_summary" ), spatialDeltaSummary );
    o.insert( QStringLiteral( "uncertainty_missing_replicate" ), uncertaintyMissingReplicate );
    QJsonArray iss;
    for ( const QString &s : issues )
        iss.append( s );
    o.insert( QStringLiteral( "issues" ), iss );
    return o;
}

SensitivityViewModel projectSensitivity( const sicnu::study::StudyReport &report,
                                         const QString &selectedPointId )
{
    SensitivityViewModel vm;
    vm.studyId = report.studyId;
    vm.curves = report.curves;
    vm.envelopes = report.envelopes;
    vm.paretoPointIds = report.paretoPointIds;
    vm.declaredBest = report.declaredBest;
    vm.selectedPointId = selectedPointId;

    // Uncertainty requires seed replicates > 1; empty envelopes with seedReplicates==1
    // or missing replicate metadata → flag for teaching UI.
    bool anyBand = false;
    for ( const auto &e : report.envelopes )
    {
        if ( !e.bands.isEmpty() )
            anyBand = true;
    }
    const QJsonObject sampling = report.specJson.value( QStringLiteral( "budget" ) ).toObject();
    const int seedReplicates = sampling.value( QStringLiteral( "seed_replicates" ) ).toInt( 1 );
    if ( seedReplicates <= 1 || !anyBand )
    {
        vm.uncertaintyMissingReplicate = true;
        vm.issues.append(
            QStringLiteral( "experiment_studio.uncertainty_missing_replicate:"
                            " set budget.seed_replicates >= 2 to obtain bands" ) );
    }

    if ( !selectedPointId.isEmpty() )
    {
        for ( const auto &row : report.runTable )
        {
            if ( row.pointId == selectedPointId )
            {
                vm.selectedPointDetail = row.toJson();
                break;
            }
        }
    }
    if ( !report.spatialSummaries.isEmpty() )
        vm.spatialDeltaSummary = report.spatialSummaries.first().toJson();
    return vm;
}

} // namespace sicnu::experiment_studio
