// test_suitability_spectral.cpp — Slice C: spectral band & cloud cover
// criteria, DatasetFacts DTO, assessor integration.
//
// RED-first contract for the spectral/quality slice:
//   - required band roles are matched against the union of usable-scene and
//     facts band pools; every missing role is one typed gap; a single missing
//     required role makes the spectral dimension Unsuitable (a task without
//     its required bands is infeasible, not "weakened");
//   - cloud grading never invents data: unknown/out-of-range cloud values are
//     counted as unknown (with a diagnostic, never clamped);
//   - DatasetFacts serializes its hash members deterministically (sorted
//     keys) so facts content has one canonical form.

#include <catch2/catch_test_macros.hpp>

#include "suitability/criteria_spectral.h"
#include "suitability/dataset_facts.h"
#include "suitability/suitability_assessor.h"
#include "suitability/suitability_goal.h"
#include "suitability/suitability_types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using sicnu::data::AssetState;
using sicnu::suitability::DatasetFacts;
using sicnu::suitability::ResolvedRequirements;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::SuitabilityAssessor;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::assessCloudCover;
using sicnu::suitability::assessSpectralBands;
using sicnu::suitability::resolveRequirements;

namespace
{

SceneCandidate makeScene( const QString &id, AssetState state = AssetState::Ready )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = state;
    return scene;
}

ResolvedRequirements emptyRequirements()
{
    const auto resolved = resolveRequirements( SuitabilityGoal{} );
    REQUIRE( resolved.has_value() );
    return resolved.value();
}

} // namespace

TEST_CASE( "dataset facts JSON round-trip is deterministic", "[suitability][spectral]" )
{
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-1" );
    facts.factsTruncated = true;
    facts.sampleCount = 4200;
    facts.pseudoLabelCount = 300;
    facts.missingTimeCount = 12;
    facts.hasLabelSchema = true;
    facts.labelClasses = { QStringLiteral( "water" ), QStringLiteral( "forest" ) };
    facts.samplesByClass.insert( QStringLiteral( "water" ), 1200 );
    facts.samplesByClass.insert( QStringLiteral( "forest" ), 3000 );
    facts.samplesBySeason.insert( QStringLiteral( "summer" ), 2000 );
    facts.samplesBySeason.insert( QStringLiteral( "spring" ), 2200 );
    facts.samplesByYear.insert( QStringLiteral( "2023" ), 4000 );
    facts.samplesByYear.insert( QStringLiteral( "2024" ), 200 );
    facts.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    facts.modality = QStringLiteral( "optical" );
    facts.sensor = QStringLiteral( "S2MSI" );
    facts.crsWkt = QStringLiteral( "scene-crs" );
    facts.hasExtent = true;
    facts.minX = 0.0;
    facts.minY = 1.0;
    facts.maxX = 2.0;
    facts.maxY = 3.0;
    facts.hasTemporalExtent = true;
    facts.temporalStartUtc = QDateTime::fromString( QStringLiteral( "2023-01-01T00:00:00Z" ), Qt::ISODate );
    facts.temporalEndUtc = QDateTime::fromString( QStringLiteral( "2024-06-30T00:00:00Z" ), Qt::ISODate );

    // Deterministic canonical form: identical content -> identical JSON.
    REQUIRE( facts.toJson() == facts.toJson() );
    const QString first = QJsonDocument( facts.toJson() ).toJson( QJsonDocument::Compact );

    // Same content with hash members built in a different insertion order
    // must serialize identically (sorted keys).
    DatasetFacts reordered = facts;
    reordered.samplesBySeason.clear();
    reordered.samplesBySeason.insert( QStringLiteral( "spring" ), 2200 );
    reordered.samplesBySeason.insert( QStringLiteral( "summer" ), 2000 );
    const QString second = QJsonDocument( reordered.toJson() ).toJson( QJsonDocument::Compact );
    REQUIRE( first == second );

    const auto parsed = DatasetFacts::fromJson( facts.toJson() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->datasetVersionId == facts.datasetVersionId );
    REQUIRE( parsed->factsTruncated == facts.factsTruncated );
    REQUIRE( parsed->sampleCount == facts.sampleCount );
    REQUIRE( parsed->pseudoLabelCount == facts.pseudoLabelCount );
    REQUIRE( parsed->missingTimeCount == facts.missingTimeCount );
    REQUIRE( parsed->hasLabelSchema == facts.hasLabelSchema );
    REQUIRE( parsed->labelClasses == facts.labelClasses );
    REQUIRE( parsed->samplesByClass == facts.samplesByClass );
    REQUIRE( parsed->samplesBySeason == facts.samplesBySeason );
    REQUIRE( parsed->samplesByYear == facts.samplesByYear );
    REQUIRE( parsed->bandRoles == facts.bandRoles );
    REQUIRE( parsed->modality == facts.modality );
    REQUIRE( parsed->sensor == facts.sensor );
    REQUIRE( parsed->crsWkt == facts.crsWkt );
    REQUIRE( parsed->hasExtent == facts.hasExtent );
    REQUIRE( parsed->minX == facts.minX );
    REQUIRE( parsed->maxY == facts.maxY );
    REQUIRE( parsed->hasTemporalExtent == facts.hasTemporalExtent );
    REQUIRE( parsed->temporalStartUtc.toMSecsSinceEpoch()
             == facts.temporalStartUtc.toMSecsSinceEpoch() );
    REQUIRE( parsed->temporalEndUtc.toMSecsSinceEpoch() == facts.temporalEndUtc.toMSecsSinceEpoch() );
    REQUIRE( parsed->toJson() == facts.toJson() );

    // Unknown counts survive the round-trip as "unknown".
    DatasetFacts sparse;
    sparse.datasetVersionId = QStringLiteral( "dv-2" );
    const auto parsedSparse = DatasetFacts::fromJson( sparse.toJson() );
    REQUIRE( parsedSparse.has_value() );
    REQUIRE( parsedSparse->sampleCount == -1 );
    REQUIRE( parsedSparse->pseudoLabelCount == -1 );
    REQUIRE( parsedSparse->missingTimeCount == -1 );
    REQUIRE( !parsedSparse->hasLabelSchema );
    REQUIRE( !parsedSparse->hasExtent );
    REQUIRE( !parsedSparse->hasTemporalExtent );
}

TEST_CASE( "dataset facts fromJson fails typed on foreign schema or missing identity", "[suitability][spectral]" )
{
    QJsonObject foreign = DatasetFacts().toJson();
    foreign.insert( QStringLiteral( "schema_version" ), 7 );
    const auto parsedForeign = DatasetFacts::fromJson( foreign );
    REQUIRE( !parsedForeign.has_value() );
    REQUIRE( parsedForeign.diagnostics().first().code == QStringLiteral( "suitability.facts_schema" ) );

    QJsonObject noIdentity = DatasetFacts().toJson();
    noIdentity.remove( QStringLiteral( "dataset_version_id" ) );
    const auto parsedNoIdentity = DatasetFacts::fromJson( noIdentity );
    REQUIRE( !parsedNoIdentity.has_value() );
    REQUIRE( parsedNoIdentity.diagnostics().first().code == QStringLiteral( "suitability.facts_invalid" ) );
}

TEST_CASE( "spectral bands without a requirement is not applicable", "[suitability][spectral]" )
{
    const ResolvedRequirements req = emptyRequirements();
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ) ) );
    const SuitabilityCriterion criterion = assessSpectralBands( req, scenes, std::nullopt );
    REQUIRE( criterion.id == QStringLiteral( "spectral.bands" ) );
    REQUIRE( !criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "spectral bands with a requirement but no metadata anywhere is unknown", "[suitability][spectral]" )
{
    SuitabilityGoal goal;
    goal.requiredBandRoles = { QStringLiteral( "nir" ) };
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    const SuitabilityCriterion noScenes = assessSpectralBands( *resolved, {}, std::nullopt );
    REQUIRE( noScenes.level == SuitabilityLevel::Unknown );
    REQUIRE( noScenes.notes.join( QLatin1String( " " ) )
                 .contains( QStringLiteral( "no band role metadata" ) ) );

    // A scene exists but carries no roles, and no facts either.
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ) ) );
    const SuitabilityCriterion silent = assessSpectralBands( *resolved, scenes, std::nullopt );
    REQUIRE( silent.level == SuitabilityLevel::Unknown );
    REQUIRE( silent.notes.join( QLatin1String( " " ) )
                 .contains( QStringLiteral( "no band role metadata" ) ) );
}

TEST_CASE( "spectral bands satisfied from scene pool or facts pool", "[suitability][spectral]" )
{
    SuitabilityGoal goal;
    goal.requiredBandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    QVector<SceneCandidate> scenes;
    SceneCandidate scene = makeScene( QStringLiteral( "s1" ) );
    scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ), QStringLiteral( "green" ) };
    scenes.append( scene );
    const SuitabilityCriterion fromScenes = assessSpectralBands( *resolved, scenes, std::nullopt );
    REQUIRE( fromScenes.level == SuitabilityLevel::Suitable );
    REQUIRE( fromScenes.gaps.isEmpty() );
    REQUIRE( fromScenes.evidence.value( QStringLiteral( "matched_roles" ) ).toArray().size() == 2 );

    // Facts alone can satisfy the requirement (scene-less dataset subject).
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-1" );
    facts.bandRoles = { QStringLiteral( "nir" ), QStringLiteral( "red" ) };
    const SuitabilityCriterion fromFacts = assessSpectralBands( *resolved, {}, facts );
    REQUIRE( fromFacts.level == SuitabilityLevel::Suitable );

    // Non-Ready scenes contribute nothing.
    QVector<SceneCandidate> unusable;
    SceneCandidate stale = makeScene( QStringLiteral( "stale" ), AssetState::Stale );
    stale.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    unusable.append( stale );
    const SuitabilityCriterion filtered = assessSpectralBands( *resolved, unusable, std::nullopt );
    REQUIRE( filtered.level == SuitabilityLevel::Unknown );
}

TEST_CASE( "spectral bands missing any required role is unsuitable with one gap per role", "[suitability][spectral]" )
{
    SuitabilityGoal goal;
    goal.requiredBandRoles = { QStringLiteral( "NIR" ), QStringLiteral( "Red Edge" ), QStringLiteral( "red" ) };
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    QVector<SceneCandidate> scenes;
    SceneCandidate scene = makeScene( QStringLiteral( "s1" ) );
    scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "green" ) };
    scenes.append( scene );

    const SuitabilityCriterion criterion = assessSpectralBands( *resolved, scenes, std::nullopt );
    // One missing required band already makes the task infeasible.
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( criterion.gaps.size() == 2 );
    QStringList gapIds;
    for ( const auto &gap : criterion.gaps )
    {
        gapIds.append( gap.id );
        REQUIRE( gap.criterionId == QStringLiteral( "spectral.bands" ) );
    }
    REQUIRE( gapIds.contains( QStringLiteral( "band.missing.nir" ) ) );
    REQUIRE( gapIds.contains( QStringLiteral( "band.missing.red_edge" ) ) );
}

TEST_CASE( "cloud cover without a requirement is not applicable", "[suitability][spectral]" )
{
    const ResolvedRequirements req = emptyRequirements();
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ) ) );
    const SuitabilityCriterion criterion = assessCloudCover( req, scenes );
    REQUIRE( criterion.id == QStringLiteral( "quality.cloud" ) );
    REQUIRE( !criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "cloud cover with no measurable cloud is unknown", "[suitability][spectral]" )
{
    SuitabilityGoal goal;
    goal.maxCloudCoverPercent = 20.0;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    REQUIRE( assessCloudCover( *resolved, {} ).level == SuitabilityLevel::Unknown );

    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ) ) );
    const SuitabilityCriterion criterion = assessCloudCover( *resolved, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.evidence.value( QStringLiteral( "unknown_count" ) ).toInt() == 1 );

    // Out-of-range cloud values are unknown evidence with a diagnostic —
    // never clamped into range.
    QVector<SceneCandidate> polluted;
    SceneCandidate negative = makeScene( QStringLiteral( "neg" ) );
    negative.cloudCoverPercent = -5.0;
    polluted.append( negative );
    const SuitabilityCriterion warned = assessCloudCover( *resolved, polluted );
    REQUIRE( warned.level == SuitabilityLevel::Unknown );
    REQUIRE( warned.evidence.value( QStringLiteral( "unknown_count" ) ).toInt() == 1 );
    REQUIRE( !warned.diagnostics.isEmpty() );
    REQUIRE( warned.diagnostics.first().severity == sicnu::data::DiagnosticSeverity::Warning );
}

TEST_CASE( "cloud cover grades clear, mixed and fully-cloudy scene sets", "[suitability][spectral]" )
{
    SuitabilityGoal goal;
    goal.maxCloudCoverPercent = 20.0;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );

    auto withCloud = []( const QString &id, double cloud )
    {
        SceneCandidate scene = makeScene( id );
        scene.cloudCoverPercent = cloud;
        return scene;
    };

    // All within limit (inclusive) -> Suitable.
    QVector<SceneCandidate> clear;
    clear.append( withCloud( QStringLiteral( "a" ), 0.0 ) );
    clear.append( withCloud( QStringLiteral( "b" ), 20.0 ) );
    const SuitabilityCriterion fine = assessCloudCover( *resolved, clear );
    REQUIRE( fine.level == SuitabilityLevel::Suitable );
    REQUIRE( fine.gaps.isEmpty() );
    REQUIRE( fine.evidence.value( QStringLiteral( "worst_cloud_cover_percent" ) ).toDouble() == 20.0 );

    // All above limit -> Unsuitable.
    QVector<SceneCandidate> cloudy;
    cloudy.append( withCloud( QStringLiteral( "a" ), 80.0 ) );
    cloudy.append( withCloud( QStringLiteral( "b" ), 95.0 ) );
    const SuitabilityCriterion bad = assessCloudCover( *resolved, cloudy );
    REQUIRE( bad.level == SuitabilityLevel::Unsuitable );
    REQUIRE( bad.gaps.size() == 1 );
    REQUIRE( bad.gaps.first().id == QStringLiteral( "cloud.cover_exceeded" ) );

    // Some clear -> Marginal; unknown clouds are counted, not judged.
    QVector<SceneCandidate> mixed;
    mixed.append( withCloud( QStringLiteral( "clear" ), 5.0 ) );
    mixed.append( withCloud( QStringLiteral( "opaque" ), 70.0 ) );
    SceneCandidate mystery = makeScene( QStringLiteral( "mystery" ) );
    mixed.append( mystery );
    const SuitabilityCriterion partial = assessCloudCover( *resolved, mixed );
    REQUIRE( partial.level == SuitabilityLevel::Marginal );
    REQUIRE( partial.gaps.size() == 1 );
    REQUIRE( partial.gaps.first().id == QStringLiteral( "cloud.cover_exceeded" ) );
    REQUIRE( partial.evidence.value( QStringLiteral( "clear_count" ) ).toInt() == 1 );
    REQUIRE( partial.evidence.value( QStringLiteral( "known_count" ) ).toInt() == 2 );
    REQUIRE( partial.evidence.value( QStringLiteral( "unknown_count" ) ).toInt() == 1 );
    REQUIRE( partial.evidence.value( QStringLiteral( "worst_cloud_cover_percent" ) ).toDouble() == 70.0 );
}

TEST_CASE( "assessor integrates spectral and cloud criteria with facts", "[suitability][spectral]" )
{
    SuitabilityAssessor::Inputs inputs;
    inputs.goal.requiredBandRoles = { QStringLiteral( "nir" ) };
    inputs.datasetVersionId = QStringLiteral( "dv-1" );

    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-1" );
    facts.bandRoles = { QStringLiteral( "nir" ), QStringLiteral( "red" ) };
    inputs.facts = facts;

    // A facts-only subject is legal (no empty-subject refusal).
    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    REQUIRE( result->criteria().size() == 10 );
    QStringList ids;
    for ( const auto &criterion : result->criteria() )
        ids.append( criterion.id );
    REQUIRE( ids == QStringList{ QStringLiteral( "grid.compatibility" ),
                                 QStringLiteral( "labels.availability" ),
                                 QStringLiteral( "model.compatibility" ),
                                 QStringLiteral( "quality.cloud" ),
                                 QStringLiteral( "spatial.coverage" ),
                                 QStringLiteral( "spatial.resolution" ),
                                 QStringLiteral( "spectral.bands" ),
                                 QStringLiteral( "temporal.coverage" ),
                                 QStringLiteral( "temporal.density" ),
                                 QStringLiteral( "temporal.seasonality" ) } );
    const SuitabilityCriterion *spectral = nullptr;
    for ( const auto &criterion : result->criteria() )
        if ( criterion.id == QLatin1String( "spectral.bands" ) )
            spectral = &criterion;
    REQUIRE( spectral != nullptr );
    // Facts carry the role, so spectral is judgeable even without scenes.
    REQUIRE( spectral->level == SuitabilityLevel::Suitable );

    // Without facts and without scenes, the required-band requirement is
    // unknown, never silently suitable.
    SuitabilityAssessor::Inputs bare;
    bare.goal.requiredBandRoles = { QStringLiteral( "nir" ) };
    bare.datasetVersionId = QStringLiteral( "dv-2" );
    const auto bareResult = SuitabilityAssessor::assess( bare );
    REQUIRE( bareResult.has_value() );
    const SuitabilityCriterion *bareSpectral = nullptr;
    for ( const auto &criterion : bareResult->criteria() )
        if ( criterion.id == QLatin1String( "spectral.bands" ) )
            bareSpectral = &criterion;
    REQUIRE( bareSpectral != nullptr );
    REQUIRE( bareSpectral->level == SuitabilityLevel::Unknown );

    // Still refuses a subject that identifies nothing at all.
    SuitabilityAssessor::Inputs nothing;
    nothing.facts = DatasetFacts();  // no datasetVersionId -> not an identity
    const auto refused = SuitabilityAssessor::assess( nothing );
    REQUIRE( !refused.has_value() );
    REQUIRE( refused.diagnostics().first().code == QStringLiteral( "suitability.empty_subject" ) );
}
