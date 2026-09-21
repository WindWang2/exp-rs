// test_suitability_spatial.cpp — Slice B: SceneCandidate DTO + adapter, goal
// resolution/validation, spatial coverage & resolution criteria, assessor
// entry point.
//
// RED-first contract for the spatial slice:
//   - coverage is only computed when scene CRS and AOI CRS are the same
//     (empty WKT never matches; the core does not reproject);
//   - a goal with an AOI but no usable data is Unsuitable, not Unknown;
//   - partial coverage between 0.5*threshold and threshold is Marginal,
//     below that Unsuitable (fail-closed grading);
//   - meters-based GSD only ever comes from explicit evidence, never from
//     CRS-unit pixel sizes;
//   - the assessor fails typed rather than silently truncating or guessing.

#include <catch2/catch_test_macros.hpp>

#include "suitability/criteria_spatial.h"
#include "suitability/scene_candidate.h"
#include "suitability/suitability_assessor.h"
#include "suitability/suitability_goal.h"
#include "suitability/suitability_report.h"
#include "suitability/suitability_types.h"

#include "data/band_role.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using sicnu::data::AssetState;
using sicnu::data::RasterStructure;
using sicnu::data::SpatialExtent;
using sicnu::suitability::ResolvedRequirements;
using sicnu::suitability::SceneCandidate;
using sicnu::suitability::SuitabilityAssessor;
using sicnu::suitability::SuitabilityCriterion;
using sicnu::suitability::SuitabilityGoal;
using sicnu::suitability::SuitabilityLevel;
using sicnu::suitability::SuitabilityReport;
using sicnu::suitability::assessResolution;
using sicnu::suitability::assessSpatialCoverage;
using sicnu::suitability::resolveRequirements;

namespace
{

SpatialExtent extent( double minX, double minY, double maxX, double maxY )
{
    return SpatialExtent{ minX, minY, maxX, maxY, true };
}

SceneCandidate makeScene( const QString &id, double minX, double minY,
                          double maxX, double maxY, const QString &crsWkt = QString() )
{
    SceneCandidate scene;
    scene.id = id;
    scene.state = AssetState::Ready;
    scene.crsWkt = crsWkt;
    scene.extent = extent( minX, minY, maxX, maxY );
    return scene;
}

SuitabilityGoal goalWithAoi( double minX, double minY, double maxX, double maxY )
{
    SuitabilityGoal goal;
    goal.hasAoi = true;
    goal.aoi = extent( minX, minY, maxX, maxY );
    goal.aoiCrsWkt = QStringLiteral( "aoi-crs" );
    return goal;
}

RasterStructure makeRasterStructure()
{
    RasterStructure structure;
    structure.driverName = QStringLiteral( "GTiff" );
    structure.width = 100;
    structure.height = 200;
    structure.bandCount = 3;
    structure.crsWkt = QStringLiteral( "scene-crs" );
    structure.hasGeoTransform = true;
    structure.geoTransform = { 480000.0, 30.0, 0.0, 5200000.0, 0.0, -30.0 };
    structure.extent = extent( 480000.0, 5194000.0, 483000.0, 5200000.0 );
    sicnu::data::RasterBandStructure band;
    band.number = 1;
    band.role = sicnu::data::BandRole::Unknown;
    structure.bands.append( band );
    sicnu::data::RasterBandStructure band2;
    band2.number = 2;
    band2.role = sicnu::data::BandRole::Red;
    structure.bands.append( band2 );
    sicnu::data::RasterBandStructure band3;
    band3.number = 3;
    band3.role = sicnu::data::BandRole::NIR;
    structure.bands.append( band3 );
    return structure;
}

QStringList noteText( const SuitabilityCriterion &criterion )
{
    return criterion.notes;
}

} // namespace

TEST_CASE( "scene candidate JSON round-trip", "[suitability][spatial]" )
{
    SceneCandidate scene;
    scene.id = QStringLiteral( "scene-1" );
    scene.state = AssetState::Ready;
    scene.crsWkt = QStringLiteral( "scene-crs" );
    scene.extent = extent( 0.0, 1.0, 2.0, 3.0 );
    scene.gsdM = 10.5;
    scene.pixelSizeCrsUnits = 30.0;
    scene.acquisitionTimeUtc = QDateTime::fromString( QStringLiteral( "2024-05-01T10:30:00Z" ), Qt::ISODate );
    REQUIRE( scene.acquisitionTimeUtc.has_value() );
    scene.cloudCoverPercent = 12.5;
    scene.bandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    scene.bandCount = 4;
    sicnu::data::RasterGrid grid;
    grid.crsWkt = QStringLiteral( "scene-crs" );
    grid.hasGeoTransform = true;
    grid.geoTransform = { 0.0, 10.0, 0.0, 1000.0, 0.0, -10.0 };
    grid.width = 100;
    grid.height = 100;
    grid.bandNoData = { 0.0, std::nullopt };
    scene.grid = grid;
    scene.modality = QStringLiteral( "optical" );
    scene.provenance = QJsonObject{ { QStringLiteral( "source" ), QStringLiteral( "stac" ) } };

    const auto parsed = SceneCandidate::fromJson( scene.toJson() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->id == scene.id );
    REQUIRE( parsed->state == scene.state );
    REQUIRE( parsed->crsWkt == scene.crsWkt );
    REQUIRE( parsed->extent == scene.extent );
    REQUIRE( parsed->gsdM == scene.gsdM );
    REQUIRE( parsed->pixelSizeCrsUnits == scene.pixelSizeCrsUnits );
    REQUIRE( parsed->acquisitionTimeUtc->toMSecsSinceEpoch()
             == scene.acquisitionTimeUtc->toMSecsSinceEpoch() );
    REQUIRE( parsed->cloudCoverPercent == scene.cloudCoverPercent );
    REQUIRE( parsed->bandRoles == scene.bandRoles );
    REQUIRE( parsed->bandCount == scene.bandCount );
    REQUIRE( parsed->grid.has_value() );
    REQUIRE( parsed->grid->width == grid.width );
    REQUIRE( parsed->grid->height == grid.height );
    REQUIRE( parsed->grid->geoTransform == grid.geoTransform );
    REQUIRE( parsed->grid->bandNoData.size() == grid.bandNoData.size() );
    REQUIRE( parsed->grid->bandNoData.at( 0 ) == grid.bandNoData.at( 0 ) );
    REQUIRE( !parsed->grid->bandNoData.at( 1 ).has_value() );
    REQUIRE( parsed->modality == scene.modality );
    REQUIRE( parsed->provenance == scene.provenance );
    REQUIRE( parsed->toJson() == scene.toJson() );
    REQUIRE( parsed->usable() );
}

TEST_CASE( "scene candidate fromJson fails typed on foreign schema or bad content", "[suitability][spatial]" )
{
    QJsonObject foreign = SceneCandidate().toJson();
    foreign.insert( QStringLiteral( "schema_version" ), 99 );
    const auto parsedForeign = SceneCandidate::fromJson( foreign );
    REQUIRE( !parsedForeign.has_value() );
    REQUIRE( parsedForeign.diagnostics().first().code == QStringLiteral( "suitability.scene_schema" ) );

    QJsonObject noId = SceneCandidate().toJson();
    noId.remove( QStringLiteral( "id" ) );
    const auto parsedNoId = SceneCandidate::fromJson( noId );
    REQUIRE( !parsedNoId.has_value() );
    REQUIRE( parsedNoId.diagnostics().first().code == QStringLiteral( "suitability.scene_invalid" ) );

    QJsonObject badState = SceneCandidate().toJson();
    badState.insert( QStringLiteral( "id" ), QStringLiteral( "scene-x" ) );
    badState.insert( QStringLiteral( "state" ), QStringLiteral( "FlyToTheMoon" ) );
    const auto parsedBadState = SceneCandidate::fromJson( badState );
    REQUIRE( !parsedBadState.has_value() );
    REQUIRE( parsedBadState.diagnostics().first().code == QStringLiteral( "suitability.scene_invalid" ) );
}

TEST_CASE( "scene candidate adapter maps structure fields and leaves gsd unknown without a hint", "[suitability][spatial]" )
{
    const RasterStructure structure = makeRasterStructure();
    const SceneCandidate scene = sicnu::suitability::sceneCandidateFromRasterStructure(
        QStringLiteral( "asset-1" ), AssetState::Ready, structure );

    REQUIRE( scene.id == QStringLiteral( "asset-1" ) );
    REQUIRE( scene.state == AssetState::Ready );
    REQUIRE( scene.usable() );
    REQUIRE( scene.crsWkt == structure.crsWkt );
    REQUIRE( scene.extent == structure.extent );
    REQUIRE( scene.bandCount == 3 );
    // Band roles come only from bands with a known role, in band order.
    REQUIRE( scene.bandRoles == QStringList{ QStringLiteral( "red" ), QStringLiteral( "nir" ) } );
    // Pixel size in CRS units is carried as evidence.
    REQUIRE( scene.pixelSizeCrsUnits.has_value() );
    REQUIRE( *scene.pixelSizeCrsUnits == 30.0 );
    // The honest contract: without an explicit meter hint there is NO gsd —
    // the CRS-unit pixel size must never be passed off as meters.
    REQUIRE( !scene.gsdM.has_value() );

    // Without a geotransform there is no pixel size evidence either.
    RasterStructure unreferenced = structure;
    unreferenced.hasGeoTransform = false;
    const SceneCandidate candidate = sicnu::suitability::sceneCandidateFromRasterStructure(
        QStringLiteral( "asset-2" ), AssetState::Registered, unreferenced );
    REQUIRE( !candidate.pixelSizeCrsUnits.has_value() );
    REQUIRE( !candidate.usable() );
}

TEST_CASE( "scene candidate adapter consumes hints and ignores unknown keys", "[suitability][spatial]" )
{
    const RasterStructure structure = makeRasterStructure();
    QJsonObject hints;
    hints.insert( QStringLiteral( "gsd_m" ), 10.0 );
    hints.insert( QStringLiteral( "acquisition_time_utc" ), QStringLiteral( "2024-05-01T10:30:00Z" ) );
    hints.insert( QStringLiteral( "cloud_cover_percent" ), 7.5 );
    hints.insert( QStringLiteral( "modality" ), QStringLiteral( "optical" ) );
    hints.insert( QStringLiteral( "mystery_key" ), QStringLiteral( "ignored" ) );

    const SceneCandidate scene = sicnu::suitability::sceneCandidateFromRasterStructure(
        QStringLiteral( "asset-1" ), AssetState::Ready, structure, hints );

    REQUIRE( scene.gsdM.has_value() );
    REQUIRE( *scene.gsdM == 10.0 );
    REQUIRE( scene.acquisitionTimeUtc.has_value() );
    REQUIRE( scene.acquisitionTimeUtc->timeSpec() == Qt::UTC );
    REQUIRE( scene.acquisitionTimeUtc->toMSecsSinceEpoch()
             == QDateTime::fromString( QStringLiteral( "2024-05-01T10:30:00Z" ), Qt::ISODate )
                    .toMSecsSinceEpoch() );
    REQUIRE( scene.cloudCoverPercent.has_value() );
    REQUIRE( *scene.cloudCoverPercent == 7.5 );
    REQUIRE( scene.modality == QStringLiteral( "optical" ) );
}

TEST_CASE( "goal resolution applies defaults and validates explicit values", "[suitability][spatial]" )
{
    SuitabilityGoal goal;
    const auto resolved = resolveRequirements( goal );
    REQUIRE( resolved.has_value() );
    REQUIRE( resolved->minCoverageFractionResolved == 0.95 );
    REQUIRE( !resolved->hasAoi );
    REQUIRE( !resolved->hasTimeWindow );
    REQUIRE( resolved->maxCloudCoverPercent < 0 );
    REQUIRE( resolved->minScenesInWindow == 0 );

    auto expectFailure = []( const SuitabilityGoal &bad, const QString &code )
    {
        const auto result = resolveRequirements( bad );
        REQUIRE( !result.has_value() );
        REQUIRE( result.diagnostics().first().code == code );
    };

    SuitabilityGoal profiled;
    profiled.profileKey = QStringLiteral( "phenology" );
    expectFailure( profiled, QStringLiteral( "suitability.profile_unknown" ) );

    SuitabilityGoal backwards;
    backwards.hasTimeWindow = true;
    backwards.windowStartUtc = QDateTime::fromString( QStringLiteral( "2024-06-01T00:00:00Z" ), Qt::ISODate );
    backwards.windowEndUtc = QDateTime::fromString( QStringLiteral( "2024-05-01T00:00:00Z" ), Qt::ISODate );
    expectFailure( backwards, QStringLiteral( "suitability.goal_invalid" ) );

    SuitabilityGoal equalWindow = backwards;
    equalWindow.windowEndUtc = equalWindow.windowStartUtc;
    expectFailure( equalWindow, QStringLiteral( "suitability.goal_invalid" ) );

    SuitabilityGoal invertedGsd;
    invertedGsd.minGsdM = 30.0;
    invertedGsd.maxGsdM = 10.0;
    expectFailure( invertedGsd, QStringLiteral( "suitability.goal_invalid" ) );

    // One-sided bounds are fine; only both-positive must be ordered.
    SuitabilityGoal oneSided;
    oneSided.minGsdM = 30.0;
    REQUIRE( resolveRequirements( oneSided ).has_value() );

    SuitabilityGoal badCloudHigh;
    badCloudHigh.maxCloudCoverPercent = 150.0;
    expectFailure( badCloudHigh, QStringLiteral( "suitability.goal_invalid" ) );

    SuitabilityGoal badCloudLow;
    badCloudLow.maxCloudCoverPercent = -2.0;
    expectFailure( badCloudLow, QStringLiteral( "suitability.goal_invalid" ) );

    SuitabilityGoal negativeSamples;
    negativeSamples.minSamples = -1;
    expectFailure( negativeSamples, QStringLiteral( "suitability.goal_invalid" ) );

    SuitabilityGoal badCoverage;
    badCoverage.minCoverageFraction = 1.5;
    expectFailure( badCoverage, QStringLiteral( "suitability.goal_invalid" ) );

    SuitabilityGoal negativeDensity;
    negativeDensity.minScenesInWindow = -1;
    expectFailure( negativeDensity, QStringLiteral( "suitability.goal_invalid" ) );
}

TEST_CASE( "goal JSON round-trip and deterministic digest", "[suitability][spatial]" )
{
    SuitabilityGoal goal = goalWithAoi( 0.0, 0.0, 10.0, 10.0 );
    goal.taskFamily = sicnu::dataset::BenchmarkTaskFamily::Segmentation;
    goal.hasTimeWindow = true;
    goal.windowStartUtc = QDateTime::fromString( QStringLiteral( "2024-01-01T00:00:00Z" ), Qt::ISODate );
    goal.windowEndUtc = QDateTime::fromString( QStringLiteral( "2024-12-31T00:00:00Z" ), Qt::ISODate );
    goal.requiredSeasons = { QStringLiteral( "spring" ), QStringLiteral( "summer" ) };
    goal.minGsdM = 5.0;
    goal.maxGsdM = 30.0;
    goal.requiredBandRoles = { QStringLiteral( "red" ), QStringLiteral( "nir" ) };
    goal.maxCloudCoverPercent = 20.0;
    goal.minSamples = 200;
    goal.requiredClasses = { QStringLiteral( "water" ), QStringLiteral( "forest" ) };
    goal.requireLabels = true;
    goal.hasModel = true;
    goal.modelRequiredBandRoles = { QStringLiteral( "red" ) };
    goal.modelMinGsdM = 1.0;
    goal.modelMaxGsdM = 5.0;
    goal.modelModality = QStringLiteral( "optical" );
    goal.minCoverageFraction = 0.8;
    goal.minScenesInWindow = 3;

    const auto parsed = SuitabilityGoal::fromJson( goal.toJson() );
    REQUIRE( parsed.has_value() );
    REQUIRE( parsed->taskFamily == goal.taskFamily );
    REQUIRE( parsed->profileKey == goal.profileKey );
    REQUIRE( parsed->hasAoi == goal.hasAoi );
    REQUIRE( parsed->aoi == goal.aoi );
    REQUIRE( parsed->aoiCrsWkt == goal.aoiCrsWkt );
    REQUIRE( parsed->hasTimeWindow == goal.hasTimeWindow );
    REQUIRE( parsed->windowStartUtc.toMSecsSinceEpoch() == goal.windowStartUtc.toMSecsSinceEpoch() );
    REQUIRE( parsed->windowEndUtc.toMSecsSinceEpoch() == goal.windowEndUtc.toMSecsSinceEpoch() );
    REQUIRE( parsed->requiredSeasons == goal.requiredSeasons );
    REQUIRE( parsed->minGsdM == goal.minGsdM );
    REQUIRE( parsed->maxGsdM == goal.maxGsdM );
    REQUIRE( parsed->requiredBandRoles == goal.requiredBandRoles );
    REQUIRE( parsed->maxCloudCoverPercent == goal.maxCloudCoverPercent );
    REQUIRE( parsed->minSamples == goal.minSamples );
    REQUIRE( parsed->requiredClasses == goal.requiredClasses );
    REQUIRE( parsed->requireLabels == goal.requireLabels );
    REQUIRE( parsed->hasModel == goal.hasModel );
    REQUIRE( parsed->modelRequiredBandRoles == goal.modelRequiredBandRoles );
    REQUIRE( parsed->modelMinGsdM == goal.modelMinGsdM );
    REQUIRE( parsed->modelMaxGsdM == goal.modelMaxGsdM );
    REQUIRE( parsed->modelModality == goal.modelModality );
    REQUIRE( parsed->minCoverageFraction == goal.minCoverageFraction );
    REQUIRE( parsed->minScenesInWindow == goal.minScenesInWindow );

    // Unknown fields are ignored, not fatal.
    QJsonObject extended = goal.toJson();
    extended.insert( QStringLiteral( "some_future_field" ), 42 );
    REQUIRE( SuitabilityGoal::fromJson( extended ).has_value() );

    // Unknown task family value fails typed instead of guessing.
    QJsonObject badFamily = goal.toJson();
    badFamily.insert( QStringLiteral( "task_family" ), QStringLiteral( "astrology" ) );
    const auto parsedBadFamily = SuitabilityGoal::fromJson( badFamily );
    REQUIRE( !parsedBadFamily.has_value() );
    REQUIRE( parsedBadFamily.diagnostics().first().code == QStringLiteral( "suitability.goal_invalid" ) );

    // Foreign schema_version fails typed.
    QJsonObject foreign = goal.toJson();
    foreign.insert( QStringLiteral( "schema_version" ), 2 );
    const auto parsedForeign = SuitabilityGoal::fromJson( foreign );
    REQUIRE( !parsedForeign.has_value() );
    REQUIRE( parsedForeign.diagnostics().first().code == QStringLiteral( "suitability.goal_schema" ) );

    // Digests are deterministic and content-sensitive.
    const SuitabilityGoal sameGoal = *( SuitabilityGoal::fromJson( goal.toJson() ) );
    REQUIRE( goal.contentDigest() == sameGoal.contentDigest() );
    REQUIRE( !goal.contentDigest().isEmpty() );
    SuitabilityGoal changed = goal;
    changed.minSamples = 201;
    REQUIRE( changed.contentDigest() != goal.contentDigest() );
}

TEST_CASE( "spatial coverage without an AOI is unknown, not applicable-looking", "[suitability][spatial]" )
{
    SuitabilityGoal goal;
    const auto req = resolveRequirements( goal );
    REQUIRE( req.has_value() );

    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s1" ), 0, 0, 10, 10 ) );
    const SuitabilityCriterion criterion = assessSpatialCoverage( *req, scenes );
    REQUIRE( criterion.id == QStringLiteral( "spatial.coverage" ) );
    REQUIRE( criterion.applicable );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( noteText( criterion ).join( QLatin1String( " " ) ).contains( QStringLiteral( "AOI not specified" ) ) );
    REQUIRE( criterion.gaps.isEmpty() );
}

TEST_CASE( "spatial coverage with an AOI but no usable scene is unsuitable", "[suitability][spatial]" )
{
    const auto req = resolveRequirements( goalWithAoi( 0, 0, 100, 100 ) );
    REQUIRE( req.has_value() );

    QVector<SceneCandidate> scenes;
    SceneCandidate notReady = makeScene( QStringLiteral( "s1" ), 0, 0, 100, 100 );
    notReady.state = AssetState::Registered;
    scenes.append( notReady );
    SceneCandidate noExtent = makeScene( QStringLiteral( "s2" ), 0, 0, 100, 100 );
    noExtent.extent.valid = false;
    scenes.append( noExtent );

    const SuitabilityCriterion criterion = assessSpatialCoverage( *req, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Unsuitable );
    REQUIRE( criterion.gaps.size() == 1 );
    REQUIRE( criterion.gaps.first().id == QStringLiteral( "coverage.no_usable_scene" ) );
    REQUIRE( criterion.gaps.first().criterionId == QStringLiteral( "spatial.coverage" ) );
}

TEST_CASE( "spatial coverage excludes CRS mismatches and never reprojects", "[suitability][spatial]" )
{
    const QString wgs84 = QStringLiteral(
        "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    const QString utm33n = QStringLiteral(
        "PROJCS[\"WGS 84 / UTM zone 33N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
        "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
        "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
        "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",15],"
        "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
        "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]" );

    SuitabilityGoal goal = goalWithAoi( 0, 0, 100, 100 );
    goal.aoiCrsWkt = wgs84;
    const auto req = resolveRequirements( goal );
    REQUIRE( req.has_value() );

    // A scene in another CRS must not silently count as coverage. With
    // nothing measurable left the honest verdict is Unknown, not Unsuitable.
    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "utm" ), 0, 0, 100, 100, utm33n ) );
    const SuitabilityCriterion mismatched = assessSpatialCoverage( *req, scenes );
    REQUIRE( mismatched.level == SuitabilityLevel::Unknown );
    const QString notes = noteText( mismatched ).join( QLatin1String( " " ) );
    REQUIRE( notes.contains( QStringLiteral( "CRS mismatch" ) ) );
    REQUIRE( notes.contains( QStringLiteral( "does not reproject" ) ) );

    // ...and a matching scene beside it restores coverage.
    scenes.append( makeScene( QStringLiteral( "wgs" ), 0, 0, 100, 100, wgs84 ) );
    const SuitabilityCriterion matched = assessSpatialCoverage( *req, scenes );
    REQUIRE( matched.level == SuitabilityLevel::Suitable );

    // An empty scene WKT never counts as "same CRS" — also Unknown.
    QVector<SceneCandidate> unnamed;
    unnamed.append( makeScene( QStringLiteral( "nocrs" ), 0, 0, 100, 100 ) );
    const SuitabilityCriterion noCrs = assessSpatialCoverage( *req, unnamed );
    REQUIRE( noCrs.level == SuitabilityLevel::Unknown );

    // All scenes excluded on CRS grounds is Unknown (nothing was measured),
    // not Unsuitable.
    SuitabilityGoal unnamedAoi = goalWithAoi( 0, 0, 100, 100 );
    unnamedAoi.aoiCrsWkt.clear();
    const auto reqUnnamed = resolveRequirements( unnamedAoi );
    REQUIRE( reqUnnamed.has_value() );
    QVector<SceneCandidate> emptyCrsScenes;
    emptyCrsScenes.append( makeScene( QStringLiteral( "nocrs" ), 0, 0, 100, 100 ) );
    const SuitabilityCriterion allExcluded = assessSpatialCoverage( *reqUnnamed, emptyCrsScenes );
    REQUIRE( allExcluded.level == SuitabilityLevel::Unknown );
}

TEST_CASE( "spatial coverage measures the union of overlapping scenes", "[suitability][spatial]" )
{
    const auto req = resolveRequirements( goalWithAoi( 0, 0, 100, 100 ) );
    REQUIRE( req.has_value() );

    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "a" ), 0, 0, 60, 100, QStringLiteral( "aoi-crs" ) ) );
    scenes.append( makeScene( QStringLiteral( "b" ), 40, 0, 100, 100, QStringLiteral( "aoi-crs" ) ) );
    const SuitabilityCriterion criterion = assessSpatialCoverage( *req, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Suitable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "measured_fraction" ) ).toDouble() == 1.0 );
    REQUIRE( criterion.gaps.isEmpty() );
}

TEST_CASE( "spatial coverage grading locks the 0.5*threshold marginal band", "[suitability][spatial]" )
{
    SuitabilityGoal goal = goalWithAoi( 0, 0, 100, 100 );
    goal.minCoverageFraction = 0.8;
    const auto req = resolveRequirements( goal );
    REQUIRE( req.has_value() );
    REQUIRE( req->minCoverageFractionResolved == 0.8 );

    // Exactly at the threshold is Suitable (inclusive boundary).
    QVector<SceneCandidate> atThreshold;
    atThreshold.append( makeScene( QStringLiteral( "s" ), 0, 0, 80, 100, QStringLiteral( "aoi-crs" ) ) );
    REQUIRE( assessSpatialCoverage( *req, atThreshold ).level == SuitabilityLevel::Suitable );

    // 0.5*threshold (0.4 here) is the inclusive Marginal floor.
    QVector<SceneCandidate> atMarginalFloor;
    atMarginalFloor.append( makeScene( QStringLiteral( "s" ), 0, 0, 40, 100, QStringLiteral( "aoi-crs" ) ) );
    const SuitabilityCriterion marginal = assessSpatialCoverage( *req, atMarginalFloor );
    REQUIRE( marginal.level == SuitabilityLevel::Marginal );
    REQUIRE( marginal.gaps.size() == 1 );
    REQUIRE( marginal.gaps.first().id == QStringLiteral( "coverage.below_minimum" ) );
    REQUIRE( marginal.gaps.first().evidence.value( QStringLiteral( "measured_fraction" ) ).toDouble() == 0.4 );
    REQUIRE( marginal.gaps.first().evidence.value( QStringLiteral( "required_fraction" ) ).toDouble() == 0.8 );

    // Strictly below 0.5*threshold fails all the way to Unsuitable.
    QVector<SceneCandidate> below;
    below.append( makeScene( QStringLiteral( "s" ), 0, 0, 39, 100, QStringLiteral( "aoi-crs" ) ) );
    const SuitabilityCriterion poor = assessSpatialCoverage( *req, below );
    REQUIRE( poor.level == SuitabilityLevel::Unsuitable );
    REQUIRE( poor.gaps.size() == 1 );
    REQUIRE( poor.gaps.first().id == QStringLiteral( "coverage.below_minimum" ) );

    // Zero measured coverage is Unsuitable, never Marginal.
    QVector<SceneCandidate> disjoint;
    disjoint.append( makeScene( QStringLiteral( "s" ), 200, 200, 300, 300, QStringLiteral( "aoi-crs" ) ) );
    const SuitabilityCriterion zero = assessSpatialCoverage( *req, disjoint );
    REQUIRE( zero.level == SuitabilityLevel::Unsuitable );
    REQUIRE( zero.evidence.value( QStringLiteral( "measured_fraction" ) ).toDouble() == 0.0 );
}

TEST_CASE( "spatial coverage with a degenerate AOI is unknown with a diagnostic", "[suitability][spatial]" )
{
    SuitabilityGoal goal = goalWithAoi( 50, 50, 50, 50 );
    const auto req = resolveRequirements( goal );
    REQUIRE( req.has_value() );

    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s" ), 0, 0, 100, 100 ) );
    const SuitabilityCriterion criterion = assessSpatialCoverage( *req, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( !criterion.diagnostics.isEmpty() );
    REQUIRE( criterion.gaps.isEmpty() );
}

TEST_CASE( "resolution without a gsd requirement is not applicable", "[suitability][spatial]" )
{
    const auto req = resolveRequirements( SuitabilityGoal{} );
    REQUIRE( req.has_value() );

    QVector<SceneCandidate> scenes;
    scenes.append( makeScene( QStringLiteral( "s" ), 0, 0, 10, 10 ) );
    const SuitabilityCriterion criterion = assessResolution( *req, scenes );
    REQUIRE( criterion.id == QStringLiteral( "spatial.resolution" ) );
    REQUIRE( !criterion.applicable );
    REQUIRE( criterion.evidence.value( QStringLiteral( "status" ) )
                 .toString() == QStringLiteral( "not_applicable" ) );
}

TEST_CASE( "resolution with no usable or measurable scene is unknown", "[suitability][spatial]" )
{
    SuitabilityGoal goal;
    goal.maxGsdM = 30.0;
    const auto req = resolveRequirements( goal );
    REQUIRE( req.has_value() );

    REQUIRE( assessResolution( *req, {} ).level == SuitabilityLevel::Unknown );

    QVector<SceneCandidate> unknownGsd;
    SceneCandidate scene = makeScene( QStringLiteral( "s" ), 0, 0, 10, 10 );
    scene.gsdM = std::nullopt;
    unknownGsd.append( scene );
    const SuitabilityCriterion criterion = assessResolution( *req, unknownGsd );
    REQUIRE( criterion.level == SuitabilityLevel::Unknown );
    REQUIRE( criterion.evidence.value( QStringLiteral( "gsd_unknown_count" ) ).toInt() == 1 );
    REQUIRE( criterion.gaps.isEmpty() );
}

TEST_CASE( "resolution grades in-range, mixed and out-of-range scene sets", "[suitability][spatial]" )
{
    SuitabilityGoal goal;
    goal.minGsdM = 5.0;
    goal.maxGsdM = 30.0;
    const auto req = resolveRequirements( goal );
    REQUIRE( req.has_value() );

    auto makeGsdScene = []( const QString &id, double gsd )
    {
        SceneCandidate scene;
        scene.id = id;
        scene.state = AssetState::Ready;
        scene.gsdM = gsd;
        return scene;
    };

    // All in range (inclusive bounds) -> Suitable.
    QVector<SceneCandidate> good;
    good.append( makeGsdScene( QStringLiteral( "a" ), 5.0 ) );
    good.append( makeGsdScene( QStringLiteral( "b" ), 30.0 ) );
    const SuitabilityCriterion fine = assessResolution( *req, good );
    REQUIRE( fine.level == SuitabilityLevel::Suitable );
    REQUIRE( fine.gaps.isEmpty() );

    // All out of range -> Unsuitable with the measured/required evidence.
    QVector<SceneCandidate> coarse;
    coarse.append( makeGsdScene( QStringLiteral( "c" ), 100.0 ) );
    coarse.append( makeGsdScene( QStringLiteral( "d" ), 200.0 ) );
    const SuitabilityCriterion bad = assessResolution( *req, coarse );
    REQUIRE( bad.level == SuitabilityLevel::Unsuitable );
    REQUIRE( bad.gaps.size() == 1 );
    REQUIRE( bad.gaps.first().id == QStringLiteral( "resolution.out_of_range" ) );
    REQUIRE( bad.evidence.value( QStringLiteral( "measured_min_gsd_m" ) ).toDouble() == 100.0 );
    REQUIRE( bad.evidence.value( QStringLiteral( "measured_max_gsd_m" ) ).toDouble() == 200.0 );

    // Mixed -> Marginal, gap carries the offending ids.
    QVector<SceneCandidate> mixed;
    mixed.append( makeGsdScene( QStringLiteral( "ok" ), 10.0 ) );
    mixed.append( makeGsdScene( QStringLiteral( "too-coarse" ), 999.0 ) );
    const SuitabilityCriterion partial = assessResolution( *req, mixed );
    REQUIRE( partial.level == SuitabilityLevel::Marginal );
    REQUIRE( partial.gaps.size() == 1 );
    REQUIRE( partial.gaps.first().id == QStringLiteral( "resolution.out_of_range" ) );
    REQUIRE( partial.gaps.first().evidence.value( QStringLiteral( "out_of_range_scene_ids" ) )
                 .toArray().size() == 1 );

    // Unknown-gsd scenes are counted, not judged.
    SceneCandidate mystery = makeGsdScene( QStringLiteral( "mystery" ), 10.0 );
    mystery.gsdM = std::nullopt;
    mixed.append( mystery );
    const SuitabilityCriterion withUnknown = assessResolution( *req, mixed );
    REQUIRE( withUnknown.evidence.value( QStringLiteral( "gsd_unknown_count" ) ).toInt() == 1 );
    REQUIRE( withUnknown.level == SuitabilityLevel::Marginal );
}

TEST_CASE( "resolution caps the out-of-range scene id evidence at 20", "[suitability][spatial]" )
{
    SuitabilityGoal goal;
    goal.maxGsdM = 10.0;
    const auto req = resolveRequirements( goal );
    REQUIRE( req.has_value() );

    QVector<SceneCandidate> scenes;
    for ( int i = 0; i < 25; ++i )
    {
        SceneCandidate scene;
        scene.id = QStringLiteral( "scene-%1" ).arg( i, 2, 10, QLatin1Char( '0' ) );
        scene.state = AssetState::Ready;
        scene.gsdM = 999.0;
        scenes.append( scene );
    }
    SceneCandidate usable;
    usable.id = QStringLiteral( "fine" );
    usable.state = AssetState::Ready;
    usable.gsdM = 10.0;
    scenes.append( usable );

    const SuitabilityCriterion criterion = assessResolution( *req, scenes );
    REQUIRE( criterion.level == SuitabilityLevel::Marginal );
    REQUIRE( criterion.gaps.size() == 1 );
    REQUIRE( criterion.gaps.first().evidence.value( QStringLiteral( "out_of_range_scene_ids" ) )
                 .toArray().size() == 20 );
    REQUIRE( criterion.gaps.first().evidence.value( QStringLiteral( "omitted_count" ) ).toInt() == 5 );
}

TEST_CASE( "assessor fails typed on unusable inputs instead of guessing", "[suitability][spatial]" )
{
    // Goal failure passes through unchanged.
    SuitabilityAssessor::Inputs badGoal;
    badGoal.goal.profileKey = QStringLiteral( "nope" );
    const auto refusedGoal = SuitabilityAssessor::assess( badGoal );
    REQUIRE( !refusedGoal.has_value() );
    REQUIRE( refusedGoal.diagnostics().first().code == QStringLiteral( "suitability.profile_unknown" ) );

    // Too many scenes is an honest typed failure, never a silent truncation.
    SuitabilityAssessor::Inputs tooMany;
    for ( int i = 0; i <= SuitabilityAssessor::kMaxScenes; ++i )
    {
        SceneCandidate scene;
        scene.id = QStringLiteral( "scene-%1" ).arg( i );
        scene.state = AssetState::Ready;
        tooMany.scenes.append( scene );
    }
    const auto refusedCount = SuitabilityAssessor::assess( tooMany );
    REQUIRE( !refusedCount.has_value() );
    REQUIRE( refusedCount.diagnostics().first().code == QStringLiteral( "suitability.too_many_scenes" ) );

    // Nothing to assess at all -> empty subject.
    SuitabilityAssessor::Inputs empty;
    const auto refusedEmpty = SuitabilityAssessor::assess( empty );
    REQUIRE( !refusedEmpty.has_value() );
    REQUIRE( refusedEmpty.diagnostics().first().code == QStringLiteral( "suitability.empty_subject" ) );
}

TEST_CASE( "assessor produces a deterministic report for a legal subject", "[suitability][spatial]" )
{
    SuitabilityAssessor::Inputs inputs;
    inputs.goal = goalWithAoi( 0, 0, 100, 100 );
    inputs.goal.hasTimeWindow = true;
    inputs.goal.windowStartUtc = QDateTime::fromString( QStringLiteral( "2024-01-01T00:00:00Z" ), Qt::ISODate );
    inputs.goal.windowEndUtc = QDateTime::fromString( QStringLiteral( "2025-01-01T00:00:00Z" ), Qt::ISODate );
    inputs.datasetVersionId = QStringLiteral( "dv-1" );
    SceneCandidate scene = makeScene( QStringLiteral( "s1" ), 0, 0, 100, 100, QStringLiteral( "aoi-crs" ) );
    scene.acquisitionTimeUtc =
        QDateTime::fromString( QStringLiteral( "2024-06-01T00:00:00Z" ), Qt::ISODate );
    inputs.scenes.append( scene );

    const auto result = SuitabilityAssessor::assess( inputs );
    REQUIRE( result.has_value() );
    const SuitabilityReport report = result.value();
    REQUIRE( report.datasetVersionId() == QStringLiteral( "dv-1" ) );
    REQUIRE( report.sceneIds() == QStringList{ QStringLiteral( "s1" ) } );
    REQUIRE( report.goalDigest() == inputs.goal.contentDigest() );

    REQUIRE( report.criteria().size() == 7 );
    REQUIRE( report.criteria().at( 0 ).id == QStringLiteral( "quality.cloud" ) );
    REQUIRE( report.criteria().at( 1 ).id == QStringLiteral( "spatial.coverage" ) );
    REQUIRE( report.criteria().at( 2 ).id == QStringLiteral( "spatial.resolution" ) );
    REQUIRE( report.criteria().at( 3 ).id == QStringLiteral( "spectral.bands" ) );
    REQUIRE( report.criteria().at( 4 ).id == QStringLiteral( "temporal.coverage" ) );
    REQUIRE( report.criteria().at( 5 ).id == QStringLiteral( "temporal.density" ) );
    REQUIRE( report.criteria().at( 6 ).id == QStringLiteral( "temporal.seasonality" ) );
    // Every graded dimension is suitable or not-applicable (partial evidence
    // would hold the overall at Unknown).
    REQUIRE( report.overallLevel() == SuitabilityLevel::Suitable );

    // Deterministic replay: same inputs, same digest.
    const auto replay = SuitabilityAssessor::assess( inputs );
    REQUIRE( replay.has_value() );
    REQUIRE( replay->contentDigest() == report.contentDigest() );
    REQUIRE( replay->toJson() == report.toJson() );

    // Scenes present without an AOI stay a legal subject; coverage is Unknown.
    SuitabilityAssessor::Inputs noAoi;
    noAoi.scenes.append( makeScene( QStringLiteral( "s1" ), 0, 0, 100, 100 ) );
    const auto legalUnknown = SuitabilityAssessor::assess( noAoi );
    REQUIRE( legalUnknown.has_value() );
    const SuitabilityCriterion *coverage = nullptr;
    for ( const SuitabilityCriterion &criterion : legalUnknown->criteria() )
        if ( criterion.id == QLatin1String( "spatial.coverage" ) )
            coverage = &criterion;
    REQUIRE( coverage != nullptr );
    REQUIRE( coverage->level == SuitabilityLevel::Unknown );

    // A goal with an AOI but zero scenes is a legal subject judged Unsuitable
    // (a target with no data at all is infeasible, not unknown).
    SuitabilityAssessor::Inputs wishful;
    wishful.goal = goalWithAoi( 0, 0, 100, 100 );
    const auto infeasible = SuitabilityAssessor::assess( wishful );
    REQUIRE( infeasible.has_value() );
    REQUIRE( infeasible->overallLevel() == SuitabilityLevel::Unsuitable );
}
