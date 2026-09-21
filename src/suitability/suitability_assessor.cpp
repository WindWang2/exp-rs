#include "suitability_assessor.h"

#include "criteria_spatial.h"

namespace sicnu::suitability
{

sicnu::data::Result<SuitabilityReport> SuitabilityAssessor::assess( const Inputs &inputs )
{
    const auto resolved = resolveRequirements( inputs.goal );
    if ( !resolved.has_value() )
        return sicnu::data::Result<SuitabilityReport>::failure( resolved.diagnostics() );

    if ( inputs.scenes.size() > kMaxScenes )
    {
        return sicnu::data::Result<SuitabilityReport>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.too_many_scenes" ),
            QStringLiteral( "%1 scenes exceed the assessor cap of %2; pre-filter or sample first" )
                .arg( inputs.scenes.size() )
                .arg( kMaxScenes ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    if ( inputs.scenes.isEmpty() && inputs.datasetVersionId.isEmpty() && !inputs.goal.hasAoi )
    {
        return sicnu::data::Result<SuitabilityReport>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.empty_subject" ),
            QStringLiteral( "nothing to assess: no scenes, no dataset version, no AOI" ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    SuitabilityReport report;
    report.setDatasetVersionId( inputs.datasetVersionId );
    QStringList sceneIds;
    sceneIds.reserve( inputs.scenes.size() );
    for ( const SceneCandidate &scene : inputs.scenes )
        sceneIds.append( scene.id );
    report.setSceneIds( sceneIds );
    report.setGoalDigest( inputs.goal.contentDigest() );

    report.addCriterion( assessSpatialCoverage( *resolved, inputs.scenes ) );
    report.addCriterion( assessResolution( *resolved, inputs.scenes ) );
    return sicnu::data::Result<SuitabilityReport>::success( report );
}

} // namespace sicnu::suitability
