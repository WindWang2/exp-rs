// test_suitability_temporal.cpp — Slice D: season mapping and the temporal
// coverage / density / seasonality criteria.
//
// RED-first contract for the temporal slice:
//   - without a time window nothing temporal can be graded (Unknown), and
//     with a window the honest verdicts are per-scene-time first, dataset
//     facts extent second;
//   - density uses scene counts plus AT MOST a single estimated "temporal
//     cluster" from a facts extent (an extent is not a scene count — the
//     estimate is stated in a note);
//   - seasons are meteorological, northern-hemisphere, derived from scene
//     month or facts sample distributions; every missing required season is
//     one typed gap.

#include <catch2/catch_test_macros.hpp>

#include "suitability/criteria_temporal.h"
#include "suitability/dataset_facts.h"
#include "suitability/seasonality.h"
#include "suitability/suitability_assessor.h"
#include "suitability/suitability_goal.h"
#include "suitability/suitability_types.h"

#include "data/data_asset.h"

#include <QJsonObject>

using sicnu::data::AssetState;
using sicnu::suitability::DatasetFacts;
using sicnu::suitability::ResolvedRequirements;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::assessTemporalCoverage;
using sicnu::suitability::assessTemporalDensity;
using sicnu::suitability::assessTemporalSeasonality;
using sicnu::suitability::resolveRequirements;
using sicnu::suitability::seasonForMonth;

namespace
{

QDateTime utcTime( const QString &iso )
{
    return QDateTime::fromString( iso, Qt::ISODate );
}

SceneCandidate makeScene( const QString &id, const QString &acquisitionIso = QString() )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = AssetState::Ready;
    if ( !acquisitionIso.isEmpty() )
        scene.acquisitionTimeUtc = utcTime( acquisitionIso );
    return scene;
}

ResolvedRequirements reqWithWindow()
{
    SuitabilityGoal goal;
    goal.hasTimeWindow = true;
    goal.windowStartUtc = utcTime( QStringLiteral( "2024-01-01T00:00:00Z" ) );
    goal.windowEndUtc = utcTime( QStringLiteral( "2025-01-01T00:00:00Z" ) );
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );
    return resolved.value();
}

ResolvedRequirements emptyRequirements()
{
    const auto resolved = resolveRequirements( SuitabilityGoal{} );
    REQUIRE( resolved.has_value() );
    return resolved.value();
}

DatasetFacts identifiedFacts()
{
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-1" );
    return facts;
}

} // namespace

TEST_CASE( "seasonForMonth maps meteorological northern-hemisphere seasons", "[suitability][temporal]" )
{
    REQUIRE( seasonForMonth( 1 ) == QStringLiteral( "winter" ) );
    REQUIRE( seasonForMonth( 3 ) == QStringLiteral( "spring" ) );
    REQUIRE( seasonForMonth( 6 ) == QStringLiteral( "summer" ) );
    REQUIRE( seasonForMonth( 9 ) == QStringLiteral( "autumn" ) );
    REQUIRE( seasonForMonth( 12 ) == QStringLiteral( "winter" ) );
    REQUIRE( seasonForMonth( 13 ).isEmpty() );
    REQUIRE( seasonForMonth( 0 ).isEmpty() );
    REQUIRE( seasonForMonth( -2 ).isEmpty() );
}

TEST_CASE( "temporal coverage without a window is unknown", "[suitability][temporal]" )
{
    const ResolvedRequirements req = emptyRequirements();
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ), QStringLiteral( "2024-05-01T10:00:00Z" ) ) );
    const SuitabilityCriterion criterion = assessTemporalCoverage( req, scenes, std::nullopt );
    REQUIRE( criterion.id == QStringLiteral( "temporal.coverage" ) );
    REQUIRE( criterion.applicable );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.notes.join( QLatin1String( " " ) ).contains( QStringLiteral( "no time window" ) ) );
}

TEST_CASE( "temporal coverage with no usable time evidence is unknown", "[suitability][temporal]" )
{
    const ResolvedRequirements req = reqWithWindow();

    // Scenes exist but carry no acquisition time; facts carry no extent.
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "mute" ) ) );
    const SuitabilityCriterion silent = assessTemporalCoverage( req, scenes, std::nullopt );
    REQUIRE( silent.level == SuitabilityLevel::Unknown );
    REQUIRE( silent.gaps.isEmpty() );

    const SuitabilityCriterion withEmptyFacts = assessTemporalCoverage( req, scenes, identifiedFacts() );
    REQUIRE( withEmptyFacts.level == SuitabilityLevel::Unknown );
}

TEST_CASE( "temporal coverage grades in-window and out-of-window scene sets", "[suitability][temporal]" )
{
    const ResolvedRequirements req = reqWithWindow();

    QVector<SceneCandidate> inside;
    inside.append( makeScene( QStringLiteral( "a" ), QStringLiteral( "2024-03-01T00:00:00Z" ) ) );
    const SuitabilityCriterion covered = assessTemporalCoverage( req, inside, std::nullopt );
    REQUIRE( covered.level == SuitabilityLevel::Suitable );
    REQUIRE( covered.evidence.value( QStringLiteral( "in_window_count" ) ).toInt() == 1 );
    REQUIRE( covered.gaps.isEmpty() );

    // Window bounds are inclusive.
    QVector<SceneCandidate> atBounds;
    atBounds.append( makeScene( QStringLiteral( "edge" ), QStringLiteral( "2024-01-01T00:00:00Z" ) ) );
    atBounds.append( makeScene( QStringLiteral( "far" ), QStringLiteral( "2025-06-01T00:00:00Z" ) ) );
    const SuitabilityCriterion boundary = assessTemporalCoverage( req, atBounds, std::nullopt );
    REQUIRE( boundary.level == SuitabilityLevel::Suitable );
    REQUIRE( boundary.evidence.value( QStringLiteral( "in_window_count" ) ).toInt() == 1 );

    QVector<SceneCandidate> outside;
    outside.append( makeScene( QStringLiteral( "late" ), QStringLiteral( "2025-06-01T00:00:00Z" ) ) );
    const SuitabilityCriterion missed = assessTemporalCoverage( req, outside, std::nullopt );
    REQUIRE( missed.level == SuitabilityLevel::Unsuitable );
    REQUIRE( missed.gaps.size() == 1 );
    REQUIRE( missed.gaps.first().id == QStringLiteral( "temporal.outside_window" ) );
}

TEST_CASE( "temporal coverage falls back to the facts temporal extent", "[suitability][temporal]" )
{
    const ResolvedRequirements req = reqWithWindow();

    DatasetFacts facts = identifiedFacts();
    facts.hasTemporalExtent = true;
    facts.temporalStartUtc = utcTime( QStringLiteral( "2023-06-01T00:00:00Z" ) );
    facts.temporalEndUtc = utcTime( QStringLiteral( "2024-08-31T00:00:00Z" ) );

    // No scene times, but the dataset range intersects the window: covered.
    const SuitabilityCriterion covered = assessTemporalCoverage( req, {}, facts );
    REQUIRE( covered.level == SuitabilityLevel::Suitable );
    REQUIRE( covered.evidence.value( QStringLiteral( "facts_temporal_intersect" ) ).toBool() );

    // A facts range entirely outside the window covers nothing, and with no
    // scene time either the verdict is Unsuitable (we measured: no data in
    // the window), not Unknown.
    DatasetFacts elsewhere = facts;
    elsewhere.temporalStartUtc = utcTime( QStringLiteral( "2025-06-01T00:00:00Z" ) );
    elsewhere.temporalEndUtc = utcTime( QStringLiteral( "2025-09-01T00:00:00Z" ) );
    const SuitabilityCriterion missed = assessTemporalCoverage( req, {}, elsewhere );
    REQUIRE( missed.level == SuitabilityLevel::Unsuitable );
    REQUIRE( missed.gaps.size() == 1 );
    REQUIRE( missed.gaps.first().id == QStringLiteral( "temporal.outside_window" ) );
}

TEST_CASE( "temporal density without a requirement is not applicable", "[suitability][temporal]" )
{
    const ResolvedRequirements req = emptyRequirements();
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ), QStringLiteral( "2024-05-01T00:00:00Z" ) ) );
    const SuitabilityCriterion criterion = assessTemporalDensity( req, scenes, std::nullopt );
    REQUIRE( criterion.id == QStringLiteral( "temporal.density" ) );
    REQUIRE( !criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "temporal density needs a window to count in", "[suitability][temporal]" )
{
    SuitabilityGoal goal;
    goal.minScenesInWindow = 3;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    const SuitabilityCriterion criterion = assessTemporalDensity( *resolved, {}, std::nullopt );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
}

TEST_CASE( "temporal density grades below, at and above the minimum", "[suitability][temporal]" )
{
    SuitabilityGoal goal;
    goal.hasTimeWindow = true;
    goal.windowStartUtc = utcTime( QStringLiteral( "2024-01-01T00:00:00Z" ) );
    goal.windowEndUtc = utcTime( QStringLiteral( "2025-01-01T00:00:00Z" ) );
    goal.minScenesInWindow = 2;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    auto timedScene = []( const QString &id, const QString &iso )
    {
        return makeScene( id, iso );
    };

    // Below the minimum -> Unsuitable.
    QVector<SceneCandidate> one;
    one.append( timedScene( QStringLiteral( "a" ), QStringLiteral( "2024-02-01T00:00:00Z" ) ) );
    const SuitabilityCriterion thin = assessTemporalDensity( *resolved, one, std::nullopt );
    REQUIRE( thin.level == SuitabilityLevel::Unsuitable );
    REQUIRE( thin.gaps.size() == 1 );
    REQUIRE( thin.gaps.first().id == QStringLiteral( "temporal.below_min_density" ) );

    // Exactly at the minimum -> Marginal with at_minimum evidence.
    QVector<SceneCandidate> two;
    two.append( timedScene( QStringLiteral( "a" ), QStringLiteral( "2024-02-01T00:00:00Z" ) ) );
    two.append( timedScene( QStringLiteral( "b" ), QStringLiteral( "2024-03-01T00:00:00Z" ) ) );
    const SuitabilityCriterion borderline = assessTemporalDensity( *resolved, two, std::nullopt );
    REQUIRE( borderline.level == SuitabilityLevel::Marginal );
    REQUIRE( borderline.gaps.size() == 1 );
    REQUIRE( borderline.gaps.first().id == QStringLiteral( "temporal.below_min_density" ) );
    REQUIRE( borderline.gaps.first().evidence.value( QStringLiteral( "at_minimum" ) ).toBool() );

    // Above -> Suitable.
    QVector<SceneCandidate> three;
    three.append( timedScene( QStringLiteral( "a" ), QStringLiteral( "2024-02-01T00:00:00Z" ) ) );
    three.append( timedScene( QStringLiteral( "b" ), QStringLiteral( "2024-03-01T00:00:00Z" ) ) );
    three.append( timedScene( QStringLiteral( "c" ), QStringLiteral( "2024-04-01T00:00:00Z" ) ) );
    const SuitabilityCriterion ample = assessTemporalDensity( *resolved, three, std::nullopt );
    REQUIRE( ample.level == SuitabilityLevel::Suitable );
    REQUIRE( ample.gaps.isEmpty() );
}

TEST_CASE( "temporal density counts a facts extent as one estimated cluster", "[suitability][temporal]" )
{
    SuitabilityGoal goal;
    goal.hasTimeWindow = true;
    goal.windowStartUtc = utcTime( QStringLiteral( "2024-01-01T00:00:00Z" ) );
    goal.windowEndUtc = utcTime( QStringLiteral( "2025-01-01T00:00:00Z" ) );
    goal.minScenesInWindow = 2;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    QVector<SceneCandidate> one;
    one.append( makeScene( QStringLiteral( "a" ), QStringLiteral( "2024-02-01T00:00:00Z" ) ) );

    DatasetFacts facts = identifiedFacts();
    facts.hasTemporalExtent = true;
    facts.temporalStartUtc = utcTime( QStringLiteral( "2024-06-01T00:00:00Z" ) );
    facts.temporalEndUtc = utcTime( QStringLiteral( "2024-09-01T00:00:00Z" ) );

    // One timed scene + one facts cluster = 2 (== min) -> Marginal, and the
    // estimate is stated, not hidden.
    const SuitabilityCriterion fused = assessTemporalDensity( *resolved, one, facts );
    REQUIRE( fused.level == SuitabilityLevel::Marginal );
    REQUIRE( fused.evidence.value( QStringLiteral( "scene_in_window_count" ) ).toInt() == 1 );
    REQUIRE( fused.evidence.value( QStringLiteral( "in_window_count" ) ).toInt() == 2 );
    REQUIRE( fused.evidence.value( QStringLiteral( "facts_temporal_contributed" ) ).toBool() );
    REQUIRE( fused.notes.join( QLatin1String( " " ) ).contains( QStringLiteral( "estimated" ) ) );

    // A facts range outside the window contributes nothing.
    DatasetFacts elsewhere = facts;
    elsewhere.temporalStartUtc = utcTime( QStringLiteral( "2026-01-01T00:00:00Z" ) );
    elsewhere.temporalEndUtc = utcTime( QStringLiteral( "2026-03-01T00:00:00Z" ) );
    const SuitabilityCriterion outsideOnly = assessTemporalDensity( *resolved, one, elsewhere );
    REQUIRE( outsideOnly.level == SuitabilityLevel::Unsuitable );
    REQUIRE( outsideOnly.evidence.value( QStringLiteral( "in_window_count" ) ).toInt() == 1 );
}

TEST_CASE( "temporal seasonality without a requirement is not applicable", "[suitability][temporal]" )
{
    const ResolvedRequirements req = emptyRequirements();
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ), QStringLiteral( "2024-07-01T00:00:00Z" ) ) );
    const SuitabilityCriterion criterion = assessTemporalSeasonality( req, scenes, std::nullopt );
    REQUIRE( criterion.id == QStringLiteral( "temporal.seasonality" ) );
    REQUIRE( !criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "temporal seasonality with no observable season is unknown", "[suitability][temporal]" )
{
    SuitabilityGoal goal;
    goal.requiredSeasons = { QStringLiteral( "summer" ) };
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    QVector<SceneCandidate> mute;
    mute.append( makeScene( QStringLiteral( "mute" ) ) );
    const SuitabilityCriterion silent = assessTemporalSeasonality( *resolved, mute, std::nullopt );
    REQUIRE( silent.level == SuitabilityLevel::Unknown );

    // A zero-valued facts season entry is no evidence either.
    DatasetFacts facts = identifiedFacts();
    facts.samplesBySeason.insert( QStringLiteral( "summer" ), 0 );
    const SuitabilityCriterion zeroFacts = assessTemporalSeasonality( *resolved, mute, facts );
    REQUIRE( zeroFacts.level == SuitabilityLevel::Unknown );
}

TEST_CASE( "temporal seasonality grades observed seasons from scenes and facts", "[suitability][temporal]" )
{
    SuitabilityGoal goal;
    goal.requiredSeasons = { QStringLiteral( "summer" ), QStringLiteral( "winter" ) };
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    // A July scene observes summer only -> winter missing -> one typed gap.
    QVector<SceneCandidate> july;
    july.append( makeScene( QStringLiteral( "jul" ), QStringLiteral( "2024-07-15T00:00:00Z" ) ) );
    const SuitabilityCriterion partial = assessTemporalSeasonality( *resolved, july, std::nullopt );
    REQUIRE( partial.level == SuitabilityLevel::Unsuitable );
    REQUIRE( partial.gaps.size() == 1 );
    REQUIRE( partial.gaps.first().id == QStringLiteral( "season.missing.winter" ) );
    REQUIRE( partial.gaps.first().criterionId == QStringLiteral( "temporal.seasonality" ) );
    REQUIRE( partial.evidence.value( QStringLiteral( "time_unknown_count" ) ).toInt() == 0 );

    // December scene joins: both seasons observed -> Suitable.
    july.append( makeScene( QStringLiteral( "dec" ), QStringLiteral( "2024-12-15T00:00:00Z" ) ) );
    const SuitabilityCriterion full = assessTemporalSeasonality( *resolved, july, std::nullopt );
    REQUIRE( full.level == SuitabilityLevel::Suitable );
    REQUIRE( full.gaps.isEmpty() );

    // Facts sample distributions can supply a season too (value > 0 counts).
    QVector<SceneCandidate> julyOnly;
    julyOnly.append( makeScene( QStringLiteral( "jul" ), QStringLiteral( "2024-07-15T00:00:00Z" ) ) );
    DatasetFacts facts = identifiedFacts();
    facts.samplesBySeason.insert( QStringLiteral( "winter" ), 42 );
    const SuitabilityCriterion fused = assessTemporalSeasonality( *resolved, julyOnly, facts );
    REQUIRE( fused.level == SuitabilityLevel::Suitable );

    // Untimed scenes are counted as unknown time evidence.
    QVector<SceneCandidate> mixed;
    mixed.append( makeScene( QStringLiteral( "mute" ) ) );
    mixed.append( makeScene( QStringLiteral( "jul" ), QStringLiteral( "2024-07-15T00:00:00Z" ) ) );
    const SuitabilityCriterion withUnknown = assessTemporalSeasonality( *resolved, mixed, std::nullopt );
    REQUIRE( withUnknown.evidence.value( QStringLiteral( "time_unknown_count" ) ).toInt() == 1 );
    REQUIRE( withUnknown.level == SuitabilityLevel::Unsuitable );
}

TEST_CASE( "assessor emits the full seven-criteria report in canonical order", "[suitability][temporal]" )
{
    // A fully-answered subject: AOI covered by two in-window summer scenes
    // (density above the minimum, so nothing sits Marginal, and every
    // applicable criterion grades Suitable).
    SuitabilityGoal goal;
    goal.hasAoi = true;
    goal.aoi = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    goal.aoiCrsWkt = QStringLiteral( "scene-crs" );
    goal.requiredSeasons = { QStringLiteral( "summer" ) };
    goal.hasTimeWindow = true;
    goal.windowStartUtc = utcTime( QStringLiteral( "2024-01-01T00:00:00Z" ) );
    goal.windowEndUtc = utcTime( QStringLiteral( "2025-01-01T00:00:00Z" ) );
    goal.minScenesInWindow = 1;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "jul" ), QStringLiteral( "2024-07-15T00:00:00Z" ) ) );
    scenes.append( makeScene( QStringLiteral( "aug" ), QStringLiteral( "2024-08-15T00:00:00Z" ) ) );
    for ( SceneCandidate &scene : scenes )
    {
        scene.crsWkt = QStringLiteral( "scene-crs" );
        scene.extent = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    }

    sicnu::suitability::SuitabilityAssessor::Inputs inputs;
    inputs.goal = goal;
    inputs.scenes = scenes;
    inputs.datasetVersionId = QStringLiteral( "dv-1" );
    const auto result = sicnu::suitability::SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    REQUIRE( result->criteria().size() == 7 );
    QStringList ids;
    for ( const auto &criterion : result->criteria() )
        ids.append( criterion.id );
    REQUIRE( ids == QStringList{ QStringLiteral( "quality.cloud" ),
                                 QStringLiteral( "spatial.coverage" ),
                                 QStringLiteral( "spatial.resolution" ),
                                 QStringLiteral( "spectral.bands" ),
                                 QStringLiteral( "temporal.coverage" ),
                                 QStringLiteral( "temporal.density" ),
                                 QStringLiteral( "temporal.seasonality" ) } );
    REQUIRE( result->overallLevel() == SuitabilityLevel::Suitable );
}
