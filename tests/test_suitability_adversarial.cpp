// test_suitability_adversarial.cpp — Slice G: adversarial oracles for the
// suitability assessor.
//
// The job of this suite is to make "fake green" impossible:
//   - missing metadata must surface as Unknown (never laundered into a
//     Suitable verdict by aggregation or by not-applicable criteria);
//   - truncation must be VISIBLE and must degrade any verdict that rests on
//     the truncated evidence;
//   - broken numbers (NaN/inf/negative GSD, NaN cloud, unrepresentable AOI
//     ratios) are unknown evidence or typed goal failures — never passes;
//   - hostile JSON (absurd magnitudes, garbage objects, duplicates) fails
//     typed or round-trips honestly;
//   - identical inputs produce byte-identical reports (replay determinism,
//     including the QHash iteration-order trap);
//   - grid pair sampling is deterministic and its linear-pair decode is
//     cross-checked against a brute-force enumeration.

#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/sample.h"
#include "suitability/criteria_grid.h"
#include "suitability/criteria_labels.h"
#include "suitability/criteria_model.h"
#include "suitability/criteria_spatial.h"
#include "suitability/criteria_spectral.h"
#include "suitability/dataset_facts.h"
#include "suitability/scene_candidate.h"
#include "suitability/suitability_assessor.h"
#include "suitability/suitability_provider.h"
#include "suitability/suitability_goal.h"
#include "suitability/suitability_profiles.h"
#include "suitability/suitability_report.h"
#include "suitability/suitability_types.h"
#include "suitability/store_data_provider.h"

#include "data/data_asset.h"
#include "data/raster_grid_compat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
#include <QJsonValue>

using sicnu::data::AssetState;
using sicnu::data::SpatialExtent;
using sicnu::dataset::DatasetId;
using sicnu::dataset::DatasetManifest;
using sicnu::dataset::DatasetStore;
using sicnu::dataset::DatasetVersionId;
using sicnu::dataset::PointSample;
using sicnu::dataset::SampleId;
using sicnu::dataset::SampleKind;
using sicnu::dataset::SampleRecord;
using sicnu::suitability::DatasetFacts;
using sicnu::suitability::FactsLimits;
using sicnu::suitability::InMemoryDataProvider;
using sicnu::suitability::kMaxGridPairs;
using sicnu::suitability::ResolvedRequirements;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::StoreDataProvider;
using sicnu::suitability::SuitabilityAssessor;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGap;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::SuitabilityReport;
using sicnu::suitability::assessGridCompatibility;
using sicnu::suitability::assessLabelAvailability;
using sicnu::suitability::assessModelCompatibility;
using sicnu::suitability::assessResolution;
using sicnu::suitability::assessSpatialCoverage;
using sicnu::suitability::builtinProfile;
using sicnu::suitability::builtinProfileKeys;
using sicnu::suitability::resolveRequirements;
using sicnu::suitability::suitabilitySeverityRank;

namespace
{

SpatialExtent extent( double minX, double minY, double maxX, double maxY )
{
    return SpatialExtent{ minX, minY, maxX, maxY, true };
}

SceneCandidate baseScene( const QString &id )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = AssetState::Ready;
    scene.crsWkt = QStringLiteral( "crs-1" );
    scene.extent = extent( 0.0, 0.0, 100.0, 100.0 );
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
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::Classification;
    goal.hasAoi = true;
    goal.aoi = extent( 0.0, 0.0, 100.0, 100.0 );
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

DatasetFacts perfectFacts( bool truncated = false )
{
    DatasetFacts facts;
    facts.datasetVersionId = QStringLiteral( "dv-perfect" );
    facts.factsTruncated = truncated;
    facts.sampleCount = 300;
    facts.pseudoLabelCount = 0;
    facts.hasLabelSchema = true;
    facts.labelClasses = { QStringLiteral( "forest" ), QStringLiteral( "water" ) };
    // Insertion order is deliberately shuffled between call sites in the
    // determinism tests — serialization must sort it away.
    facts.samplesByClass = { { QStringLiteral( "forest" ), 250 },
                             { QStringLiteral( "water" ), 50 } };
    facts.samplesBySeason = { { QStringLiteral( "spring" ), 300 } };
    facts.samplesByYear = { { QStringLiteral( "2024" ), 300 } };
    return facts;
}

QVector<SceneCandidate> perfectScenes( int count = 3 )
{
    QVector<SceneCandidate> scenes;
    for ( int i = 0; i < count; ++i )
        scenes.append( baseScene( QStringLiteral( "s%1" ).arg( i ) ) );
    return scenes;
}

SuitabilityAssessor::Inputs perfectInputs()
{
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = perfectGoal();
    inputs.scenes = perfectScenes();
    inputs.facts = perfectFacts();
    return inputs;
}

const SuitabilityCriterion *findCriterion( const SuitabilityReport &report, const QString &id )
{
    for ( const SuitabilityCriterion &criterion : report.criteria() )
        if ( criterion.id == id )
            return &criterion;
    return nullptr;
}

bool hasGap( const SuitabilityReport &report, const QString &id )
{
    for ( const SuitabilityGap &gap : report.allGaps() )
        if ( gap.id == id )
            return true;
    return false;
}

QByteArray canonicalJson( const SuitabilityReport &report )
{
    return QJsonDocument( report.toJson() ).toJson( QJsonDocument::Compact );
}

bool containsNonFiniteNumber( const QJsonObject &object )
{
    bool found = false;
    std::function< void( const QJsonValue & ) > walk = [ & ]( const QJsonValue &value )
    {
        if ( value.isDouble() )
        {
            if ( !qIsFinite( value.toDouble() ) )
                found = true;
        }
        else if ( value.isObject() )
        {
            const QJsonObject nested = value.toObject();
            for ( auto it = nested.begin(); it != nested.end(); ++it )
                walk( it.value() );
        }
        else if ( value.isArray() )
        {
            const QJsonArray array = value.toArray();
            for ( const QJsonValue &entry : array )
                walk( entry );
        }
    };
    for ( auto it = object.begin(); it != object.end(); ++it )
        walk( it.value() );
    return found;
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

ResolvedRequirements resolved( const SuitabilityGoal &goal )
{
    const auto requirements = resolveRequirements( goal );
    REQUIRE( requirements.has_value() );
    return requirements.value();
}

/// The published equal-stride linear-pair decode (criteria_grid.cpp): a
/// linear index in [0, n*(n-1)/2) decodes to one (i, j) pair, i < j.
std::pair< qint64, qint64 > decodeLinearPair( qint64 linear, qint64 sceneCount )
{
    qint64 pairI = 0;
    qint64 rest = linear;
    while ( rest >= sceneCount - pairI - 1 )
    {
        rest -= sceneCount - pairI - 1;
        ++pairI;
    }
    return { pairI, pairI + 1 + rest };
}

struct StoreFixture
{
    QTemporaryDir dir;
    DatasetStore store;
    DatasetId datasetId = DatasetId::generate();
    DatasetVersionId versionId = DatasetVersionId::generate();

    StoreFixture()
    {
        REQUIRE( store.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
        REQUIRE( store.createDataset( datasetId, QStringLiteral( "suitability-adv" ) ).has_value() );

        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( versionId.toString() );
        manifest.setName( QStringLiteral( "Adversarial Store Fixture" ) );
        manifest.setCreatedAtUtc( QDateTime::fromString(
            QStringLiteral( "2026-09-21T00:00:00.000Z" ), Qt::ISODateWithMs ) );
        manifest.labelSchema().schemaId = QStringLiteral( "schema-adv" );
        manifest.labelSchema().version = 1;
        REQUIRE( store.createDraftVersion( manifest ).has_value() );
    }

    void addSample( bool withTime, const QVector< QPair< QString, QString > > &facets )
    {
        SampleRecord sample;
        sample.setSampleId( SampleId::generate().toString() );
        sample.setDatasetVersionId( versionId.toString() );
        sample.setKind( SampleKind::Point );
        if ( withTime )
            sample.setTimeUtc( QDateTime::fromString(
                QStringLiteral( "2024-05-01T10:00:00Z" ), Qt::ISODate ) );
        PointSample point;
        point.x = 0.0;
        point.y = 0.0;
        sample.payload() = point;
        REQUIRE( store.addSamples( { sample } ).has_value() );
        if ( !facets.isEmpty() )
        {
            REQUIRE( store.setSampleFacets( versionId,
                                            SampleId::fromString( sample.sampleId() ).value(),
                                            facets )
                         .has_value() );
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------
// 1. Missing-metadata matrix: every dropped field keeps overall < Suitable.
// ---------------------------------------------------------------------------

TEST_CASE( "missing metadata matrix: each dropped field keeps the overall below suitable",
           "[suitability][adversarial]" )
{
    // The fully-answered subject is Suitable end to end.
    {
        const auto baseline = SuitabilityAssessor::assess( perfectInputs() );
        REQUIRE( baseline.has_value() );
        REQUIRE( baseline->overallLevel() == SuitabilityLevel::Suitable );
    }

    auto knockedOut = []( const std::function< void( SuitabilityAssessor::Inputs & ) > &mutation,
                          const QString &criterionId, SuitabilityLevel expectedLevel )
    {
        SuitabilityAssessor::Inputs inputs = perfectInputs();
        mutation( inputs );
        const auto result = SuitabilityAssessor::assess( inputs );
        REQUIRE( result.has_value() );
        const SuitabilityCriterion *criterion = findCriterion( *result, criterionId );
        REQUIRE( criterion != nullptr );
        REQUIRE( criterion->level == expectedLevel );
        // The headline invariant: no missing-metadata input may reach the
        // report as "suitable".
        REQUIRE( suitabilitySeverityRank( result->overallLevel() )
                 > suitabilitySeverityRank( SuitabilityLevel::Suitable ) );
        return result;
    };

    SECTION( "no scene gsd -> resolution unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                for ( SceneCandidate &scene : inputs.scenes )
                    scene.gsdM.reset();
            },
            QStringLiteral( "spatial.resolution" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no scene cloud -> quality.cloud unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                for ( SceneCandidate &scene : inputs.scenes )
                    scene.cloudCoverPercent.reset();
            },
            QStringLiteral( "quality.cloud" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no band roles anywhere -> spectral.bands unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                for ( SceneCandidate &scene : inputs.scenes )
                    scene.bandRoles.clear();
                inputs.facts->bandRoles.clear();
            },
            QStringLiteral( "spectral.bands" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no scene times -> temporal.coverage unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                for ( SceneCandidate &scene : inputs.scenes )
                    scene.acquisitionTimeUtc.reset();
            },
            QStringLiteral( "temporal.coverage" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no goal window -> temporal.coverage unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                inputs.goal.hasTimeWindow = false;
            },
            QStringLiteral( "temporal.coverage" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no grid snapshots (multiple usable scenes) -> grid unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                for ( SceneCandidate &scene : inputs.scenes )
                    scene.grid.reset();
            },
            QStringLiteral( "grid.compatibility" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no scene CRS -> coverage unknown (all scenes excluded)" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                for ( SceneCandidate &scene : inputs.scenes )
                    scene.crsWkt.clear();
            },
            QStringLiteral( "spatial.coverage" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no scene extents -> coverage unsuitable (no usable georef scene)" )
    {
        const auto result = knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                for ( SceneCandidate &scene : inputs.scenes )
                    scene.extent.valid = false;
            },
            QStringLiteral( "spatial.coverage" ), SuitabilityLevel::Unsuitable );
        REQUIRE( hasGap( *result, QStringLiteral( "coverage.no_usable_scene" ) ) );
    }
    SECTION( "no goal AOI -> coverage unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                inputs.goal.hasAoi = false;
            },
            QStringLiteral( "spatial.coverage" ), SuitabilityLevel::Unknown );
    }
    SECTION( "no facts -> labels unknown" )
    {
        knockedOut(
            []( SuitabilityAssessor::Inputs &inputs )
            {
                inputs.facts.reset();
            },
            QStringLiteral( "labels.availability" ), SuitabilityLevel::Unknown );
    }
}

// ---------------------------------------------------------------------------
// 2. "Nothing required" must never wash the overall clean.
// ---------------------------------------------------------------------------

TEST_CASE( "an empty-requirement goal never launders the overall to suitable",
           "[suitability][adversarial]" )
{
    // spectral_matching pins no labels, samples, seasons or density: most
    // criteria go not-applicable. The overall still cannot be Suitable,
    // because spatial.coverage and temporal.coverage are unconditionally
    // applicable and degrade to Unknown when their goal-side input (AOI,
    // window) is missing. "Nothing was asked" is Unknown, not a pass.
    SuitabilityAssessor::Inputs inputs;
    inputs.goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::SpectralMatching;
    inputs.scenes = perfectScenes();
    inputs.facts = perfectFacts();

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    // Slice H semantics (deliberate change, stance preserved and sharpened):
    // before the uncertainty roll-up this subject graded overall Unknown —
    // "nothing was asked, nothing asserted". Now spatial.coverage's "AOI not
    // specified" and temporal.coverage's "no time window" notes are listed as
    // non-blocking uncertainty sources, so uncertainty.sources grades
    // Marginal, and Marginal outranks Unknown in the severity lattice. A
    // subject with undigested assumptions reads Marginal, never a clean
    // Suitable — the fail-closed posture, one step stronger.
    REQUIRE( result->overallLevel() == SuitabilityLevel::Marginal );

    // The washout is prevented by the two unconditionally-applicable
    // criteria: spatial.coverage (no AOI) and temporal.coverage (no window)
    // both resolve to Unknown, and the uncertainty roll-up carries their
    // notes as non-blocking sources.
    const SuitabilityCriterion *coverage = findCriterion( *result, QStringLiteral( "spatial.coverage" ) );
    const SuitabilityCriterion *temporal = findCriterion( *result, QStringLiteral( "temporal.coverage" ) );
    const SuitabilityCriterion *uncertainty =
        findCriterion( *result, QStringLiteral( "uncertainty.sources" ) );
    REQUIRE( coverage != nullptr );
    REQUIRE( coverage->applicable );
    REQUIRE( coverage->level == SuitabilityLevel::Unknown );
    REQUIRE( temporal != nullptr );
    REQUIRE( temporal->applicable );
    REQUIRE( temporal->level == SuitabilityLevel::Unknown );
    REQUIRE( uncertainty != nullptr );
    REQUIRE( uncertainty->applicable );
    REQUIRE( uncertainty->level == SuitabilityLevel::Marginal );

    // Everything the goal declined is honestly not-applicable (and fully
    // measured criteria like grid.compatibility may still be Suitable —
    // they do not wash the overall because the aggregate includes Unknown).
    int notApplicable = 0;
    for ( const SuitabilityCriterion &criterion : result->criteria() )
        if ( !criterion.applicable )
            ++notApplicable;
    REQUIRE( notApplicable > 0 );

    // Report level: an all-not-applicable report claims nothing — the
    // aggregate of an empty applicable set is Unknown ("what was not asked
    // cannot be asserted"), which is exactly the fail-closed posture.
    SuitabilityReport allNotApplicable;
    for ( const char *id : { "spatial.coverage", "temporal.coverage", "labels.availability" } )
    {
        SuitabilityCriterion criterion;
        criterion.id = QString::fromLatin1( id );
        criterion.applicable = false;
        criterion.level = SuitabilityLevel::Suitable;
        allNotApplicable.addCriterion( criterion );
    }
    REQUIRE( allNotApplicable.overallLevel() == SuitabilityLevel::Unknown );
}

// ---------------------------------------------------------------------------
// 3. Truncation honesty.
// ---------------------------------------------------------------------------

TEST_CASE( "facts truncation degrades a passing label verdict to marginal and stays visible",
           "[suitability][adversarial]" )
{
    SuitabilityAssessor::Inputs inputs = perfectInputs();
    inputs.facts = perfectFacts( /*truncated*/ true );

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *labels = findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    // The label requirement passed on partial evidence: Marginal, never a
    // clean pass.
    REQUIRE( labels->level == SuitabilityLevel::Marginal );
    REQUIRE( labels->evidence.value( QStringLiteral( "facts_truncated" ) ).toBool() );
    REQUIRE( labels->evidence.value( QStringLiteral( "truncation_downgraded" ) ).toBool() );
    REQUIRE( labels->notes.join( QLatin1String( " " ) ).contains( QStringLiteral( "truncated" ) ) );
    REQUIRE( result->overallLevel() == SuitabilityLevel::Marginal );

    // The untruncated twin of the same subject is a full pass: the delta is
    // attributable to the truncation alone.
    const auto clean = SuitabilityAssessor::assess( perfectInputs() );
    REQUIRE( clean.has_value() );
    REQUIRE( clean->overallLevel() == SuitabilityLevel::Suitable );
}

TEST_CASE( "facts truncation stays visible in the report when labels are not applicable",
           "[suitability][adversarial]" )
{
    SuitabilityAssessor::Inputs inputs = perfectInputs();
    inputs.goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction;
    inputs.goal.requireLabels = false;
    inputs.facts = perfectFacts( /*truncated*/ true );

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *labels = findCriterion( *result, QStringLiteral( "labels.availability" ) );
    REQUIRE( labels != nullptr );
    REQUIRE( !labels->applicable );
    // No verdict rests on the truncated counts here (the other facts
    // consumers read manifest-derived fields or fail in the conservative
    // direction), so the overall may stand — but the truncation must be
    // readable from the serialized report, not swallowed.
    REQUIRE( labels->evidence.value( QStringLiteral( "facts_truncated" ) ).toBool() );
    REQUIRE( labels->notes.join( QLatin1String( " " ) ).contains( QStringLiteral( "truncated" ) ) );
    REQUIRE( canonicalJson( *result ).contains( "truncated" ) );
}

TEST_CASE( "store provider reports a folded facet tail even when the watched value survived",
           "[suitability][adversarial][store]" )
{
    // Six label_source values where "pseudo" dominates: with a 1-value cap
    // the store folds "annotated" into "(other)" while "pseudo" survives.
    // The partial fold is still a cap touch — it must set factsTruncated
    // (the pre-fix behavior presented the count as complete, silently).
    StoreFixture fixture;
    for ( int i = 0; i < 5; ++i )
        fixture.addSample( true, { { QStringLiteral( "label_source" ), QStringLiteral( "pseudo" ) } } );
    fixture.addSample( true, { { QStringLiteral( "label_source" ), QStringLiteral( "annotated" ) } } );

    StoreDataProvider provider( &fixture.store );
    FactsLimits limits;
    limits.maxClassValues = 1;
    const auto result = provider.datasetFacts( fixture.versionId.toString(), limits );
    REQUIRE( result.has_value() );
    const DatasetFacts facts = result.value();
    REQUIRE( facts.factsTruncated );
    REQUIRE( facts.pseudoLabelCount == 5 );

    // Same honesty for a folded missing_time tail with a seen truthy value.
    StoreFixture missingFixture;
    for ( int i = 0; i < 3; ++i )
        missingFixture.addSample( false, { { QStringLiteral( "missing_time" ), QStringLiteral( "true" ) } } );
    missingFixture.addSample( false, { { QStringLiteral( "missing_time" ), QStringLiteral( "false" ) } } );

    StoreDataProvider missingProvider( &missingFixture.store );
    const auto missing = missingProvider.datasetFacts( missingFixture.versionId.toString(), limits );
    REQUIRE( missing.has_value() );
    REQUIRE( missing->factsTruncated );
    // The visible truthy bucket ("true": 3) is reported; the folded tail
    // ("false": 1) is only announced by the truncation flag.
    REQUIRE( missing->missingTimeCount == 3 );
}

// ---------------------------------------------------------------------------
// 4. Broken GSD numbers are unknown evidence, never passes.
// ---------------------------------------------------------------------------

TEST_CASE( "non-finite and non-positive gsd values are unknown evidence, never in range",
           "[suitability][adversarial]" )
{
    auto goal = perfectGoal();
    const auto req = resolved( goal );

    auto resolutionFor = [ &req ]( double gsd )
    {
        SceneCandidate scene = baseScene( QStringLiteral( "x" ) );
        scene.gsdM = gsd;
        return assessResolution( req, { scene } );
    };

    // NaN satisfies no comparison, so pre-fix it passed every range check
    // and graded Suitable. It is broken metadata -> unknown + diagnostic.
    {
        const SuitabilityCriterion nanScene = resolutionFor( std::numeric_limits< double >::quiet_NaN() );
        REQUIRE( nanScene.level == SuitabilityLevel::Unknown );
        REQUIRE( nanScene.evidence.value( QStringLiteral( "gsd_invalid_count" ) ).toInt() == 1 );
        REQUIRE( !nanScene.diagnostics.isEmpty() );
        REQUIRE( nanScene.diagnostics.first().code == QStringLiteral( "suitability.gsd_invalid" ) );
        REQUIRE( !containsNonFiniteNumber( nanScene.evidence ) );
    }
    // Infinity with no max limit: same broken-metadata path.
    {
        goal.maxGsdM = 0.0; // min-only requirement
        const auto minOnly = resolved( goal );
        const SuitabilityCriterion infScene =
            assessResolution( minOnly, { []( SceneCandidate s )
                                         { s.gsdM = std::numeric_limits< double >::infinity(); return s; }( baseScene( QStringLiteral( "x" ) ) ) } );
        REQUIRE( infScene.level == SuitabilityLevel::Unknown );
        REQUIRE( infScene.evidence.value( QStringLiteral( "gsd_invalid_count" ) ).toInt() == 1 );
    }
    // Negative GSD is physically meaningless: unknown, never in range.
    {
        const SuitabilityCriterion negative = resolutionFor( -3.0 );
        REQUIRE( negative.level == SuitabilityLevel::Unknown );
        REQUIRE( negative.evidence.value( QStringLiteral( "gsd_invalid_count" ) ).toInt() == 1 );
    }
    // A mixed set grades on the valid scenes but keeps the broken ones on
    // the record (diagnostic + evidence count).
    {
        SceneCandidate broken = baseScene( QStringLiteral( "broken" ) );
        broken.gsdM = std::numeric_limits< double >::quiet_NaN();
        const SuitabilityCriterion mixed = assessResolution( req, { broken, baseScene( QStringLiteral( "ok" ) ) } );
        REQUIRE( mixed.level == SuitabilityLevel::Suitable );
        REQUIRE( mixed.evidence.value( QStringLiteral( "gsd_invalid_count" ) ).toInt() == 1 );
        REQUIRE( mixed.evidence.value( QStringLiteral( "gsd_unknown_count" ) ).toInt() == 1 );
        REQUIRE( !mixed.diagnostics.isEmpty() );
        REQUIRE( qIsFinite( mixed.evidence.value( QStringLiteral( "measured_min_gsd_m" ) ).toDouble() ) );
        REQUIRE( qIsFinite( mixed.evidence.value( QStringLiteral( "measured_max_gsd_m" ) ).toDouble() ) );
    }
    // End to end: a fully-NaN subject must not reach the report as suitable.
    {
        SuitabilityAssessor::Inputs inputs = perfectInputs();
        for ( SceneCandidate &scene : inputs.scenes )
            scene.gsdM = std::numeric_limits< double >::quiet_NaN();
        const auto result = SuitabilityAssessor::assess( inputs );
        REQUIRE( result.has_value() );
        REQUIRE( suitabilitySeverityRank( result->overallLevel() )
                 > suitabilitySeverityRank( SuitabilityLevel::Suitable ) );
    }
}

TEST_CASE( "a model-pinned gsd range with only broken gsd evidence stays unknown",
           "[suitability][adversarial]" )
{
    SuitabilityGoal goal;
    goal.hasModel = true;
    goal.modelMinGsdM = 1.0;
    goal.modelMaxGsdM = 10.0;
    const auto req = resolved( goal );

    // Without the fix, NaN entered the measured pool, satisfied every
    // comparison and graded the model fit Suitable.
    SceneCandidate scene = baseScene( QStringLiteral( "nan" ) );
    scene.gsdM = std::numeric_limits< double >::quiet_NaN();
    const SuitabilityCriterion criterion = assessModelCompatibility( req, { scene }, std::nullopt );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.evidence.value( QStringLiteral( "gsd_invalid_count" ) ).toInt() == 1 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) ).toString()
             == QStringLiteral( "unknown" ) );

    // A pinned GSD range with NO gsd evidence at all is equally unverifiable.
    SceneCandidate gsdLess = baseScene( QStringLiteral( "no-gsd" ) );
    gsdLess.gsdM.reset();
    const SuitabilityCriterion absent = assessModelCompatibility( req, { gsdLess }, std::nullopt );
    REQUIRE( absent.level == SuitabilityLevel::Unknown );
    REQUIRE( absent.evidence.value( QStringLiteral( "reason" ) ).toString()
             == QStringLiteral( "gsd_evidence_absent" ) );
}

// ---------------------------------------------------------------------------
// 5. Broken cloud numbers.
// ---------------------------------------------------------------------------

TEST_CASE( "nan cloud cover is unknown evidence with a diagnostic, never a verdict input",
           "[suitability][adversarial]" )
{
    auto goal = perfectGoal();
    const auto req = resolved( goal );

    // NaN alone: pre-fix it counted as "known but not clear" -> Unsuitable.
    // It is broken metadata -> Unknown.
    {
        SceneCandidate scene = baseScene( QStringLiteral( "nan" ) );
        scene.cloudCoverPercent = std::numeric_limits< double >::quiet_NaN();
        const auto criterion =
            sicnu::suitability::assessCloudCover( req, { scene } );
        REQUIRE( criterion.level == SuitabilityLevel::Unknown );
        REQUIRE( criterion.evidence.value( QStringLiteral( "unknown_count" ) ).toInt() == 1 );
        REQUIRE( !criterion.diagnostics.isEmpty() );
        REQUIRE( criterion.diagnostics.first().code
                 == QStringLiteral( "suitability.cloud_out_of_range" ) );
        REQUIRE( !containsNonFiniteNumber( criterion.evidence ) );
    }
    // Mixed: valid scenes grade, the NaN stays on the record, evidence
    // stays finite.
    {
        SceneCandidate broken = baseScene( QStringLiteral( "nan" ) );
        broken.cloudCoverPercent = std::numeric_limits< double >::quiet_NaN();
        const auto criterion =
            sicnu::suitability::assessCloudCover( req, { broken, baseScene( QStringLiteral( "ok" ) ) } );
        REQUIRE( criterion.level == SuitabilityLevel::Suitable );
        REQUIRE( criterion.evidence.value( QStringLiteral( "unknown_count" ) ).toInt() == 1 );
        REQUIRE( !criterion.diagnostics.isEmpty() );
        REQUIRE( qIsFinite(
            criterion.evidence.value( QStringLiteral( "worst_cloud_cover_percent" ) ).toDouble() ) );
    }
}

// ---------------------------------------------------------------------------
// 6. Extreme AOI geometry.
// ---------------------------------------------------------------------------

TEST_CASE( "an unrepresentable coverage ratio is honest unknown, never a garbage verdict",
           "[suitability][adversarial]" )
{
    auto goal = perfectGoal();
    goal.minCoverageFraction = 0.5;

    auto coverageFor = [ &goal ]( double side )
    {
        SuitabilityGoal local = goal;
        local.aoi = extent( 0.0, 0.0, side, side );
        SceneCandidate scene = baseScene( QStringLiteral( "cover" ) );
        scene.extent = extent( 0.0, 0.0, side, side ); // fully covers the AOI
        return assessSpatialCoverage( resolved( local ), { scene } );
    };

    // 1e-300 m AOI: the area underflows to 0 and the ratio degenerates
    // (pre-fix: 0/0 = NaN graded Unsuitable with NaN evidence, and a fully
    // covered tiny AOI could read as coverage 0). Refusal is the honest
    // answer at this scale.
    {
        const SuitabilityCriterion tiny = coverageFor( 1e-300 );
        REQUIRE( tiny.level == SuitabilityLevel::Unknown );
        REQUIRE( tiny.evidence.value( QStringLiteral( "reason" ) ).toString()
                 == QStringLiteral( "aoi_area_not_representable" ) );
        REQUIRE( !tiny.diagnostics.isEmpty() );
        REQUIRE( !containsNonFiniteNumber( tiny.evidence ) );
    }
    // 1e300 m sides: the area overflows to inf; inf/inf is not a measurement.
    {
        const SuitabilityCriterion huge = coverageFor( 1e300 );
        REQUIRE( huge.level == SuitabilityLevel::Unknown );
        REQUIRE( huge.evidence.value( QStringLiteral( "reason" ) ).toString()
                 == QStringLiteral( "aoi_area_not_representable" ) );
        REQUIRE( !containsNonFiniteNumber( huge.evidence ) );
    }
    // Ordinary scales are untouched: a fully covered AOI is Suitable with a
    // finite fraction.
    {
        const SuitabilityCriterion normal = coverageFor( 100.0 );
        REQUIRE( normal.level == SuitabilityLevel::Suitable );
        REQUIRE( normal.evidence.value( QStringLiteral( "measured_fraction" ) ).toDouble() == 1.0 );
    }
}

TEST_CASE( "nan scene extent corners fold into unknown extents without corrupting the union",
           "[suitability][adversarial]" )
{
    auto goal = perfectGoal();
    SceneCandidate broken = baseScene( QStringLiteral( "nan-extent" ) );
    broken.extent = SpatialExtent{ std::numeric_limits< double >::quiet_NaN(),
                                   std::numeric_limits< double >::quiet_NaN(),
                                   std::numeric_limits< double >::quiet_NaN(),
                                   std::numeric_limits< double >::quiet_NaN(), true };

    // Pre-fix the NaN corners reached the coordinate sort (undefined
    // comparator) and the fraction. Post-fix they are unknown extents.
    const SuitabilityCriterion criterion = assessSpatialCoverage( resolved( goal ), { broken } );
    REQUIRE( criterion.evidence.value( QStringLiteral( "extent_unknown_count" ) ).toInt() == 1 );
    REQUIRE( !containsNonFiniteNumber( criterion.evidence ) );
    // With no usable geo-referenced scene the locked verdict is Unsuitable
    // (a goal with a place to cover and no data is infeasible), with clean
    // finite evidence and the canonical gap.
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( hasGap( [&criterion]
                     {
                         SuitabilityReport report;
                         report.addCriterion( criterion );
                         return report;
                     }(),
                     QStringLiteral( "coverage.no_usable_scene" ) ) );
}

// ---------------------------------------------------------------------------
// 7. Hostile goal numbers.
// ---------------------------------------------------------------------------

TEST_CASE( "hostile goal magnitudes fail typed, never assess", "[suitability][adversarial]" )
{
    auto expectInvalid = []( const SuitabilityGoal &goal, const char *what )
    {
        INFO( what );
        const auto inMemory = resolveRequirements( goal );
        REQUIRE( !inMemory.has_value() );
        REQUIRE( inMemory.diagnostics().first().code == QStringLiteral( "suitability.goal_invalid" ) );
        const auto fromJson = SuitabilityGoal::fromJson( goal.toJson() );
        REQUIRE( !fromJson.has_value() );
        REQUIRE( fromJson.diagnostics().first().code == QStringLiteral( "suitability.goal_invalid" ) );
    };

    SuitabilityGoal nanGsd = perfectGoal();
    nanGsd.minGsdM = std::numeric_limits< double >::quiet_NaN();
    expectInvalid( nanGsd, "NaN gsd" );

    SuitabilityGoal infCloud = perfectGoal();
    infCloud.maxCloudCoverPercent = std::numeric_limits< double >::infinity();
    expectInvalid( infCloud, "inf cloud limit" );

    // The absurd pair from the review brief: a 1e300 floor and a -1e300
    // ceiling. Pre-fix the negative ceiling silently meant "no limit".
    SuitabilityGoal absurd = perfectGoal();
    absurd.minGsdM = 1e300;
    absurd.maxGsdM = -1e300;
    expectInvalid( absurd, "absurd gsd pair" );

    SuitabilityGoal nanCoverage = perfectGoal();
    nanCoverage.minCoverageFraction = std::numeric_limits< double >::quiet_NaN();
    expectInvalid( nanCoverage, "NaN coverage fraction" );
}

TEST_CASE( "unrepresentable integral goal fields from json fail typed instead of undefined casts",
           "[suitability][adversarial]" )
{
    auto parse = []( QJsonObject json ) { return SuitabilityGoal::fromJson( json ); };

    // 1e300 does not fit a qint64: pre-fix this was an undefined
    // double->integer cast on the way in.
    QJsonObject hugeSamples{ { QStringLiteral( "schema_version" ), 1 },
                             { QStringLiteral( "min_samples" ), 1e300 } };
    const auto huge = parse( hugeSamples );
    REQUIRE( !huge.has_value() );
    REQUIRE( huge.diagnostics().first().code == QStringLiteral( "suitability.goal_invalid" ) );

    // An infinity (e.g. parsed from 1e999) must die in the same gate.
    QJsonObject infiniteSamples{ { QStringLiteral( "schema_version" ), 1 },
                                 { QStringLiteral( "min_samples" ),
                                   std::numeric_limits< double >::infinity() } };
    const auto infinite = parse( infiniteSamples );
    REQUIRE( !infinite.has_value() );
    REQUIRE( infinite.diagnostics().first().code == QStringLiteral( "suitability.goal_invalid" ) );

    QJsonObject hugeScenes{ { QStringLiteral( "schema_version" ), 1 },
                            { QStringLiteral( "min_scenes_in_window" ), 1e300 } };
    const auto hugeScenesResult = parse( hugeScenes );
    REQUIRE( !hugeScenesResult.has_value() );
    REQUIRE( hugeScenesResult.diagnostics().first().code
             == QStringLiteral( "suitability.goal_invalid" ) );

    // Negative counts stay refused exactly as before.
    QJsonObject negativeSamples{ { QStringLiteral( "schema_version" ), 1 },
                                 { QStringLiteral( "min_samples" ), -5.0 } };
    const auto negative = parse( negativeSamples );
    REQUIRE( !negative.has_value() );
    REQUIRE( negative.diagnostics().first().code == QStringLiteral( "suitability.goal_invalid" ) );
}

TEST_CASE( "a degenerate time window (start == end) and absurd-but-valid numbers grade honestly",
           "[suitability][adversarial]" )
{
    // start == end: an empty window is a malformed goal, already locked —
    // re-asserted here against hostile input.
    SuitabilityGoal emptyWindow = perfectGoal();
    emptyWindow.windowEndUtc = emptyWindow.windowStartUtc;
    const auto refused = resolveRequirements( emptyWindow );
    REQUIRE( !refused.has_value() );
    REQUIRE( refused.diagnostics().first().code == QStringLiteral( "suitability.goal_invalid" ) );

    // A 1e300 m GSD window is absurd but well-formed: the goal resolves and
    // every scene is out of range — Unsuitable, never Suitable.
    SuitabilityGoal absurdButValid = perfectGoal();
    absurdButValid.minGsdM = 1e300;
    absurdButValid.maxGsdM = 1e300;
    const auto result = SuitabilityAssessor::assess( [&absurdButValid]
                                                     {
                                                         SuitabilityAssessor::Inputs inputs;
                                                         inputs.goal = absurdButValid;
                                                         inputs.scenes = perfectScenes();
                                                         inputs.facts = perfectFacts();
                                                         return inputs;
                                                     }() );
    REQUIRE( result.has_value() );
    const SuitabilityCriterion *resolution = findCriterion( *result, QStringLiteral( "spatial.resolution" ) );
    REQUIRE( resolution != nullptr );
    REQUIRE( resolution->level == SuitabilityLevel::Unsuitable );
    REQUIRE( hasGap( *result, QStringLiteral( "resolution.out_of_range" ) ) );
}

// ---------------------------------------------------------------------------
// 8. Hostile JSON into the report parser.
// ---------------------------------------------------------------------------

TEST_CASE( "report fromJson survives garbage: non-object criteria, duplicates and junk evidence",
           "[suitability][adversarial]" )
{
    // A non-object criterion entry fails typed, not by crash.
    QJsonObject garbageCriteria{ { QStringLiteral( "schema_version" ), 1 },
                                 { QStringLiteral( "criteria" ), QJsonArray{ 42, QStringLiteral( "junk" ) } } };
    const auto garbage = SuitabilityReport::fromJson( garbageCriteria );
    REQUIRE( !garbage.has_value() );
    REQUIRE( garbage.diagnostics().first().code == QStringLiteral( "suitability.report_invalid" ) );

    // Duplicate criterion ids: last add wins, one entry remains (documented
    // addCriterion replace semantics, now locked over the wire).
    SuitabilityCriterion first;
    first.id = QStringLiteral( "spatial.coverage" );
    first.level = SuitabilityLevel::Unsuitable;
    SuitabilityCriterion second = first;
    second.level = SuitabilityLevel::Suitable;
    QJsonObject duplicateJson = SuitabilityReport().toJson();
    duplicateJson.insert( QStringLiteral( "criteria" ),
                          QJsonArray{ first.toJson(), second.toJson() } );
    const auto duplicated = SuitabilityReport::fromJson( duplicateJson );
    REQUIRE( duplicated.has_value() );
    REQUIRE( duplicated->criteria().size() == 1 );
    REQUIRE( duplicated->criteria().first().level == SuitabilityLevel::Suitable );
    // ...and the same replace semantics hold in memory.
    SuitabilityReport replaced;
    replaced.addCriterion( first );
    replaced.addCriterion( second );
    REQUIRE( replaced.criteria().size() == 1 );
    REQUIRE( replaced.criteria().first().level == SuitabilityLevel::Suitable );

    // A gap with no id is invalid content: the gap gate says why, the
    // report wrapper carries it as report_invalid with the reason embedded.
    SuitabilityCriterion withGapless;
    withGapless.id = QStringLiteral( "spatial.coverage" );
    SuitabilityGap anonymousGap;
    anonymousGap.description = QStringLiteral( "no id" );
    withGapless.gaps.append( anonymousGap );
    const auto gapDirect = SuitabilityCriterion::fromJson( withGapless.toJson() );
    REQUIRE( !gapDirect.has_value() );
    REQUIRE( gapDirect.diagnostics().first().code == QStringLiteral( "suitability.gap_invalid" ) );

    QJsonObject gaplessJson = SuitabilityReport().toJson();
    gaplessJson.insert( QStringLiteral( "criteria" ), QJsonArray{ withGapless.toJson() } );
    const auto gapless = SuitabilityReport::fromJson( gaplessJson );
    REQUIRE( !gapless.has_value() );
    REQUIRE( gapless.diagnostics().first().code == QStringLiteral( "suitability.report_invalid" ) );

    // Junk nested objects inside evidence round-trip byte-stable: the
    // module never recurses into caller evidence, so hostile nesting is
    // carried as-is (QJsonDocument's own depth cap guards the text layer).
    QJsonObject nested{ { QStringLiteral( "leaf" ), true } };
    for ( int level = 0; level < 200; ++level )
    {
        QJsonObject wrapper;
        wrapper.insert( QStringLiteral( "child" ), nested );
        nested = wrapper;
    }
    QJsonObject evidence;
    evidence.insert( QStringLiteral( "junk" ), QJsonObject{ { QStringLiteral( "a" ),
                                                             QJsonArray{ 1, QStringLiteral( "x" ), QJsonObject{ { QStringLiteral( "b" ), true } } } } } );
    evidence.insert( QStringLiteral( "deep" ), nested );
    SuitabilityCriterion withJunk;
    withJunk.id = QStringLiteral( "spatial.coverage" );
    withJunk.evidence = evidence;
    withJunk.summary = QString( 100000, QLatin1Char( 'x' ) ); // hostile length
    QJsonObject junkReport = SuitabilityReport().toJson();
    junkReport.insert( QStringLiteral( "criteria" ), QJsonArray{ withJunk.toJson() } );
    const auto junkParsed = SuitabilityReport::fromJson( junkReport );
    REQUIRE( junkParsed.has_value() );
    REQUIRE( junkParsed->criteria().first().evidence == evidence );
    REQUIRE( junkParsed->criteria().first().summary.size() == 100000 );
    REQUIRE( QJsonDocument( junkParsed->toJson() ).toJson( QJsonDocument::Compact )
             == QJsonDocument( junkReport ).toJson( QJsonDocument::Compact ) );

    // Syntactically broken JSON never reaches the parser: the document
    // layer refuses it (QJson is the depth/syntax guard; the module adds
    // the typed schema gates on top).
    const auto brokenDocument = QJsonDocument::fromJson(
        QByteArrayLiteral( "{\"schema_version\":1,\"criteria\":[{\"schema_version\":1," ) );
    REQUIRE( brokenDocument.isNull() );
}

TEST_CASE( "duplicate gaps collapse only when they are exact duplicates", "[suitability][adversarial]" )
{
    // Semantics (locked here): a gap is id + criterion + description +
    // evidence. The SAME statement repeated (a repeated required class, a
    // crafted report) collapses; the same id with different evidence stays.
    SuitabilityGoal goal = perfectGoal();
    goal.requiredClasses = { QStringLiteral( "urban" ), QStringLiteral( "urban" ) };
    const auto req = resolved( goal );
    DatasetFacts facts = perfectFacts();
    const SuitabilityCriterion labels = assessLabelAvailability( req, facts );
    REQUIRE( labels.gaps.size() == 1 );
    REQUIRE( labels.gaps.first().id == QStringLiteral( "label.class_missing.urban" ) );

    SuitabilityCriterion a;
    a.id = QStringLiteral( "spectral.bands" );
    SuitabilityGap gap;
    gap.id = QStringLiteral( "band.missing.nir" );
    gap.criterionId = a.id;
    gap.evidence = QJsonObject{ { QStringLiteral( "required_role" ), QStringLiteral( "nir" ) } };
    a.gaps.append( gap );
    a.gaps.append( gap ); // exact duplicate

    // A different criterion carrying the same gap id with DIFFERENT
    // evidence stays its own finding (not folded into the first).
    SuitabilityCriterion b;
    b.id = QStringLiteral( "model.compatibility" );
    SuitabilityGap different;
    different.id = QStringLiteral( "band.missing.nir" );
    different.criterionId = b.id;
    different.evidence = QJsonObject{ { QStringLiteral( "required_role" ), QStringLiteral( "nir" ) },
                                      { QStringLiteral( "source" ), QStringLiteral( "facts" ) } };
    b.gaps.append( different );

    SuitabilityReport report;
    report.addCriterion( a );
    report.addCriterion( b );
    REQUIRE( report.allGaps().size() == 2 );
}

// ---------------------------------------------------------------------------
// 9. Hostile scene identity.
// ---------------------------------------------------------------------------

TEST_CASE( "duplicate and empty scene ids neither crash nor break determinism",
           "[suitability][adversarial]" )
{
    // Semantics (locked here): ids are labels, not keys — the assessor
    // judges data, so duplicates and empty ids pass through untouched
    // (the report mirrors the input id list) and grading stays deterministic.
    SuitabilityAssessor::Inputs inputs = perfectInputs();
    inputs.scenes = { baseScene( QString() ), baseScene( QStringLiteral( "twin" ) ),
                      baseScene( QStringLiteral( "twin" ) ) };

    const auto first = SuitabilityAssessor::assess( inputs );
    const auto second = SuitabilityAssessor::assess( inputs );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    REQUIRE( first->sceneIds() == QStringList{ QString(), QStringLiteral( "twin" ),
                                               QStringLiteral( "twin" ) } );
    REQUIRE( canonicalJson( *first ) == canonicalJson( *second ) );
    REQUIRE( first->contentDigest() == second->contentDigest() );
    // Coverage still grades the rectangles (3 full-cover scenes -> suitable).
    const SuitabilityCriterion *coverage = findCriterion( *first, QStringLiteral( "spatial.coverage" ) );
    REQUIRE( coverage != nullptr );
    REQUIRE( coverage->level == SuitabilityLevel::Suitable );
}

// ---------------------------------------------------------------------------
// 10. Replay determinism (byte-for-byte), including the QHash trap.
// ---------------------------------------------------------------------------

TEST_CASE( "replay determinism: identical inputs produce byte-identical reports",
           "[suitability][adversarial]" )
{
    // Subject 1: the fully-answered facts subject. The two runs build the
    // QHash members in opposite insertion order — serialization must sort
    // that away (QHash iteration order is unspecified).
    auto runFactsSubject = []( bool reverseHashInsertion )
    {
        SuitabilityAssessor::Inputs inputs = perfectInputs();
        if ( reverseHashInsertion )
        {
            DatasetFacts shuffled = inputs.facts.value();
            shuffled.samplesByClass.clear();
            shuffled.samplesByClass.insert( QStringLiteral( "water" ), 50 );
            shuffled.samplesByClass.insert( QStringLiteral( "forest" ), 250 );
            inputs.facts = shuffled;
        }
        const auto result = SuitabilityAssessor::assess( inputs );
        REQUIRE( result.has_value() );
        return result.value();
    };
    const SuitabilityReport forward = runFactsSubject( false );
    const SuitabilityReport backward = runFactsSubject( true );
    REQUIRE( canonicalJson( forward ) == canonicalJson( backward ) );
    REQUIRE( forward.contentDigest() == backward.contentDigest() );

    // Subject 2: a 203-scene set driven into the sampled grid path.
    QVector<SceneCandidate> scenes;
    for ( int i = 0; i < 203; ++i )
    {
        SceneCandidate scene = baseScene( QStringLiteral( "grid-%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ) );
        scenes.append( scene );
    }
    SuitabilityAssessor::Inputs gridInputs;
    gridInputs.goal = perfectGoal();
    gridInputs.scenes = scenes;
    gridInputs.facts = perfectFacts();

    const auto gridFirst = SuitabilityAssessor::assess( gridInputs );
    const auto gridSecond = SuitabilityAssessor::assess( gridInputs );
    REQUIRE( gridFirst.has_value() );
    REQUIRE( gridSecond.has_value() );
    REQUIRE( canonicalJson( *gridFirst ) == canonicalJson( *gridSecond ) );
    REQUIRE( gridFirst->contentDigest() == gridSecond->contentDigest() );

    const SuitabilityCriterion *grid =
        findCriterion( *gridFirst, QStringLiteral( "grid.compatibility" ) );
    REQUIRE( grid != nullptr );
    REQUIRE( grid->evidence.value( QStringLiteral( "pairs_total" ) ).toInteger() == 203 * 202 / 2 );
    REQUIRE( grid->evidence.value( QStringLiteral( "pairs_checked" ) ).toInteger() == 200 );
}

// ---------------------------------------------------------------------------
// 11. Grid pair sampling: decode cross-check + sampled grading.
// ---------------------------------------------------------------------------

TEST_CASE( "grid linear-pair decode covers exactly the brute-force pair set at n=12",
           "[suitability][adversarial]" )
{
    // Reproduce the published decode for EVERY linear index and compare the
    // produced pair set against a plain double loop: bijective, in-range,
    // i < j. 12 scenes -> 66 pairs, below the cap, so this is the same code
    // path the criterion uses for small inputs.
    const qint64 sceneCount = 12;
    const qint64 totalPairs = sceneCount * ( sceneCount - 1 ) / 2;

    std::vector< std::pair< qint64, qint64 > > decoded;
    for ( qint64 linear = 0; linear < totalPairs; ++linear )
        decoded.push_back( decodeLinearPair( linear, sceneCount ) );
    std::sort( decoded.begin(), decoded.end() );
    REQUIRE( std::adjacent_find( decoded.begin(), decoded.end() ) == decoded.end() ); // no dups

    std::vector< std::pair< qint64, qint64 > > brute;
    for ( qint64 i = 0; i < sceneCount; ++i )
        for ( qint64 j = i + 1; j < sceneCount; ++j )
            brute.push_back( { i, j } );
    std::sort( brute.begin(), brute.end() );
    REQUIRE( decoded == brute );
}

TEST_CASE( "grid grading cross-checks the brute-force blocking count at n=12",
           "[suitability][adversarial]" )
{
    // 12 identical grids except scene 5: exactly the 11 pairs involving
    // scene 5 block. Full (unsampled) comparison must count exactly those.
    QVector<SceneCandidate> scenes;
    ResolvedRequirements req;
    for ( int i = 0; i < 12; ++i )
    {
        SceneCandidate scene = baseScene( QStringLiteral( "s%1" ).arg( i ) );
        scene.grid = makeGrid( i == 5 ? 20.0 : 10.0 );
        scenes.append( scene );
    }
    SuitabilityGoal goal;
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction;
    req = resolved( goal );

    const SuitabilityCriterion criterion = assessGridCompatibility( req, scenes );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_total" ) ).toInteger() == 66 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_checked" ) ).toInteger() == 66 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_pair_count" ) ).toInteger() == 11 );
    REQUIRE( criterion.level == SuitabilityLevel::Marginal ); // non-strict profile
}

TEST_CASE( "grid sampled grading matches the brute-force selected pair count at n=21",
           "[suitability][adversarial]" )
{
    // 21 scenes -> 210 pairs -> 200 sampled. Scene 20 is mismatched: 20
    // blocking pairs exist in total; the criterion must see exactly as many
    // as the published stride selection picks out of the brute-force set.
    const qint64 sceneCount = 21;
    const qint64 totalPairs = sceneCount * ( sceneCount - 1 ) / 2;
    const qint64 pairsChecked = kMaxGridPairs;

    qint64 expectedBlocking = 0;
    for ( qint64 checked = 0; checked < pairsChecked; ++checked )
    {
        // Tail-inclusive stride mirror of criteria_grid.cpp.
        const qint64 linear = ( checked + 1 ) * totalPairs / pairsChecked - 1;
        const auto pair = decodeLinearPair( linear, sceneCount );
        if ( ( pair.first == 20 ) || ( pair.second == 20 ) )
            ++expectedBlocking;
    }

    QVector<SceneCandidate> scenes;
    for ( int i = 0; i < sceneCount; ++i )
    {
        SceneCandidate scene = baseScene( QStringLiteral( "s%1" ).arg( i ) );
        scene.grid = makeGrid( i == 20 ? 20.0 : 10.0 );
        scenes.append( scene );
    }
    SuitabilityGoal goal;
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction;

    const SuitabilityCriterion criterion = assessGridCompatibility( resolved( goal ), scenes );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_total" ) ).toInteger() == totalPairs );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_checked" ) ).toInteger() == pairsChecked );
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_pair_count" ) ).toInteger()
             == expectedBlocking );
    REQUIRE( criterion.notes.join( QLatin1String( " " ) )
                 .contains( QStringLiteral( "sampled %1 of %2 pairs" ).arg( pairsChecked ).arg( totalPairs ) ) );
}

TEST_CASE( "grid tail-scene sampling: a lone tail mismatch cannot escape the sample",
           "[suitability][adversarial]" )
{
    // 1000 scenes -> 499500 pairs -> 200 sampled. The ONLY mismatched scene
    // is the last one: every pair it participates in lives in the final 999
    // linear indices, while the old floor-stride sample stopped at
    // floor(199*499500/200) = 497002 — the entire tail block was unsampled
    // and the criterion graded Suitable with zero blocking pairs.
    QVector< SceneCandidate > scenes;
    for ( int i = 0; i < 1000; ++i )
    {
        SceneCandidate scene = baseScene( QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) ) );
        scene.grid = makeGrid( i == 999 ? 20.0 : 10.0 );
        scenes.append( scene );
    }
    SuitabilityGoal goal;
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction;

    const SuitabilityCriterion criterion = assessGridCompatibility( resolved( goal ), scenes );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_total" ) ).toInteger() == 499500 );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_checked" ) ).toInteger() == 200 );
    // The tail-inclusive stride always judges the final pair (998, 999),
    // which is blocking here.
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_pair_count" ) ).toInteger() >= 1 );
    REQUIRE( ( criterion.level == SuitabilityLevel::Unsuitable
               || criterion.level == SuitabilityLevel::Marginal ) );
}

TEST_CASE( "grid sampled pass degrades to Marginal, matching the label-sampling posture",
           "[suitability][adversarial]" )
{
    // 1000 identical grids: nothing blocks, but only 200 of 499500 pairs
    // were judged — a pass over a sampled space must not read as Suitable.
    QVector< SceneCandidate > scenes;
    for ( int i = 0; i < 1000; ++i )
    {
        SceneCandidate scene = baseScene( QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) ) );
        scene.grid = makeGrid();
        scenes.append( scene );
    }
    SuitabilityGoal goal;
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction;

    const SuitabilityCriterion criterion = assessGridCompatibility( resolved( goal ), scenes );
    REQUIRE( criterion.evidence.value( QStringLiteral( "blocking_pair_count" ) ).toInteger() == 0 );
    REQUIRE( criterion.level == SuitabilityLevel::Marginal );
    REQUIRE( criterion.notes.join( QLatin1String( " " ) ).contains( QStringLiteral( "sampled" ) ) );
}

TEST_CASE( "grid unsampled clean pass stays Suitable", "[suitability][adversarial]" )
{
    // Below the sampling cap, a genuinely clean comparison is still Suitable.
    QVector< SceneCandidate > scenes;
    for ( int i = 0; i < 12; ++i )
    {
        SceneCandidate scene = baseScene( QStringLiteral( "s%1" ).arg( i ) );
        scene.grid = makeGrid();
        scenes.append( scene );
    }
    SuitabilityGoal goal;
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::TemporalPrediction;

    const SuitabilityCriterion criterion = assessGridCompatibility( resolved( goal ), scenes );
    REQUIRE( criterion.evidence.value( QStringLiteral( "pairs_checked" ) ).toInteger() == 66 );
    REQUIRE( criterion.level == SuitabilityLevel::Suitable );
}

// ---------------------------------------------------------------------------
// 12. Profile table pin (positional-init regression guard).
// ---------------------------------------------------------------------------

TEST_CASE( "the builtin profile table keeps its scientific defaults per key",
           "[suitability][adversarial]" )
{
    struct Expectation
    {
        const char *key;
        const char *taskFamily;
        bool requireLabels;
        qint64 minSamples;         // 0 = unset
        double minCoverage;        // 0 = unset
        qint64 minScenesInWindow;  // 0 = unset
        double maxGsdM;            // 0 = unset
        int seasonCount;
        bool pseudoAllowed;
    };
    const std::vector< Expectation > table = {
        { "classification", "classification", true, 200, 0.95, 0, 0, 0, true },
        { "segmentation", "segmentation", true, 50, 0.95, 0, 0, 0, true },
        { "change_detection", "change_detection", true, 100, 0.95, 2, 0, 0, false },
        { "object_detection", "object_detection", true, 300, 0.9, 0, 2.0, 0, true },
        { "regression", "regression", true, 100, 0, 0, 0, 0, true },
        { "temporal_prediction", "temporal_prediction", false, 0, 0, 4, 0, 0, true },
        { "spectral_matching", "spectral_matching", false, 0, 0, 0, 0, 0, true },
        { "phenology", "temporal_prediction", false, 0, 0, 6, 0, 4, true },
    };
    REQUIRE( builtinProfileKeys().size() == static_cast< int >( table.size() ) );
    for ( const Expectation &expectation : table )
    {
        INFO( expectation.key );
        const auto profile = builtinProfile(
            QString::fromLatin1( expectation.key ) );
        REQUIRE( profile.has_value() );
        REQUIRE( profile->key == expectation.key );
        REQUIRE( !profile->displayName.isEmpty() );
        REQUIRE( profile->taskFamily == expectation.taskFamily );
        REQUIRE( profile->requireLabels.value_or( false ) == expectation.requireLabels );
        REQUIRE( profile->minSamples.value_or( 0 ) == expectation.minSamples );
        REQUIRE( profile->minCoverageFraction.value_or( 0.0 ) == expectation.minCoverage );
        REQUIRE( profile->minScenesInWindow.value_or( 0 ) == expectation.minScenesInWindow );
        REQUIRE( profile->maxGsdM.value_or( 0.0 ) == expectation.maxGsdM );
        REQUIRE( profile->requiredSeasons.size() == expectation.seasonCount );
        REQUIRE( profile->pseudoLabelsAllowed.value_or( true ) == expectation.pseudoAllowed );
        // gridStrict is set by no built-in profile today.
        REQUIRE( !profile->gridStrict.has_value() );
    }
}

TEST_CASE( "unrepresentable integral facts fields from json fail typed like goal fields",
           "[suitability][adversarial]" )
{
    // Review round 1 (P1): DatasetFacts::fromJson cast sample/histogram
    // doubles straight to qint64 — the same UB class the goal parser fixed.
    auto factsJson = [] {
        QJsonObject json;
        json.insert( QStringLiteral( "schema_version" ), 1 );
        json.insert( QStringLiteral( "dataset_version_id" ), QStringLiteral( "dv-1" ) );
        return json;
    };

    QJsonObject hugeSampleCount = factsJson();
    hugeSampleCount.insert( QStringLiteral( "sample_count" ), 1e300 );
    const auto huge = DatasetFacts::fromJson( hugeSampleCount );
    REQUIRE( !huge.has_value() );
    REQUIRE( huge.diagnostics().first().code == QStringLiteral( "suitability.facts_invalid" ) );

    QJsonObject infiniteMissing = factsJson();
    infiniteMissing.insert( QStringLiteral( "missing_time_count" ),
                            std::numeric_limits< double >::infinity() );
    const auto infinite = DatasetFacts::fromJson( infiniteMissing );
    REQUIRE( !infinite.has_value() );
    REQUIRE( infinite.diagnostics().first().code == QStringLiteral( "suitability.facts_invalid" ) );

    QJsonObject hugeHistogram = factsJson();
    QJsonObject byClass;
    byClass.insert( QStringLiteral( "forest" ), 1e301 );
    hugeHistogram.insert( QStringLiteral( "samples_by_class" ), byClass );
    const auto histogram = DatasetFacts::fromJson( hugeHistogram );
    REQUIRE( !histogram.has_value() );
    REQUIRE( histogram.diagnostics().first().code == QStringLiteral( "suitability.facts_invalid" ) );

    // RED on master: extent doubles had no finite guard, so a corrupted
    // store document carried ±inf (or silently-zeroed gaps) into the
    // spatial criteria and the report digest. Same typed gate as the
    // integral fields.
    QJsonObject infiniteExtent = factsJson();
    infiniteExtent.insert( QStringLiteral( "has_extent" ), true );
    infiniteExtent.insert( QStringLiteral( "min_x" ),
                           std::numeric_limits< double >::infinity() );
    const auto infiniteExtentFacts = DatasetFacts::fromJson( infiniteExtent );
    REQUIRE( !infiniteExtentFacts.has_value() );
    REQUIRE( infiniteExtentFacts.diagnostics().first().code
             == QStringLiteral( "suitability.facts_invalid" ) );

    QJsonObject missingExtentMember = factsJson();
    missingExtentMember.insert( QStringLiteral( "has_extent" ), true );
    missingExtentMember.insert( QStringLiteral( "max_y" ), 40.0 );
    const auto missingExtent = DatasetFacts::fromJson( missingExtentMember );
    REQUIRE( !missingExtent.has_value() );
    REQUIRE( missingExtent.diagnostics().first().code == QStringLiteral( "suitability.facts_invalid" ) );

    // Sane facts keep parsing (regression against over-tightening).
    QJsonObject sane = factsJson();
    sane.insert( QStringLiteral( "sample_count" ), 4200.0 );
    const auto ok = DatasetFacts::fromJson( sane );
    REQUIRE( ok.has_value() );
    REQUIRE( ok.value().sampleCount == 4200 );

    QJsonObject saneExtent = factsJson();
    saneExtent.insert( QStringLiteral( "has_extent" ), true );
    saneExtent.insert( QStringLiteral( "min_x" ), 10.0 );
    saneExtent.insert( QStringLiteral( "min_y" ), 20.0 );
    saneExtent.insert( QStringLiteral( "max_x" ), 30.0 );
    saneExtent.insert( QStringLiteral( "max_y" ), 40.0 );
    const auto extentOk = DatasetFacts::fromJson( saneExtent );
    REQUIRE( extentOk.has_value() );
    REQUIRE( extentOk.value().minX == 10.0 );
    REQUIRE( extentOk.value().maxY == 40.0 );
}

// ---------------------------------------------------------------------------
// Track 16 WP-F: the provider seam boundary — a named dataset is never
// silently assessed without its facts.
// ---------------------------------------------------------------------------

namespace {

/// Spy provider: records every consultation.
class SpyProvider final : public sicnu::suitability::SuitabilityDataProvider
{
    public:
        mutable int consultations = 0;
        sicnu::data::Result<sicnu::suitability::DatasetFacts> datasetFacts(
            const QString &, const sicnu::suitability::FactsLimits & ) const override
        {
            ++consultations;
            return sicnu::data::Result<sicnu::suitability::DatasetFacts>::success(
                sicnu::suitability::DatasetFacts{} );
        }
};

} // namespace

TEST_CASE( "a failing provider fails the named dataset typed — never silently "
           "assessed",
           "[suitability][track16][provider]" )
{
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = perfectGoal();
    inputs.datasetVersionId = QStringLiteral( "dv-track16" );
    static const sicnu::data::Diagnostic failure{
        QStringLiteral( "suitability.store_read_failed" ),
        QStringLiteral( "injected track-16 failure" ) };
    const InMemoryDataProvider provider( failure );
    inputs.provider = &provider;

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE_FALSE( result.has_value() );
    REQUIRE( result.diagnostics().size() == 1 );
    CHECK( result.diagnostics().first().code == QStringLiteral( "suitability.store_read_failed" ) );
}

TEST_CASE( "explicit facts take precedence; the provider channel is not "
           "consulted",
           "[suitability][track16][provider]" )
{
    SuitabilityAssessor::Inputs inputs = perfectInputs();
    inputs.datasetVersionId = QStringLiteral( "dv-track16" );
    SpyProvider spy;
    inputs.provider = &spy;

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    CHECK( spy.consultations == 0 );
}

TEST_CASE( "the provider is consulted only when facts are absent and a dataset "
           "version names the subject",
           "[suitability][track16][provider]" )
{
    // Scenes alone, no facts, no version id: the provider must stay quiet.
    {
        SuitabilityAssessor::Inputs inputs;
        inputs.goal = perfectGoal();
        inputs.scenes = perfectScenes();
        SpyProvider spy;
        inputs.provider = &spy;
        const auto result = SuitabilityAssessor::assess( inputs );
        REQUIRE( result.has_value() );
        CHECK( spy.consultations == 0 );
    }
    // Facts absent + version id present: the provider channel serves.
    {
        SuitabilityAssessor::Inputs inputs;
        inputs.goal = perfectGoal();
        inputs.datasetVersionId = QStringLiteral( "dv-track16" );
        const InMemoryDataProvider provider( perfectFacts() );
        inputs.provider = &provider;
        const auto result = SuitabilityAssessor::assess( inputs );
        REQUIRE( result.has_value() );
    }
}
