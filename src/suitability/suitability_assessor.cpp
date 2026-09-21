#include "suitability_assessor.h"

#include "criteria_grid.h"
#include "criteria_labels.h"
#include "criteria_model.h"
#include "criteria_spatial.h"
#include "criteria_spectral.h"
#include "criteria_temporal.h"
#include "suitability_provider.h"

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

    // Facts: explicit input wins; otherwise the provider is consulted when
    // the caller named a dataset version. A provider failure propagates —
    // a named dataset must never silently continue without its facts.
    std::optional<DatasetFacts> facts = inputs.facts;
    if ( !facts.has_value() && inputs.provider != nullptr
         && !inputs.datasetVersionId.isEmpty() )
    {
        const auto fetched = inputs.provider->datasetFacts( inputs.datasetVersionId,
                                                           FactsLimits{} );
        if ( !fetched.has_value() )
            return sicnu::data::Result<SuitabilityReport>::failure( fetched.diagnostics() );
        facts = fetched.value();
    }

    const bool factsIdentified = facts.has_value() && !facts->datasetVersionId.isEmpty();
    if ( inputs.scenes.isEmpty() && inputs.datasetVersionId.isEmpty() && !factsIdentified
         && !inputs.goal.hasAoi )
    {
        return sicnu::data::Result<SuitabilityReport>::failure( sicnu::data::Diagnostic{
            QStringLiteral( "suitability.empty_subject" ),
            QStringLiteral( "nothing to assess: no scenes, no dataset version, no facts, no AOI" ),
            sicnu::data::DiagnosticSeverity::Error } );
    }

    SuitabilityReport report;
    report.setDatasetVersionId(
        inputs.datasetVersionId.isEmpty() && factsIdentified ? facts->datasetVersionId
                                                             : inputs.datasetVersionId );
    QStringList sceneIds;
    sceneIds.reserve( inputs.scenes.size() );
    for ( const SceneCandidate &scene : inputs.scenes )
        sceneIds.append( scene.id );
    report.setSceneIds( sceneIds );
    report.setGoalDigest( inputs.goal.contentDigest() );

    report.addCriterion( assessSpatialCoverage( *resolved, inputs.scenes ) );
    report.addCriterion( assessResolution( *resolved, inputs.scenes ) );
    report.addCriterion( assessSpectralBands( *resolved, inputs.scenes, facts ) );
    report.addCriterion( assessCloudCover( *resolved, inputs.scenes ) );
    report.addCriterion( assessTemporalCoverage( *resolved, inputs.scenes, facts ) );
    report.addCriterion( assessTemporalDensity( *resolved, inputs.scenes, facts ) );
    report.addCriterion( assessTemporalSeasonality( *resolved, inputs.scenes, facts ) );
    report.addCriterion( assessLabelAvailability( *resolved, facts ) );
    report.addCriterion( assessGridCompatibility( *resolved, inputs.scenes ) );
    report.addCriterion( assessModelCompatibility( *resolved, inputs.scenes, facts ) );
    return sicnu::data::Result<SuitabilityReport>::success( report );
}

} // namespace sicnu::suitability
