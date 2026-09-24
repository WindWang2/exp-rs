// test_suitability_labels.cpp — Slice E: label availability, grid
// compatibility and model compatibility criteria plus the facts provider
// wiring.
//
// RED-first contract for the labels/grid/model slice:
//   - a declared label requirement with zero label evidence is Unsuitable
//     (the requirement is explicit, so its absence is a verdict, not a
//     shrug); unknown volumes stay Unknown and never pose as zero;
//   - grid judgment is sampled delegation to sicnu::data::compareGrids —
//     this criterion never re-derives grid semantics; scenes without grid
//     snapshots stay Unknown (a possible grid problem must not pass
//     unexamined);
//   - model fit grades the resolved model requirements against the fused
//     scene ∪ facts evidence pool, with honest Unknown when neither side
//     carries evidence;
//   - the assessor consults an injected provider only when no explicit
//     facts were given, and a provider failure is typed upward — it is
//     never silently dropped to continue without facts.

#include <catch2/catch_test_macros.hpp>

#include "suitability/criteria_grid.h"
#include "suitability/criteria_labels.h"
#include "suitability/criteria_model.h"
#include "suitability/dataset_facts.h"
#include "suitability/scene_candidate.h"
#include "suitability/suitability_assessor.h"
#include "suitability/suitability_goal.h"
#include "suitability/suitability_provider.h"
#include "suitability/suitability_types.h"

#include "dataset/dataset_types.h"

#include "data/data_asset.h"
#include "data/raster_grid_compat.h"

#include <QJsonObject>

#include <cmath>

using sicnu::data::AssetState;
using sicnu::suitability::DatasetFacts;
using sicnu::suitability::InMemoryDataProvider;
using sicnu::suitability::ResolvedRequirements;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::SuitabilityAssessor;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGap;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::SuitabilityReport;
using sicnu::suitability::assessGridCompatibility;
using sicnu::suitability::assessLabelAvailability;
using sicnu::suitability::assessModelCompatibility;
using sicnu::suitability::resolveRequirements;

namespace
{

SceneCandidate makeScene( const QString &id )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = AssetState::Ready;
    return scene;
}

ResolvedRequirements emptyRequirements()
{
    // temporal_prediction carries no label/volume/coverage defaults, so the
    // criterion-level fixtures here stay profile-independent.
    SuitabilityGoal goal;
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );
    return resolved.value();
}

ResolvedRequirements labelRequirements( qint64 minSamples = 200 )
{
    SuitabilityGoal goal;
    goal.requireLabels = true;
    goal.minSamples = minSamples;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );
    return resolved.value();
}

DatasetFacts identifiedFacts()
{
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-1" );
    return facts;
}

sicnu::data::RasterGrid makeGrid( double pixelSize = 10.0, double originX = 500000.0 )
{
    sicnu::data::RasterGrid grid;
    grid.crsWkt = QStringLiteral( "EPSG:32650" );
    grid.hasGeoTransform = true;
    grid.geoTransform = { originX, pixelSize, 0.0, 4000000.0, 0.0, -pixelSize };
    grid.width = 100;
    grid.height = 100;
    return grid;
}

const SuitabilityCriterion *findCriterion( const SuitabilityReport &report, const QString &id )
{
    for ( const SuitabilityCriterion &criterion : report.criteria() )
        if ( criterion.id == id )
            return &criterion;
    return nullptr;
}

bool hasGap( const SuitabilityCriterion &criterion, const QString &id )
{
    for ( const SuitabilityGap &gap : criterion.gaps )
        if ( gap.id == id )
            return true;
    return false;
}

} // namespace

TEST_CASE( "label availability without a label requirement is not applicable",
           "[suitability][labels]" )
{
    const ResolvedRequirements req = emptyRequirements();
    const SuitabilityCriterion criterion = assessLabelAvailability( req, std::nullopt );
    REQUIRE( criterion.id == QStringLiteral( "labels.availability" ) );
    REQUIRE( !criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "label availability without facts stays unknown", "[suitability][labels]" )
{
    const ResolvedRequirements req = labelRequirements();
    const SuitabilityCriterion criterion = assessLabelAvailability( req, std::nullopt );
    REQUIRE( criterion.applicable );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.notes.join( QLatin1String( " " ) )
                 .contains( QStringLiteral( "no dataset facts provided" ) ) );
    REQUIRE( criterion.gaps.isEmpty() );
}

TEST_CASE( "a label requirement with no label evidence at all is unsuitable",
           "[suitability][labels]" )
{
    const ResolvedRequirements req = labelRequirements();
    DatasetFacts facts = identifiedFacts();
    const SuitabilityCriterion criterion = assessLabelAvailability( req, facts );
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( criterion.gaps.size() == 1 );
    REQUIRE( hasGap( criterion, QStringLiteral( "labels.schema_missing" ) ) );
}

TEST_CASE( "missing required classes yield one gap per class", "[suitability][labels]" )
{
    const ResolvedRequirements req = labelRequirements();
    SuitabilityGoal withClasses;
    withClasses.requireLabels = true;
    withClasses.requiredClasses = { QStringLiteral( "forest" ), QStringLiteral( "water" ),
                                    QStringLiteral( "urban" ) };
    const auto resolved = resolveRequirements( withClasses );
    REQUIRE( resolved.has_value() );

    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    facts.labelClasses = { QStringLiteral( "forest" ), QStringLiteral( "water" ) };

    const SuitabilityCriterion criterion = assessLabelAvailability( *resolved, facts );
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( criterion.gaps.size() == 1 );
    REQUIRE( hasGap( criterion, QStringLiteral( "label.class_missing.urban" ) ) );
    REQUIRE( !hasGap( criterion, QStringLiteral( "label.class_missing.forest" ) ) );
}

TEST_CASE( "sample volume grades against minSamples with unknown kept unknown",
           "[suitability][labels]" )
{
    const ResolvedRequirements req = labelRequirements( 200 );

    // Unknown volume never poses as zero: with schema evidence present the
    // unmeasurable volume keeps the criterion Unknown.
    DatasetFacts unknownVolume = identifiedFacts();
    unknownVolume.hasLabelSchema = true;
    unknownVolume.sampleCount = -1;
    const SuitabilityCriterion unknown = assessLabelAvailability( req, unknownVolume );
    REQUIRE( unknown.level == SuitabilityLevel::Unknown );
    REQUIRE( unknown.evidence.value( QStringLiteral( "sample_count" ) ).toInteger() == -1 );
    REQUIRE( unknown.gaps.isEmpty() );

    // Below the minimum -> Unsuitable with the measured gap.
    DatasetFacts thin = unknownVolume;
    thin.sampleCount = 199;
    const SuitabilityCriterion below = assessLabelAvailability( req, thin );
    REQUIRE( below.level == SuitabilityLevel::Unsuitable );
    REQUIRE( hasGap( below, QStringLiteral( "samples.below_minimum" ) ) );
    REQUIRE( below.gaps.size() == 1 );

    // Exactly at the minimum satisfies the requirement (boundary inclusive).
    DatasetFacts exact = thin;
    exact.sampleCount = 200;
    const SuitabilityCriterion atMinimum = assessLabelAvailability( req, exact );
    REQUIRE( atMinimum.level == SuitabilityLevel::Suitable );
    REQUIRE( atMinimum.gaps.isEmpty() );

    // Without a minimum there is nothing to grade and a known count suffices
    // (direct requirement: a goal would inherit the classification profile's
    // 200-sample default).
    ResolvedRequirements noMinimum;
    noMinimum.requireLabels = true;
    DatasetFacts counted = exact;
    counted.sampleCount = 199;
    const SuitabilityCriterion unconstrained = assessLabelAvailability( noMinimum, counted );
    REQUIRE( unconstrained.level == SuitabilityLevel::Suitable );
}

TEST_CASE( "pseudo labels against a forbidding policy grade marginal",
           "[suitability][labels]" )
{
    ResolvedRequirements req = labelRequirements();
    req.pseudoLabelsAllowed = false;

    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    facts.sampleCount = 500;
    facts.pseudoLabelCount = 12;

    const SuitabilityCriterion criterion = assessLabelAvailability( req, facts );
    REQUIRE( criterion.level == SuitabilityLevel::Marginal );
    REQUIRE( hasGap( criterion, QStringLiteral( "labels.pseudo_present" ) ) );
    REQUIRE( criterion.gaps.first().evidence.value( QStringLiteral( "pseudo_label_count" ) )
                 .toInteger() == 12 );

    // The same evidence under a permissive policy is simply suitable.
    req.pseudoLabelsAllowed = true;
    const SuitabilityCriterion permissive = assessLabelAvailability( req, facts );
    REQUIRE( permissive.level == SuitabilityLevel::Suitable );
}

TEST_CASE( "hard gaps dominate the pseudo-label downgrade", "[suitability][labels]" )
{
    ResolvedRequirements req = labelRequirements( 500 );
    req.pseudoLabelsAllowed = false;

    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    facts.pseudoLabelCount = 3;
    facts.sampleCount = 10;

    const SuitabilityCriterion criterion = assessLabelAvailability( req, facts );
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( hasGap( criterion, QStringLiteral( "samples.below_minimum" ) ) );
    REQUIRE( hasGap( criterion, QStringLiteral( "labels.pseudo_present" ) ) );
}

TEST_CASE( "grid compatibility without a comparable pair is not applicable",
           "[suitability][grid]" )
{
    const ResolvedRequirements req = emptyRequirements();

    // No scenes at all.
    const SuitabilityCriterion empty = assessGridCompatibility( req, {} );
    REQUIRE( empty.id == QStringLiteral( "grid.compatibility" ) );
    REQUIRE( !empty.applicable );
    REQUIRE( empty.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );

    // A single usable scene has no pair to combine.
    QVector<SceneCandidate> single;
    single.append( makeScene( QStringLiteral( "only" ) ) );
    const SuitabilityCriterion one = assessGridCompatibility( req, single );
    REQUIRE( !one.applicable );
    REQUIRE( one.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "two usable scenes without grid snapshots stay unknown", "[suitability][grid]" )
{
    const ResolvedRequirements req = emptyRequirements();
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "a" ) ) );
    scenes.append( makeScene( QStringLiteral( "b" ) ) );

    const SuitabilityCriterion criterion = assessGridCompatibility( req, scenes );
    REQUIRE( criterion.applicable );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.notes.join( QLatin1String( " " ) )
                 .contains( QStringLiteral( "scenes lack grid snapshots" ) ) );
    REQUIRE( criterion.gaps.isEmpty() );
}

TEST_CASE( "aligned grids grade suitable and count their pairs", "[suitability][grid]" )
{
    const ResolvedRequirements req = emptyRequirements();
    SceneCandidate a = makeScene( QStringLiteral( "a" ) );
    a.grid = makeGrid();
    SceneCandidate b = makeScene( QStringLiteral( "b" ) );
    b.grid = makeGrid();
    QVector<SceneCandidate> scenes{ a, b };

    const SuitabilityCriterion criterion = assessGridCompatibility( req, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Suitable );
    REQUIRE( criterion.gaps.isEmpty() );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_checked" ) ).toInteger() == 1 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_total" ) ).toInteger() == 1 );
}

TEST_CASE( "non-blocking warnings do not fail the grid criterion", "[suitability][grid]" )
{
    const ResolvedRequirements req = emptyRequirements();
    SceneCandidate a = makeScene( QStringLiteral( "a" ) );
    sicnu::data::RasterGrid gridA = makeGrid();
    gridA.bandNoData = { 0.0 };
    a.grid = gridA;
    SceneCandidate b = makeScene( QStringLiteral( "b" ) );
    sicnu::data::RasterGrid gridB = makeGrid();
    gridB.bandNoData = { 1.0 };
    b.grid = gridB;
    QVector<SceneCandidate> scenes{ a, b };

    const SuitabilityCriterion criterion = assessGridCompatibility( req, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Suitable );
    REQUIRE( criterion.gaps.isEmpty() );
    REQUIRE( criterion.evidence.value( QStringLiteral( "verdict_counts" ) )
                 .toObject()
                 .value( QStringLiteral( "grid.nodata_mismatch" ) )
                 .toInt() == 1 );
}

TEST_CASE( "blocking mismatches grade by profile strictness", "[suitability][grid]" )
{
    SceneCandidate a = makeScene( QStringLiteral( "a" ) );
    a.grid = makeGrid( 10.0 );
    SceneCandidate b = makeScene( QStringLiteral( "b" ) );
    b.grid = makeGrid( 20.0 );
    QVector<SceneCandidate> scenes{ a, b };

    ResolvedRequirements lenient = emptyRequirements();
    lenient.gridStrict = false;
    const SuitabilityCriterion marginal = assessGridCompatibility( lenient, scenes );
    REQUIRE( marginal.level == SuitabilityLevel::Marginal );
    REQUIRE( marginal.gaps.size() == 1 );
    REQUIRE( marginal.gaps.first().id ==
             QStringLiteral( "grid.blocking_mismatch.grid.pixel_size_mismatch" ) );
    REQUIRE( marginal.gaps.first().criterionId == QStringLiteral( "grid.compatibility" ) );
    REQUIRE( marginal.evidence.value( QStringLiteral( "blocking_pair_count" ) ).toInteger() == 1 );

    ResolvedRequirements strict = lenient;
    strict.gridStrict = true;
    const SuitabilityCriterion refused = assessGridCompatibility( strict, scenes );
    REQUIRE( refused.level == SuitabilityLevel::Unsuitable );
    REQUIRE( refused.gaps.size() == 1 );
}

TEST_CASE( "pair counts above the cap are equal-stride sampled and stated",
           "[suitability][grid]" )
{
    const ResolvedRequirements req = emptyRequirements();
    QVector<SceneCandidate> scenes;
    for ( int i = 0; i < 21; ++i )
    {
        SceneCandidate scene = makeScene( QStringLiteral( "s%1" ).arg( i ) );
        scene.grid = makeGrid( 10.0, 500000.0 + 1000.0 * i );
        scenes.append( scene );
    }

    // 21 scenes -> 210 pairs; the cap is 200 sampled pairs.
    const SuitabilityCriterion criterion = assessGridCompatibility( req, scenes );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_total" ) ).toInteger() == 210 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_checked" ) ).toInteger() == 200 );
    REQUIRE( criterion.notes.join( QLatin1String( " " ) )
                 .contains( QStringLiteral( "sampled 200 of 210 pairs" ) ) );
}

TEST_CASE( "abnormal grid input stays unknown with a diagnostic", "[suitability][grid]" )
{
    const ResolvedRequirements req = emptyRequirements();
    SceneCandidate a = makeScene( QStringLiteral( "a" ) );
    a.grid = makeGrid();
    SceneCandidate broken = makeScene( QStringLiteral( "broken" ) );
    sicnu::data::RasterGrid nanGrid = makeGrid();
    nanGrid.geoTransform[1] = std::nan( "" );
    broken.grid = nanGrid;
    QVector<SceneCandidate> scenes{ a, broken };

    const SuitabilityCriterion criterion = assessGridCompatibility( req, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( !criterion.diagnostics.isEmpty() );
    REQUIRE( criterion.diagnostics.first().code == QStringLiteral( "suitability.grid_abnormal_input" ) );
}

TEST_CASE( "model compatibility without a model is not applicable", "[suitability][model]" )
{
    const ResolvedRequirements req = emptyRequirements();
    const SuitabilityCriterion criterion =
        assessModelCompatibility( req, {}, std::nullopt );
    REQUIRE( criterion.id == QStringLiteral( "model.compatibility" ) );
    REQUIRE( !criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "model compatibility without any data-side evidence stays unknown",
           "[suitability][model]" )
{
    SuitabilityGoal goal;
    goal.hasModel = true;
    goal.modelModality = QStringLiteral( "optical" );
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    const SuitabilityCriterion criterion = assessModelCompatibility( *resolved, {}, std::nullopt );
    REQUIRE( criterion.applicable );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.gaps.isEmpty() );
}

TEST_CASE( "missing model band roles yield one gap per role", "[suitability][model]" )
{
    SuitabilityGoal goal;
    goal.hasModel = true;
    goal.modelRequiredBandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    SceneCandidate scene = makeScene( QStringLiteral( "rgb" ) );
    scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "green" ),
                        QStringLiteral( "blue" ) };
    const SuitabilityCriterion criterion = assessModelCompatibility( *resolved, { scene }, std::nullopt );
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( criterion.gaps.size() == 1 );
    REQUIRE( hasGap( criterion, QStringLiteral( "model.band_missing.nir" ) ) );

    // Facts can close the band gap too (fused pool).
    DatasetFacts facts = identifiedFacts();
    facts.bandRoles = { QStringLiteral( "nir" ) };
    const SuitabilityCriterion fused =
        assessModelCompatibility( *resolved, { scene }, facts );
    REQUIRE( fused.level == SuitabilityLevel::Suitable );
}

TEST_CASE( "model GSD range grades empty intersection and partial coverage",
           "[suitability][model]" )
{
    SuitabilityGoal goal;
    goal.hasModel = true;
    goal.modelMinGsdM = 10.0;
    goal.modelMaxGsdM = 20.0;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    // Every scene outside the model range -> empty intersection -> Unsuitable.
    SceneCandidate coarse = makeScene( QStringLiteral( "coarse" ) );
    coarse.gsdM = 100.0;
    const SuitabilityCriterion disjoint =
        assessModelCompatibility( *resolved, { coarse }, std::nullopt );
    REQUIRE( disjoint.level == SuitabilityLevel::Unsuitable );
    REQUIRE( hasGap( disjoint, QStringLiteral( "model.resolution_out_of_range" ) ) );

    // One scene inside, one outside -> partial -> Marginal.
    SceneCandidate fine = makeScene( QStringLiteral( "fine" ) );
    fine.gsdM = 15.0;
    const SuitabilityCriterion partial =
        assessModelCompatibility( *resolved, { fine, coarse }, std::nullopt );
    REQUIRE( partial.level == SuitabilityLevel::Marginal );
    REQUIRE( partial.gaps.isEmpty() );

    // All scenes inside -> Suitable.
    SceneCandidate fineToo = makeScene( QStringLiteral( "fine-too" ) );
    fineToo.gsdM = 20.0; // boundary inclusive
    const SuitabilityCriterion covered =
        assessModelCompatibility( *resolved, { fine, fineToo }, std::nullopt );
    REQUIRE( covered.level == SuitabilityLevel::Suitable );
}

TEST_CASE( "modality mismatch between model and every data source is unsuitable",
           "[suitability][model]" )
{
    SuitabilityGoal goal;
    goal.hasModel = true;
    goal.modelModality = QStringLiteral( "sar" );
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    SceneCandidate scene = makeScene( QStringLiteral( "optical" ) );
    scene.modality = QStringLiteral( "optical" );
    const SuitabilityCriterion criterion =
        assessModelCompatibility( *resolved, { scene }, std::nullopt );
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( hasGap( criterion, QStringLiteral( "model.modality_mismatch" ) ) );

    // A matching fact modality repairs the pool.
    DatasetFacts facts = identifiedFacts();
    facts.modality = QStringLiteral( "SAR" ); // normalization is case-insensitive
    const SuitabilityCriterion repaired =
        assessModelCompatibility( *resolved, { scene }, facts );
    REQUIRE( repaired.level == SuitabilityLevel::Suitable );
}

TEST_CASE( "model pins a modality but no source reports one stays unknown",
           "[suitability][model]" )
{
    // Band evidence exists, so the all-evidence-absent gate does not fire —
    // but with an empty modality pool the pinned modality cannot be checked.
    // Mirroring the GSD handling: unknown, never a silent pass.
    SuitabilityGoal goal;
    goal.hasModel = true;
    goal.modelModality = QStringLiteral( "sar" );
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    SceneCandidate scene = makeScene( QStringLiteral( "roles-only" ) );
    scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    scene.modality.clear();
    const SuitabilityCriterion criterion =
        assessModelCompatibility( *resolved, { scene }, std::nullopt );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.gaps.isEmpty() );
    REQUIRE( criterion.evidence.value( QStringLiteral( "reason" ) ).toString()
             == QStringLiteral( "modality_evidence_absent" ) );
}

TEST_CASE( "in-memory provider hands out held facts or an injected failure",
           "[suitability][labels][provider]" )
{
    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    InMemoryDataProvider provider( facts );

    const auto ok = provider.datasetFacts( QStringLiteral( "dv-1" ), {} );
    REQUIRE( ok.has_value() );
    REQUIRE( ok->hasLabelSchema );

    InMemoryDataProvider failing( sicnu::data::Diagnostic{
        QStringLiteral( "suitability.provider_unavailable" ),
        QStringLiteral( "injected failure" ),
        sicnu::data::DiagnosticSeverity::Error } );
    const auto refused = failing.datasetFacts( QStringLiteral( "dv-1" ), {} );
    REQUIRE( !refused.has_value() );
    REQUIRE( refused.diagnostics().first().code ==
             QStringLiteral( "suitability.provider_unavailable" ) );
}

TEST_CASE( "assessor consults the provider when facts are absent", "[suitability][labels]" )
{
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-42" );
    facts.hasLabelSchema = true;
    facts.sampleCount = 1000;
    InMemoryDataProvider provider( facts );

    SuitabilityGoal goal;
    goal.requireLabels = true;
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.datasetVersionId = QStringLiteral( "dv-42" );
    inputs.provider = &provider;

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    // Slice H added the uncertainty roll-up criterion: 10 -> 11 criteria.
    REQUIRE( result->criteria().size() == 11 );
    const SuitabilityCriterion *labels = findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    REQUIRE( labels->level == SuitabilityLevel::Suitable );
    REQUIRE( result->datasetVersionId() == QStringLiteral( "dv-42" ) );
}

TEST_CASE( "explicit facts win over an injected provider", "[suitability][labels]" )
{
    DatasetFacts providerFacts;
    providerFacts.datasetVersionId = QStringLiteral( "dv-42" );
    providerFacts.hasLabelSchema = true;
    providerFacts.sampleCount = 10;
    InMemoryDataProvider provider( providerFacts );

    DatasetFacts explicitFacts;
    explicitFacts.datasetVersionId = QStringLiteral( "dv-42" );
    explicitFacts.hasLabelSchema = true;
    explicitFacts.sampleCount = 5000;

    SuitabilityGoal goal;
    goal.requireLabels = true;
    goal.minSamples = 200;
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.datasetVersionId = QStringLiteral( "dv-42" );
    inputs.facts = explicitFacts;
    inputs.provider = &provider;

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *labels = findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    REQUIRE( labels->level == SuitabilityLevel::Suitable );
    REQUIRE( labels->evidence.value( QStringLiteral( "sample_count" ) ).toInteger() == 5000 );
}

TEST_CASE( "a provider failure is typed upward, never swallowed", "[suitability][labels]" )
{
    InMemoryDataProvider failing( sicnu::data::Diagnostic{
        QStringLiteral( "suitability.dataset_unknown" ),
        QStringLiteral( "version missing" ),
        sicnu::data::DiagnosticSeverity::Error } );

    SuitabilityGoal goal;
    goal.requireLabels = true;
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.datasetVersionId = QStringLiteral( "dv-missing" );
    inputs.provider = &failing;

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( !result.has_value() );
    REQUIRE( result.diagnostics().first().code ==
             QStringLiteral( "suitability.dataset_unknown" ) );
}

TEST_CASE( "a provider without a named version is not consulted", "[suitability][labels]" )
{
    InMemoryDataProvider failing( sicnu::data::Diagnostic{
        QStringLiteral( "suitability.dataset_unknown" ),
        QStringLiteral( "must not be reached" ),
        sicnu::data::DiagnosticSeverity::Error } );

    SuitabilityGoal goal;
    goal.requireLabels = true;
    // A legal subject that is NOT a named dataset version: the provider is
    // never asked, and the label verdict stays honestly unknown.
    goal.hasAoi = true;
    goal.aoi = sicnu::data::SpatialExtent{ 0.0, 0.0, 10.0, 10.0, true };
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.provider = &failing;

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *labels = findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    REQUIRE( labels->level == SuitabilityLevel::Unknown );
}
