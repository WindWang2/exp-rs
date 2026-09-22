#include "experiment_studio/studio_session.h"

#include <QJsonArray>

namespace sicnu::experiment_studio
{

QJsonObject StudioSessionState::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "study_id" ), studyId );
    o.insert( QStringLiteral( "experiment_id" ), experimentId );
    o.insert( QStringLiteral( "algorithm_id" ), algorithmId );
    o.insert( QStringLiteral( "active_tab" ), activeTab );
    QJsonArray sel;
    for ( const QString &id : selectedPointIds )
        sel.append( id );
    o.insert( QStringLiteral( "selected_point_ids" ), sel );
    o.insert( QStringLiteral( "reference_run_id" ), referenceRunId );
    o.insert( QStringLiteral( "student_run_id" ), studentRunId );
    o.insert( QStringLiteral( "fault_scenario_id" ), faultScenarioId );
    o.insert( QStringLiteral( "last_export_path" ), lastExportPath );
    QJsonObject pol;
    pol.insert( QStringLiteral( "max_points" ), static_cast<double>( policy.maxPoints ) );
    pol.insert( QStringLiteral( "max_in_flight" ), policy.maxInFlight );
    pol.insert( QStringLiteral( "max_disk_bytes" ), static_cast<double>( policy.maxDiskBytes ) );
    pol.insert( QStringLiteral( "max_per_run_deadline_ms" ),
                static_cast<double>( policy.maxPerRunDeadlineMs ) );
    pol.insert( QStringLiteral( "spatial_preview_max_samples" ), policy.spatialPreviewMaxSamples );
    o.insert( QStringLiteral( "policy" ), pol );
    o.insert( QStringLiteral( "designer_draft" ), designerDraft );
    o.insert( QStringLiteral( "last_study_report" ), lastStudyReport );
    return o;
}

StudioSessionState StudioSessionState::fromJson( const QJsonObject &json )
{
    StudioSessionState s;
    s.studyId = json.value( QStringLiteral( "study_id" ) ).toString();
    s.experimentId = json.value( QStringLiteral( "experiment_id" ) ).toString();
    s.algorithmId = json.value( QStringLiteral( "algorithm_id" ) ).toString();
    s.activeTab = json.value( QStringLiteral( "active_tab" ) ).toString();
    const QJsonArray sel = json.value( QStringLiteral( "selected_point_ids" ) ).toArray();
    for ( const QJsonValue &v : sel )
        s.selectedPointIds.append( v.toString() );
    s.referenceRunId = json.value( QStringLiteral( "reference_run_id" ) ).toString();
    s.studentRunId = json.value( QStringLiteral( "student_run_id" ) ).toString();
    s.faultScenarioId = json.value( QStringLiteral( "fault_scenario_id" ) ).toString();
    s.lastExportPath = json.value( QStringLiteral( "last_export_path" ) ).toString();
    const QJsonObject pol = json.value( QStringLiteral( "policy" ) ).toObject();
    if ( !pol.isEmpty() )
    {
        s.policy.maxPoints = static_cast<qint64>( pol.value( QStringLiteral( "max_points" ) ).toDouble( 1000 ) );
        s.policy.maxInFlight = pol.value( QStringLiteral( "max_in_flight" ) ).toInt( 8 );
        s.policy.maxDiskBytes =
            static_cast<qint64>( pol.value( QStringLiteral( "max_disk_bytes" ) ).toDouble( 512LL * 1024 * 1024 ) );
        s.policy.maxPerRunDeadlineMs =
            static_cast<qint64>( pol.value( QStringLiteral( "max_per_run_deadline_ms" ) ).toDouble( 600000 ) );
        s.policy.spatialPreviewMaxSamples =
            pol.value( QStringLiteral( "spatial_preview_max_samples" ) ).toInt( 65536 );
    }
    s.designerDraft = json.value( QStringLiteral( "designer_draft" ) ).toObject();
    s.lastStudyReport = json.value( QStringLiteral( "last_study_report" ) ).toObject();
    return s;
}

} // namespace sicnu::experiment_studio
