// test_suitability_profiles.cpp — Slice F: the built-in task profile table
// and its overlay into ResolvedRequirements.
//
// RED-first contract for the profiles slice:
//   - a profile carries DEFAULTS only; explicit goal values always win and
//     profiles never compute;
//   - an empty profileKey selects the task-family's default profile;
//     "phenology" is an extra profile selectable ONLY in combination with
//     the TemporalPrediction family; any other unknown key fails typed
//     ("suitability.profile_unknown");
//   - each profile's defaults surface end-to-end through the criteria: the
//     student story is "before you run, the report tells you what is
//     missing" (missing NIR, missing winter, thin samples).

#include <catch2/catch_test_macros.hpp>

#include "suitability/suitability_assessor.h"
#include "suitability/suitability_goal.h"
#include "suitability/suitability_profiles.h"
#include "suitability/suitability_types.h"

#include "data/data_asset.h"
#include "data/data_result.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using sicnu::data::AssetState;
using sicnu::dataset::BenchmarkTaskFamily;
using sicnu::suitability::DatasetFacts;
using sicnu::suitability::ResolvedRequirements;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::SuitabilityAssessor;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGap;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::SuitabilityReport;
using sicnu::suitability::builtinProfileKeys;
using sicnu::suitability::isBuiltinProfile;
using sicnu::suitability::resolveRequirements;
using sicnu::suitability::suitabilityLevelFromString;
using sicnu::suitability::suitabilitySeverityRank;

namespace
{

SceneCandidate makeScene( const QString &id, const QString &acquisitionIso = QString() )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = AssetState::Ready;
    if ( !acquisitionIso.isEmpty() )
        scene.acquisitionTimeUtc =
            QDateTime::fromString( acquisitionIso, Qt::ISODate );
    return scene;
}

DatasetFacts identifiedFacts()
{
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-1" );
    return facts;
}

bool hasGap( const SuitabilityReport &report, const QString &id )
{
    for ( const SuitabilityGap &gap : report.allGaps() )
        if ( gap.id == id )
            return true;
    return false;
}

const SuitabilityCriterion *findCriterion( const SuitabilityReport &report, const QString &id )
{
    for ( const SuitabilityCriterion &criterion : report.criteria() )
        if ( criterion.id == id )
            return &criterion;
    return nullptr;
}

} // namespace

TEST_CASE( "builtin profile table carries the eight profiles", "[suitability][profiles]" )
{
    REQUIRE( builtinProfileKeys().size() == 8 );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "classification" ) ) );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "segmentation" ) ) );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "change_detection" ) ) );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "object_detection" ) ) );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "regression" ) ) );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "temporal_prediction" ) ) );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "spectral_matching" ) ) );
    REQUIRE( builtinProfileKeys().contains( QStringLiteral( "phenology" ) ) );
    REQUIRE( isBuiltinProfile( QStringLiteral( "classification" ) ) );
    REQUIRE( isBuiltinProfile( QStringLiteral( "phenology" ) ) );
    REQUIRE( !isBuiltinProfile( QStringLiteral( "nope" ) ) );
    REQUIRE( !isBuiltinProfile( QString() ) );
}

TEST_CASE( "each task family resolves a default profile with its defaults",
           "[suitability][profiles]" )
{
    auto resolveFor = []( BenchmarkTaskFamily family )
    {
        SuitabilityGoal goal;
        goal.taskFamily = family;
        const auto resolved = resolveRequirements( goal );
        REQUIRE( resolved.has_value() );
        return resolved.value();
    };

    // classification: labels required, 200 samples, 0.95 coverage.
    const auto classification = resolveFor( BenchmarkTaskFamily::Classification );
    REQUIRE( classification.requireLabels );
    REQUIRE( classification.minSamples == 200 );
    REQUIRE( classification.minCoverageFractionResolved == 0.95 );
    REQUIRE( classification.pseudoLabelsAllowed );

    // segmentation: labels required, 50 samples, 0.95 coverage.
    const auto segmentation = resolveFor( BenchmarkTaskFamily::Segmentation );
    REQUIRE( segmentation.requireLabels );
    REQUIRE( segmentation.minSamples == 50 );
    REQUIRE( segmentation.minCoverageFractionResolved == 0.95 );

    // change_detection: 100 samples, two-date minimum, pseudo-labels are
    // benchmark-forbidden (they must never enter an evaluation).
    const auto change = resolveFor( BenchmarkTaskFamily::ChangeDetection );
    REQUIRE( change.requireLabels );
    REQUIRE( change.minSamples == 100 );
    REQUIRE( change.minScenesInWindow == 2 );
    REQUIRE( !change.pseudoLabelsAllowed );

    // object_detection: 300 targets and a high-resolution prior (max 2 m).
    const auto detection = resolveFor( BenchmarkTaskFamily::ObjectDetection );
    REQUIRE( detection.requireLabels );
    REQUIRE( detection.minSamples == 300 );
    REQUIRE( detection.maxGsdM == 2.0 );
    REQUIRE( detection.minCoverageFractionResolved == 0.9 );

    // regression: labels required, 100 samples.
    const auto regression = resolveFor( BenchmarkTaskFamily::Regression );
    REQUIRE( regression.requireLabels );
    REQUIRE( regression.minSamples == 100 );

    // temporal_prediction: no labels required, four scenes minimum.
    const auto temporal = resolveFor( BenchmarkTaskFamily::TemporalPrediction );
    REQUIRE( !temporal.requireLabels );
    REQUIRE( temporal.minScenesInWindow == 4 );

    // spectral_matching: no labels required (band needs come from the goal).
    const auto spectral = resolveFor( BenchmarkTaskFamily::SpectralMatching );
    REQUIRE( !spectral.requireLabels );
}

TEST_CASE( "phenology profile is selectable only with the temporal prediction family",
           "[suitability][profiles]" )
{
    SuitabilityGoal goal;
    goal.taskFamily = BenchmarkTaskFamily::TemporalPrediction;
    goal.profileKey = QStringLiteral( "phenology" );
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );
    REQUIRE( !resolved->requireLabels );
    REQUIRE( resolved->minScenesInWindow == 6 );
    REQUIRE( resolved->requiredSeasons ==
             QStringList{ QStringLiteral( "spring" ), QStringLiteral( "summer" ),
                          QStringLiteral( "autumn" ), QStringLiteral( "winter" ) } );

    // The same key under any other family does not resolve.
    SuitabilityGoal misplaced;
    misplaced.taskFamily = BenchmarkTaskFamily::Classification;
    misplaced.profileKey = QStringLiteral( "phenology" );
    const auto refused = resolveRequirements( misplaced );
    REQUIRE( !refused.has_value() );
    REQUIRE( refused.diagnostics().first().code == QStringLiteral( "suitability.profile_unknown" ) );

    // An unknown key fails typed under every family (regression lock).
    SuitabilityGoal unknown;
    unknown.profileKey = QStringLiteral( "nope" );
    const auto rejected = resolveRequirements( unknown );
    REQUIRE( !rejected.has_value() );
    REQUIRE( rejected.diagnostics().first().code == QStringLiteral( "suitability.profile_unknown" ) );
}

TEST_CASE( "explicit goal values override the profile defaults", "[suitability][profiles]" )
{
    SuitabilityGoal goal;
    goal.minSamples = 5; // classification default is 200
    goal.requireLabels = true;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );
    REQUIRE( resolved->minSamples == 5 );
    REQUIRE( resolved->requireLabels );

    SuitabilityGoal densityGoal;
    densityGoal.taskFamily = BenchmarkTaskFamily::TemporalPrediction; // default 4
    densityGoal.minScenesInWindow = 9;
    const auto densityResolved = resolveRequirements( densityGoal );
    REQUIRE( densityResolved.has_value() );
    REQUIRE( densityResolved->minScenesInWindow == 9 );

    SuitabilityGoal gsdGoal;
    gsdGoal.taskFamily = BenchmarkTaskFamily::ObjectDetection; // default max 2.0
    gsdGoal.maxGsdM = 30.0;
    const auto gsdResolved = resolveRequirements( gsdGoal );
    REQUIRE( gsdResolved.has_value() );
    REQUIRE( gsdResolved->maxGsdM == 30.0 );
}

TEST_CASE( "classification story: missing NIR is reported before any compute",
           "[suitability][profiles]" )
{
    // A student picks land-cover classification, full AOI coverage, in-window
    // summer scenes — but the scenes carry only red/green/blue while the
    // goal demands NIR. The report must say Unsuitable with a named gap,
    // while the label dimension (facts built to fit) stays Suitable.
    SuitabilityGoal goal;
    goal.hasAoi = true;
    goal.aoi = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    goal.aoiCrsWkt = QStringLiteral( "scene-crs" );
    goal.requiredSeasons = { QStringLiteral( "summer" ) };
    goal.hasTimeWindow = true;
    goal.windowStartUtc = QDateTime::fromString( QStringLiteral( "2024-01-01T00:00:00Z" ), Qt::ISODate );
    goal.windowEndUtc = QDateTime::fromString( QStringLiteral( "2025-01-01T00:00:00Z" ), Qt::ISODate );
    goal.requiredBandRoles = { QStringLiteral( "nir" ), QStringLiteral( "red" ),
                               QStringLiteral( "green" ) };

    QVector<SceneCandidate> scenes;
    for ( const QString &id : { QStringLiteral( "jul" ), QStringLiteral( "aug" ) } )
    {
        SceneCandidate scene = makeScene( id, QStringLiteral( "2024-07-15T00:00:00Z" ) );
        scene.crsWkt = QStringLiteral( "scene-crs" );
        scene.extent = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
        scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "green" ),
                            QStringLiteral( "blue" ) };
        scenes.append( scene );
    }

    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    facts.sampleCount = 1000;

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.scenes = scenes;
    inputs.facts = facts;
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    REQUIRE( result->overallLevel() == SuitabilityLevel::Unsuitable );
    REQUIRE( hasGap( *result, QStringLiteral( "band.missing.nir" ) ) );
    const SuitabilityCriterion *labels =
        findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    REQUIRE( labels->level == SuitabilityLevel::Suitable );
}

TEST_CASE( "phenology story: a missing winter season is a named gap",
           "[suitability][profiles]" )
{
    SuitabilityGoal goal;
    goal.taskFamily = BenchmarkTaskFamily::TemporalPrediction;
    goal.profileKey = QStringLiteral( "phenology" );

    QVector<SceneCandidate> scenes;
    for ( const auto &time : { QStringLiteral( "2024-04-01T00:00:00Z" ),
                               QStringLiteral( "2024-05-01T00:00:00Z" ),
                               QStringLiteral( "2024-07-01T00:00:00Z" ),
                               QStringLiteral( "2024-08-01T00:00:00Z" ),
                               QStringLiteral( "2024-10-01T00:00:00Z" ),
                               QStringLiteral( "2024-11-01T00:00:00Z" ) } )
        scenes.append( makeScene( QStringLiteral( "s-%1" ).arg( time ), time ) );

    DatasetFacts facts = identifiedFacts();
    facts.samplesBySeason.insert( QStringLiteral( "spring" ), 10 );
    facts.samplesBySeason.insert( QStringLiteral( "summer" ), 12 );
    facts.samplesBySeason.insert( QStringLiteral( "autumn" ), 8 );
    // No winter anywhere.

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.scenes = scenes;
    inputs.facts = facts;
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    REQUIRE( hasGap( *result, QStringLiteral( "season.missing.winter" ) ) );
    const SuitabilityCriterion *seasonality =
        findCriterion( *result, QStringLiteral( "temporal.seasonality" ) );
    REQUIRE( seasonality != nullptr );
    REQUIRE( seasonality->level == SuitabilityLevel::Unsuitable );
    REQUIRE( result->overallLevel() == SuitabilityLevel::Unsuitable );
}

TEST_CASE( "goal minSamples overrides the classification default end-to-end",
           "[suitability][profiles]" )
{
    SuitabilityGoal goal;
    goal.minSamples = 5; // the student asks for five samples, not the 200 default

    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    facts.sampleCount = 3; // below the goal's five

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.facts = facts;
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *labels =
        findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    REQUIRE( labels->level == SuitabilityLevel::Unsuitable );
    REQUIRE( labels->gaps.size() == 1 );
    REQUIRE( labels->gaps.first().id == QStringLiteral( "samples.below_minimum" ) );
    // The grading used the goal's 5, NOT the profile default 200.
    REQUIRE( labels->gaps.first().evidence.value( QStringLiteral( "required_min_samples" ) )
                 .toInteger() == 5 );
}

TEST_CASE( "unselected family default applies: object detection samples and GSD",
           "[suitability][profiles]" )
{
    SuitabilityGoal goal;
    goal.taskFamily = BenchmarkTaskFamily::ObjectDetection; // no profileKey

    SceneCandidate coarse = makeScene( QStringLiteral( "coarse" ) );
    coarse.gsdM = 10.0; // far beyond the 2 m detection prior

    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    facts.sampleCount = 100; // below the 300-target default

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.scenes = { coarse };
    inputs.facts = facts;
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    REQUIRE( result->overallLevel() == SuitabilityLevel::Unsuitable );

    const SuitabilityCriterion *labels =
        findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    REQUIRE( labels->level == SuitabilityLevel::Unsuitable );
    REQUIRE( labels->gaps.first().id == QStringLiteral( "samples.below_minimum" ) );
    REQUIRE( labels->gaps.first().evidence.value( QStringLiteral( "required_min_samples" ) )
                 .toInteger() == 300 );

    // The high-resolution prior flowed into the resolution criterion.
    const SuitabilityCriterion *resolution =
        findCriterion( *result, QStringLiteral( "spatial.resolution" ) );
    REQUIRE( resolution != nullptr );
    REQUIRE( resolution->evidence.value( QStringLiteral( "required_max_gsd_m" ) ).toDouble() == 2.0 );
    REQUIRE( resolution->level == SuitabilityLevel::Unsuitable );
}

TEST_CASE( "change detection forbids pseudo labels by default", "[suitability][profiles]" )
{
    SuitabilityGoal goal;
    goal.taskFamily = BenchmarkTaskFamily::ChangeDetection;

    DatasetFacts facts = identifiedFacts();
    facts.hasLabelSchema = true;
    facts.sampleCount = 500;
    facts.pseudoLabelCount = 7;

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.facts = facts;
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *labels =
        findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    // Everything else fits, but pseudo-labels may not enter a benchmark —
    // so the verdict downgrades to Marginal with a named gap.
    REQUIRE( labels->level == SuitabilityLevel::Marginal );
    REQUIRE( labels->gaps.size() == 1 );
    REQUIRE( labels->gaps.first().id == QStringLiteral( "labels.pseudo_present" ) );
}

TEST_CASE( "the serialized report stays consistent with the criteria worst",
           "[suitability][profiles]" )
{
    // Teaching/agent consistency placeholder (Slice H completes the
    // narrative half): the report's overall level must equal the worst
    // serialized criterion level, computed from the JSON itself.
    SuitabilityGoal goal;
    goal.hasAoi = true;
    goal.aoi = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    goal.aoiCrsWkt = QStringLiteral( "scene-crs" );
    goal.requiredBandRoles = { QStringLiteral( "nir" ) };

    SceneCandidate scene = makeScene( QStringLiteral( "rgb" ), QStringLiteral( "2024-07-15T00:00:00Z" ) );
    scene.crsWkt = QStringLiteral( "scene-crs" );
    scene.extent = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "green" ), QStringLiteral( "blue" ) };

    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.scenes = { scene };
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );

    const SuitabilityReport report = result.value();
    const QJsonObject json = report.toJson();
    SuitabilityLevel worstFromJson = SuitabilityLevel::Suitable;
    bool anyApplicable = false;
    for ( const auto &criterionValue : json.value( QStringLiteral( "criteria" ) ).toArray() )
    {
        const QJsonObject criterionJson = criterionValue.toObject();
        if ( !criterionJson.value( QStringLiteral( "applicable" ) ).toBool() )
            continue;
        const auto level =
            suitabilityLevelFromString( criterionJson.value( QStringLiteral( "level" ) ).toString() );
        REQUIRE( level.has_value() );
        anyApplicable = true;
        if ( suitabilitySeverityRank( *level ) > suitabilitySeverityRank( worstFromJson ) )
            worstFromJson = *level;
    }
    if ( !anyApplicable )
        worstFromJson = SuitabilityLevel::Unknown;
    REQUIRE( report.overallLevel() == worstFromJson );
}
