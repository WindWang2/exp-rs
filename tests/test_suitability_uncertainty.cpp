// test_suitability_uncertainty.cpp — Slice H1: the uncertainty.sources
// criterion.
//
// The job of this suite is to lock the roll-up semantics:
//   - every criterion note, every Warning/Error diagnostic, facts truncation
//     and every invalid-measurement ("conflicting") diagnostic becomes a
//     named, machine-readable uncertainty source;
//   - blocking conflicts (broken measurements) grade Unsuitable, any other
//     source grades Marginal, no source grades Suitable — the criterion is
//     always applicable and never Unknown (the sources array always exists,
//     possibly empty);
//   - the source list is deterministic: sorted by code, same-code entries
//     keep but duplicate details collapse;
//   - a report whose criteria are all Suitable but which carries an
//     undigested assumption is overall Marginal — a clean Suitable must be
//     earned by a fully answered subject, not by aggregation luck.

#include <catch2/catch_test_macros.hpp>

#include "suitability/criteria_uncertainty.h"
#include "suitability/dataset_facts.h"
#include "suitability/suitability_assessor.h"
#include "suitability/suitability_types.h"

#include "data/data_asset.h"

#include <QJsonArray>
#include <QJsonObject>

#include <limits>
#include <QJsonValue>

using sicnu::data::AssetState;
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::suitability::DatasetFacts;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::SuitabilityAssessor;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGap;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::SuitabilityReport;
using sicnu::suitability::assessUncertaintySources;

namespace
{

SuitabilityCriterion criterionWithId( const QString &id )
{
    SuitabilityCriterion criterion;
    criterion.id = id;
    return criterion;
}

Diagnostic warning( const QString &code, const QString &message )
{
    return Diagnostic{ code, message, DiagnosticSeverity::Warning };
}

int sourceCount( const SuitabilityCriterion &criterion, const QString &code )
{
    int count = 0;
    const QJsonArray sources = criterion.evidence.value( QStringLiteral( "sources" ) ).toArray();
    for ( const QJsonValue &value : sources )
    {
        if ( value.toObject().value( QStringLiteral( "code" ) ).toString() == code )
            ++count;
    }
    return count;
}

/// The fully-answered scene set of the adversarial suite: three identical
/// ready scenes (coverage, GSD, time, season, cloud, bands, grid all fit the
/// goal below) — the subject that used to grade overall Suitable.
SceneCandidate baseScene( const QString &id )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = AssetState::Ready;
    scene.crsWkt = QStringLiteral( "crs-1" );
    scene.extent = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    scene.gsdM = 5.0;
    scene.acquisitionTimeUtc =
        QDateTime::fromString( QStringLiteral( "2024-04-10T10:00:00Z" ), Qt::ISODate );
    scene.cloudCoverPercent = 10.0;
    scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    sicnu::data::RasterGrid grid;
    grid.crsWkt = QStringLiteral( "crs-1" );
    grid.hasGeoTransform = true;
    grid.geoTransform = { 500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
    grid.width = 100;
    grid.height = 100;
    scene.grid = grid;
    return scene;
}

SuitabilityGoal perfectGoal()
{
    SuitabilityGoal goal;
    goal.hasAoi = true;
    goal.aoi = sicnu::data::SpatialExtent{ 0.0, 0.0, 100.0, 100.0, true };
    goal.aoiCrsWkt = QStringLiteral( "crs-1" );
    goal.hasTimeWindow = true;
    goal.windowStartUtc =
        QDateTime::fromString( QStringLiteral( "2024-03-01T00:00:00Z" ), Qt::ISODate );
    goal.windowEndUtc =
        QDateTime::fromString( QStringLiteral( "2024-06-30T00:00:00Z" ), Qt::ISODate );
    goal.requiredSeasons = { QStringLiteral( "spring" ) };
    goal.minGsdM = 1.0;
    goal.maxGsdM = 10.0;
    goal.requiredBandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    goal.maxCloudCoverPercent = 50.0;
    goal.minSamples = 200;
    goal.requiredClasses = { QStringLiteral( "forest" ) };
    goal.requireLabels = true;
    goal.minScenesInWindow = 2;
    goal.minCoverageFraction = 0.5;
    return goal;
}

DatasetFacts perfectFacts()
{
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-perfect" );
    facts.sampleCount = 300;
    facts.pseudoLabelCount = 0;
    facts.hasLabelSchema = true;
    facts.labelClasses = { QStringLiteral( "forest" ), QStringLiteral( "water" ) };
    return facts;
}

const SuitabilityCriterion *findCriterion( const SuitabilityReport &report, const QString &id )
{
    for ( const SuitabilityCriterion &criterion : report.criteria() )
        if ( criterion.id == id )
            return &criterion;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. The three grade branches.
// ---------------------------------------------------------------------------

TEST_CASE( "an assessment with no uncertainty sources grades suitable and never unknown",
           "[suitability][uncertainty]" )
{
    // Nothing was assumed away: the empty sources array is the evidence of a
    // clean assessment. The criterion stays applicable — "no uncertainty"
    // is a measured statement, not a skipped one.
    const SuitabilityCriterion criterion = assessUncertaintySources( {}, std::nullopt );
    REQUIRE( criterion.id == QStringLiteral( "uncertainty.sources" ) );
    REQUIRE( criterion.applicable );
    REQUIRE( criterion.level == SuitabilityLevel::Suitable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "count" ) ).toInt() == 0 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_count" ) ).toInt() == 0 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "sources" ) ).toArray().isEmpty() );
}

TEST_CASE( "every criterion note becomes a non-blocking uncertainty source",
           "[suitability][uncertainty]" )
{
    SuitabilityCriterion noted = criterionWithId( QStringLiteral( "spatial.coverage" ) );
    noted.notes.append( QStringLiteral( "CRS mismatch, scene 'x' excluded" ) );
    noted.notes.append( QStringLiteral( "AOI not specified" ) );

    const SuitabilityCriterion criterion = assessUncertaintySources( { noted }, std::nullopt );
    REQUIRE( criterion.level == SuitabilityLevel::Marginal );
    REQUIRE( criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "count" ) ).toInt() == 2 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_count" ) ).toInt() == 0 );
    // One source per note, named after the criterion that carries it.
    REQUIRE( sourceCount( criterion, QStringLiteral( "uncertainty.note.spatial.coverage" ) ) == 2 );
}

TEST_CASE( "warning and error diagnostics and truncation become non-blocking sources",
           "[suitability][uncertainty]" )
{
    SuitabilityCriterion diagnosed = criterionWithId( QStringLiteral( "grid.compatibility" ) );
    diagnosed.diagnostics.append(
        warning( QStringLiteral( "suitability.grid_compare_failed" ), "compareGrids threw" ) );

    DatasetFacts truncated;
    truncated.factsTruncated = true;

    const SuitabilityCriterion criterion =
        assessUncertaintySources( { diagnosed }, truncated );
    REQUIRE( criterion.level == SuitabilityLevel::Marginal );
    REQUIRE( criterion.evidence.value( QStringLiteral( "count" ) ).toInt() == 2 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_count" ) ).toInt() == 0 );
    REQUIRE( sourceCount( criterion, QStringLiteral( "uncertainty.diagnostic" ) ) == 1 );
    // Truncation is its own named source, separate from any criterion note.
    REQUIRE( sourceCount( criterion, QStringLiteral( "uncertainty.facts_truncated" ) ) == 1 );
}

TEST_CASE( "invalid-measurement diagnostics are blocking conflicts grading unsuitable",
           "[suitability][uncertainty]" )
{
    // A gsd_invalid diagnostic means some scenes' resolution evidence was
    // garbage: a verdict computed alongside it rests on contaminated
    // measurements — a conflict, not a footnote.
    SuitabilityCriterion conflicted = criterionWithId( QStringLiteral( "spatial.resolution" ) );
    conflicted.diagnostics.append( warning( QStringLiteral( "suitability.gsd_invalid" ),
                                            "1 scene(s) carry a non-finite or non-positive GSD" ) );

    const SuitabilityCriterion criterion = assessUncertaintySources( { conflicted }, std::nullopt );
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "count" ) ).toInt() == 1 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_count" ) ).toInt() == 1 );
    REQUIRE( sourceCount( criterion, QStringLiteral( "uncertainty.measurement_conflict" ) ) == 1 );

    // An unrepresentable AOI ratio is equally blocking.
    SuitabilityCriterion unrepresentable = criterionWithId( QStringLiteral( "spatial.coverage" ) );
    unrepresentable.diagnostics.append(
        warning( QStringLiteral( "suitability.aoi_area_not_representable" ),
                 "AOI area overflowed the double range" ) );
    const SuitabilityCriterion blocked =
        assessUncertaintySources( { unrepresentable }, std::nullopt );
    REQUIRE( blocked.level == SuitabilityLevel::Unsuitable );
    REQUIRE( blocked.evidence.value( QStringLiteral( "blocking_count" ) ).toInt() == 1 );

    // Blocking dominates any amount of ordinary uncertainty.
    SuitabilityCriterion noted = criterionWithId( QStringLiteral( "temporal.coverage" ) );
    noted.notes.append( QStringLiteral( "no time window" ) );
    const SuitabilityCriterion mixed = assessUncertaintySources( { noted, conflicted }, std::nullopt );
    REQUIRE( mixed.level == SuitabilityLevel::Unsuitable );
    REQUIRE( mixed.evidence.value( QStringLiteral( "count" ) ).toInt() == 2 );
    REQUIRE( mixed.evidence.value( QStringLiteral( "blocking_count" ) ).toInt() == 1 );
}

// ---------------------------------------------------------------------------
// 2. Determinism: sorted by code, details deduplicated.
// ---------------------------------------------------------------------------

TEST_CASE( "sources are ordered by code and duplicate details collapse",
           "[suitability][uncertainty]" )
{
    // Two criteria carrying the IDENTICAL note text: the statement is one
    // finding, not two (same rule as report.allGaps duplicate collapse).
    SuitabilityCriterion first = criterionWithId( QStringLiteral( "temporal.coverage" ) );
    first.notes.append( QStringLiteral( "no time window" ) );
    SuitabilityCriterion second = criterionWithId( QStringLiteral( "temporal.density" ) );
    second.notes.append( QStringLiteral( "no time window" ) );
    // The note-source code carries the criterion id, so these two stay
    // distinct; a repeated note on ONE criterion is what collapses.
    first.notes.append( QStringLiteral( "no time window" ) );

    const SuitabilityCriterion criterion =
        assessUncertaintySources( { first, second }, std::nullopt );
    // 1 (coverage) + 1 (coverage duplicate collapsed) ... = coverage has one
    // deduplicated detail, density has its own code entry.
    REQUIRE( criterion.evidence.value( QStringLiteral( "count" ) ).toInt() == 2 );
    REQUIRE( sourceCount( criterion, QStringLiteral( "uncertainty.note.temporal.coverage" ) ) == 1 );
    REQUIRE( sourceCount( criterion, QStringLiteral( "uncertainty.note.temporal.density" ) ) == 1 );

    // The serialized array is sorted by code then detail — deterministic for
    // identical evidence regardless of evaluation order.
    const QJsonArray sources = criterion.evidence.value( QStringLiteral( "sources" ) ).toArray();
    REQUIRE( sources.size() == 2 );
    REQUIRE( sources.at( 0 ).toObject().value( QStringLiteral( "code" ) ).toString()
             < sources.at( 1 ).toObject().value( QStringLiteral( "code" ) ).toString() );
}

// ---------------------------------------------------------------------------
// 3. Assessor integration: the roll-up participates in the overall grade.
// ---------------------------------------------------------------------------

TEST_CASE( "an all-suitable subject with one undigested assumption reads marginal overall",
           "[suitability][uncertainty]" )
{
    // The untruncated twin: fully answered, no notes anywhere -> Suitable.
    SuitabilityAssessor::Inputs cleanInputs;
    cleanInputs.goal = perfectGoal();
    cleanInputs.facts = perfectFacts();
    cleanInputs.scenes = { baseScene( QStringLiteral( "a" ) ), baseScene( QStringLiteral( "b" ) ),
                           baseScene( QStringLiteral( "c" ) ) };
    const auto clean = SuitabilityAssessor::assess( cleanInputs );
    REQUIRE( clean.has_value() );
    REQUIRE( clean->criteria().size() == 11 );
    const SuitabilityCriterion *cleanUncertainty =
        findCriterion( *clean, QStringLiteral( "uncertainty.sources" ) );
    REQUIRE( cleanUncertainty != nullptr );
    REQUIRE( cleanUncertainty->level == SuitabilityLevel::Suitable );
    REQUIRE( clean->overallLevel() == SuitabilityLevel::Suitable );

    // One extra scene in a DIFFERENT CRS: coverage still reaches its fraction
    // (the same-CRS scenes cover the AOI), so every criterion is Suitable or
    // not-applicable — but the assessor wrote down an assumption it did not
    // resolve ("scene excluded, no reprojection"). That assumption previously
    // drowned in an all-Suitable aggregate; it now holds the overall at
    // Marginal. Deliberate semantics: an undigested assumption does not earn
    // a clean Suitable.
    SuitabilityAssessor::Inputs inputs = cleanInputs;
    SceneCandidate foreign = baseScene( QStringLiteral( "foreign" ) );
    foreign.crsWkt = QStringLiteral( "some-other-crs" );
    inputs.scenes.prepend( foreign );

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *coverage =
        findCriterion( *result, QStringLiteral( "spatial.coverage" ) );
    REQUIRE( coverage != nullptr );
    REQUIRE( coverage->level == SuitabilityLevel::Suitable );
    REQUIRE( !coverage->notes.isEmpty() );

    const SuitabilityCriterion *uncertainty =
        findCriterion( *result, QStringLiteral( "uncertainty.sources" ) );
    REQUIRE( uncertainty != nullptr );
    REQUIRE( uncertainty->level == SuitabilityLevel::Marginal );
    REQUIRE( sourceCount( *uncertainty, QStringLiteral( "uncertainty.note.spatial.coverage" ) )
             == 1 );
    // The headline: the delta between the two subjects is the note alone.
    REQUIRE( result->overallLevel() == SuitabilityLevel::Marginal );

    // The uncertainty criterion round-trips through the report JSON with its
    // sources intact (the agent surface reads this list).
    const QJsonObject criterionJson = uncertainty->toJson();
    const auto parsed = SuitabilityCriterion::fromJson( criterionJson );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->evidence.value( QStringLiteral( "sources" ) ).toArray().size()
             == uncertainty->evidence.value( QStringLiteral( "sources" ) ).toArray().size() );
}

TEST_CASE( "blocking conflicts inside an assessment hold the overall at unsuitable",
           "[suitability][uncertainty]" )
{
    // A NaN GSD is a broken measurement: the resolution criterion grades on
    // the remaining valid scenes, but the conflict is blocking — the report
    // must not present the subject as merely marginal.
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = perfectGoal();
    inputs.facts = perfectFacts();
    inputs.scenes = { baseScene( QStringLiteral( "a" ) ), baseScene( QStringLiteral( "b" ) ),
                      baseScene( QStringLiteral( "c" ) ) };
    for ( SceneCandidate &scene : inputs.scenes )
        scene.gsdM = std::numeric_limits< double >::quiet_NaN();

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *uncertainty =
        findCriterion( *result, QStringLiteral( "uncertainty.sources" ) );
    REQUIRE( uncertainty != nullptr );
    REQUIRE( uncertainty->level == SuitabilityLevel::Unsuitable );
    REQUIRE( uncertainty->evidence.value( QStringLiteral( "blocking_count" ) ).toInt() == 1 );
    REQUIRE( result->overallLevel() == SuitabilityLevel::Unsuitable );
}
