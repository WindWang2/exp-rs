// tests/test_swarm_fix_agent_spatial.cpp
// swarm-review AG-C regression tests (2026-11-04) — agent spatial contract and
// tool fixes:
//   AG-C-1  datasetUnderstandingFromVectorInspect reads the per-layer
//           crs/featureCount/geometryType/fields under "layers[i]" (the shape
//           vector_inspect actually emits), never the document root.
//   AG-C-4  assess_result checks the band handle BEFORE summarizeBand
//           dereferences it, and reports the honest raster band count.
//   AG-C-7  class_count requires the expected class count to show up.
//   AG-C-5  vector_inspect computes the extent through a bounded feature scan
//           (fixture: DXF, whose driver stores no extent, so GetExtent(false)
//           fails and the capped fallback produces the bbox).
//   AG-C-6  a failed JM computation is NaN (not 0.0, which reads as "perfectly
//           inseparable") and ragged feature_correlations rows are width-checked.
//   AG-C-2  symbology:apply_raster_ramp refuses an invalid raster layer instead
//           of clamping into an empty band range and dereferencing a null
//           provider.
//   AG-C-9  workflow:preflight existence-checks path-role parameters only.
//   AG-C-10 spectral tools reject a malformed "point" instead of silently
//           falling back to the raster center and reporting success.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/contracts/spatial_contracts.h"
#include "agent/spatial_tools/spatial_tool.h"
#include "agent/spatial_tools/vector_inspect_tool.h"
#include "agent/tools/classification_tool.h"

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include <QFile>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_api.h>

#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::agent::spatial_tools;
using sicnu::agent::contracts::datasetUnderstandingFromVectorInspect;
using sicnu::agent::contracts::validateDatasetUnderstanding;

namespace {

void ensureGdalDrivers()
{
    static const bool kRegistered = [] {
        GDALAllRegister();
        OGRRegisterAll();
        return true;
    }();
    ( void )kRegistered;
}

SpatialToolResult callTool( const std::string &name, const Json::Value &input )
{
    const auto tool = SpatialToolRegistry::instance().find( name );
    if ( !tool )
        return SpatialToolResult::failure( "tool not registered: " + name, "NOT_FOUND", "validation" );
    return ( *tool )->execute( input );
}

/// Writes text content to @p path (parent dir must exist).
bool writeText( const QString &path, const std::string &content )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        return false;
    file.write( content.data(), static_cast<qint64>( content.size() ) );
    file.close();
    return true;
}

/// 3-point GeoJSON with one string and one real field, EPSG:4326.
std::string writeGeoJson( const QString &path )
{
    ensureGdalDrivers();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GeoJSON" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
    REQUIRE( ds != nullptr );

    OGRSpatialReference srs;
    srs.importFromEPSG( 4326 );
    OGRLayer *layer = ds->CreateLayer( "points", &srs, wkbPoint, nullptr );
    REQUIRE( layer != nullptr );

    OGRFieldDefn nameField( "name", OFTString );
    layer->CreateField( &nameField );
    OGRFieldDefn valueField( "value", OFTReal );
    layer->CreateField( &valueField );

    for ( int i = 0; i < 3; ++i )
    {
        OGRFeature *feature = OGRFeature::CreateFeature( layer->GetLayerDefn() );
        feature->SetField( "name", ( std::string( "p" ) + std::to_string( i ) ).c_str() );
        feature->SetField( "value", 1.5 * ( i + 1 ) );
        OGRPoint point( 116.0 + 0.1 * i, 39.0 + 0.1 * i );
        feature->SetGeometry( &point );
        REQUIRE( layer->CreateFeature( feature ) == OGRERR_NONE );
        OGRFeature::DestroyFeature( feature );
    }
    GDALClose( ds );
    return path.toStdString();
}

/// Hand-written DXF with two POINT entities. The DXF driver keeps no stored
/// extent, so layer->GetExtent(force=false) FAILS — the pre-fix code then ran
/// GetExtent(force=true), an unbounded full-file scan (#644 / AG-C-5).
std::string writeDxf( const QString &path )
{
    const char *dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nPOINT\n8\n0\n10\n116.0\n20\n39.0\n30\n0.0\n"
        "0\nPOINT\n8\n0\n10\n117.5\n20\n40.25\n30\n0.0\n"
        "0\nENDSEC\n0\nEOF\n";
    REQUIRE( writeText( path, dxf ) );
    return path.toStdString();
}

/// 8x8, 2-band Float32 GeoTIFF in UTM 50N with WAVELENGTH metadata on both
/// bands (842 nm NIR, 665 nm Red) and reflectance in [0, 1].
std::string writeSpectralTiff( const QString &path )
{
    ensureGdalDrivers();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );

    double geoTransform[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( geoTransform );
    OGRSpatialReference srs;
    srs.importFromEPSG( 32650 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );

    GDALRasterBand *nir = ds->GetRasterBand( 1 );
    nir->SetMetadataItem( "WAVELENGTH", "842", nullptr );
    nir->SetMetadataItem( "WAVELENGTH_UNITS", "nm", nullptr );
    GDALRasterBand *red = ds->GetRasterBand( 2 );
    red->SetMetadataItem( "WAVELENGTH", "665", nullptr );
    red->SetMetadataItem( "WAVELENGTH_UNITS", "nm", nullptr );

    std::vector<float> row( 8, 0.3f );
    for ( int y = 0; y < 8; ++y )
        nir->RasterIO( GF_Write, 0, y, 8, 1, row.data(), 8, 1, GDT_Float32, 0, 0 );
    std::vector<float> row2( 8, 0.2f );
    for ( int y = 0; y < 8; ++y )
        red->RasterIO( GF_Write, 0, y, 8, 1, row2.data(), 8, 1, GDT_Float32, 0, 0 );

    GDALClose( ds );
    return path.toStdString();
}

/// 8x8, 2-band Float32 GeoTIFF whose band 1 cycles exactly three distinct
/// values (1, 2, 3) — the categorical-output shape the class_count check
/// audits. Band 2 is constant filler.
std::string writeClassTiff( const QString &path )
{
    ensureGdalDrivers();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );

    double geoTransform[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( geoTransform );
    OGRSpatialReference srs;
    srs.importFromEPSG( 32650 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );

    GDALRasterBand *cls = ds->GetRasterBand( 1 );
    for ( int y = 0; y < 8; ++y )
    {
        float row[8];
        for ( int x = 0; x < 8; ++x )
            row[x] = static_cast<float>( ( x + y ) % 3 + 1 );
        cls->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
    }
    GDALRasterBand *filler = ds->GetRasterBand( 2 );
    std::vector<float> constant( 8, 5.0f );
    for ( int y = 0; y < 8; ++y )
        filler->RasterIO( GF_Write, 0, y, 8, 1, constant.data(), 8, 1, GDT_Float32, 0, 0 );

    GDALClose( ds );
    return path.toStdString();
}

const Json::Value *findCheck( const Json::Value &assessment, const std::string &name )
{
    const Json::Value &checks = assessment[ "checks" ];
    if ( !checks.isArray() )
        return nullptr;
    for ( const auto &check : checks )
        if ( check.isMember( "check" ) && check[ "check" ].asString() == name )
            return &check;
    return nullptr;
}

Json::Value params( std::initializer_list<std::pair<const char *, Json::Value>> entries )
{
    Json::Value out( Json::objectValue );
    for ( const auto &entry : entries )
        out[ entry.first ] = entry.second;
    return out;
}

Json::Value pointParam( double x, double y )
{
    Json::Value point( Json::arrayValue );
    point.append( x );
    point.append( y );
    return point;
}

} // namespace

// ---------------------------------------------------------------------------
// AG-C-1 — vector dataset understanding reads the layers[i] shape
// ---------------------------------------------------------------------------

TEST_CASE( "Vector understanding adapts the real vector_inspect output shape (AG-C-1)",
           "[swarm][agent][contracts][vector]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    QTemporaryDir dir;
    const std::string path = writeGeoJson( dir.filePath( "points.geojson" ) );

    const auto result = callTool( "spatial:vector_inspect", params( { { "path", path } } ) );
    REQUIRE( result.success );
    REQUIRE( result.output[ "layers" ].isArray() );

    const Json::Value doc = datasetUnderstandingFromVectorInspect( result.output );
    CHECK( validateDatasetUnderstanding( doc ).empty() );
    CHECK( doc[ "source_kind" ].asString() == "vector" );
    CHECK( doc[ "crs" ].asString() == "EPSG:4326" );
    CHECK( doc[ "feature_count" ].asInt64() == 3 );
    CHECK( doc[ "geometry_type" ].asString() == "Point" );
    REQUIRE( doc[ "fields" ].isArray() );
    CHECK( doc[ "fields" ].size() == 2 );
    CHECK( doc[ "fields" ][ 0 ][ "name" ].asString() == "name" );
    CHECK( doc[ "layer_count" ].asInt() == 1 );

    // The regression: pre-fix every slot was stamped "unknown" because the
    // adapter read crs/featureCount/... from the document root.
    CHECK( doc[ "fact_status" ][ "crs" ].asString() == "known" );
    CHECK( doc[ "fact_status" ][ "feature_count" ].asString() == "known" );
    CHECK( doc[ "fact_status" ][ "geometry_type" ].asString() == "known" );
    CHECK( doc[ "fact_status" ][ "fields" ].asString() == "known" );
    CHECK( doc[ "fact_status" ][ "extent" ].asString() == "known" );
}

TEST_CASE( "Vector understanding takes the first layer and discloses layer_count (AG-C-1)",
           "[swarm][agent][contracts][vector]" )
{
    Json::Value out( Json::objectValue );
    out[ "path" ] = "/data/multi.gpkg";
    out[ "driver" ] = "GPKG";
    out[ "layerCount" ] = 2;

    Json::Value layers( Json::arrayValue );
    Json::Value roads( Json::objectValue );
    roads[ "name" ] = "roads";
    roads[ "geometryType" ] = "Line String";
    roads[ "featureCount" ] = static_cast<Json::Int64>( 12 );
    roads[ "crs" ] = "EPSG:32650";
    Json::Value field( Json::objectValue );
    field[ "name" ] = "class";
    field[ "type" ] = "string";
    field[ "width" ] = 16;
    Json::Value fields( Json::arrayValue );
    fields.append( field );
    roads[ "fields" ] = fields;
    Json::Value extent( Json::objectValue );
    extent[ "minX" ] = 1.0;
    extent[ "minY" ] = 2.0;
    extent[ "maxX" ] = 3.0;
    extent[ "maxY" ] = 4.0;
    roads[ "extent" ] = extent;
    layers.append( roads );

    Json::Value points( Json::objectValue );
    points[ "name" ] = "pts";
    points[ "geometryType" ] = "Point";
    points[ "featureCount" ] = static_cast<Json::Int64>( 7 );
    points[ "crs" ] = "EPSG:4326";
    layers.append( points );
    out[ "layers" ] = layers;

    const Json::Value doc = datasetUnderstandingFromVectorInspect( out );
    CHECK( validateDatasetUnderstanding( doc ).empty() );
    // Multi-layer sources disclose the layer count; the scalar slots describe
    // layer 0 only (aggregating heterogeneous geometry types would lie).
    CHECK( doc[ "layer_count" ].asInt() == 2 );
    CHECK( doc[ "geometry_type" ].asString() == "Line String" );
    CHECK( doc[ "feature_count" ].asInt64() == 12 );
    CHECK( doc[ "crs" ].asString() == "EPSG:32650" );
    REQUIRE( doc[ "fields" ].isArray() );
    CHECK( doc[ "fields" ].size() == 1 );

    // The pre-fix shape (facts at the document root, no layers) carries no
    // per-layer facts at all — those slots stay honestly "unknown".
    Json::Value legacy( Json::objectValue );
    legacy[ "path" ] = "/data/legacy.geojson";
    legacy[ "crs" ] = "EPSG:4326";
    legacy[ "featureCount" ] = static_cast<Json::Int64>( 5 );
    const Json::Value legacyDoc = datasetUnderstandingFromVectorInspect( legacy );
    CHECK( legacyDoc[ "fact_status" ][ "crs" ].asString() == "unknown" );
    CHECK( legacyDoc[ "fact_status" ][ "feature_count" ].asString() == "unknown" );
    CHECK( !legacyDoc.isMember( "layer_count" ) );
}

// ---------------------------------------------------------------------------
// AG-C-5 — vector_inspect extent comes from a bounded feature scan
// ---------------------------------------------------------------------------

TEST_CASE( "VectorInspectTool computes the extent through the bounded scan (AG-C-5)",
           "[swarm][agent][spatial][vector]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    QTemporaryDir dir;
    const std::string path = writeDxf( dir.filePath( "points.dxf" ) );

    const auto result = callTool( "spatial:vector_inspect", params( { { "path", path } } ) );
    REQUIRE( result.success );
    const Json::Value layer = result.output[ "layers" ][ 0 ];
    // DXF stores no extent: GetExtent(false) fails and the capped feature
    // scan produces the bbox (pre-fix: an unbounded GetExtent(force=true)).
    REQUIRE( layer[ "extent" ].isObject() );
    CHECK( layer[ "extent" ][ "minX" ].asDouble() == 116.0 );
    CHECK( layer[ "extent" ][ "minY" ].asDouble() == 39.0 );
    CHECK( layer[ "extent" ][ "maxX" ].asDouble() == 117.5 );
    CHECK( layer[ "extent" ][ "maxY" ].asDouble() == 40.25 );
    CHECK( !layer.isMember( "extent_reason" ) );
    // featureCount keeps its honest "unknown" for drivers that cannot count
    // cheaply — the same contract the extent now follows.
    CHECK( layer[ "featureCount" ].asString() == "unknown" );
}

// ---------------------------------------------------------------------------
// AG-C-10 — malformed points are rejected, not silently centered
// ---------------------------------------------------------------------------

TEST_CASE( "Spectral tools reject malformed points instead of centering (AG-C-10)",
           "[swarm][agent][spatial][spectral]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    QTemporaryDir dir;
    const std::string path = writeSpectralTiff( dir.filePath( "spectral.tif" ) );

    // A well-formed point inside the raster succeeds.
    const auto ok = callTool( "spatial:spectral_inspect",
                              params( { { "path", path }, { "point", pointParam( 500060.0, 4999940.0 ) } } ) );
    REQUIRE( ok.success );
    CHECK( ok.output[ "reflectance" ].isArray() );

    // An absent point still takes the documented raster-center default.
    const auto centered = callTool( "spatial:spectral_inspect", params( { { "path", path } } ) );
    REQUIRE( centered.success );

    // Every malformed shape must fail structurally — never fall back to the
    // center and report success with the wrong spectrum.
    Json::Value shortPoint( Json::arrayValue );
    shortPoint.append( 500060.0 );
    Json::Value longPoint( Json::arrayValue );
    longPoint.append( 500060.0 );
    longPoint.append( 4999940.0 );
    longPoint.append( 0.0 );
    Json::Value stringPoint( Json::arrayValue );
    stringPoint.append( "500060" );
    stringPoint.append( "4999940" );

    const std::vector<Json::Value> malformed = {
        shortPoint, longPoint, stringPoint,
        Json::Value( "center" ), Json::Value( 42 ), Json::Value( Json::objectValue ),
    };
    for ( const auto &point : malformed )
    {
        const auto bad = callTool( "spatial:spectral_inspect",
                                   params( { { "path", path }, { "point", point } } ) );
        INFO( "point: " << point.toStyledString() );
        CHECK_FALSE( bad.success );
        CHECK( bad.errorCode == "INVALID_PARAMETER" );
        CHECK( bad.errorCategory == "validation" );
    }

    // Same contract on the sibling BOA physics tool.
    const auto boaBad = callTool( "spatial:validate_boa_physics",
                                  params( { { "path", path }, { "point", shortPoint } } ) );
    CHECK_FALSE( boaBad.success );
    CHECK( boaBad.errorCode == "INVALID_PARAMETER" );

    const auto boaOk = callTool( "spatial:validate_boa_physics",
                                 params( { { "path", path }, { "point", pointParam( 500060.0, 4999940.0 ) } } ) );
    REQUIRE( boaOk.success );
    CHECK( boaOk.output[ "has_anomaly" ].isBool() );
}

// ---------------------------------------------------------------------------
// AG-C-4 / AG-C-7 — assess_result band handling and class_count semantics
// ---------------------------------------------------------------------------

TEST_CASE( "AssessResult reports the honest band count and class_count semantics (AG-C-4/AG-C-7)",
           "[swarm][agent][spatial][assess]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    QTemporaryDir dir;
    const std::string path = writeClassTiff( dir.filePath( "classified.tif" ) );

    // The band handle is now validated BEFORE summarizeBand dereferences it,
    // and the reported band count is the dataset's real one (pre-fix the count
    // was clamped to >= 1, masking a 0-band dataset).
    const auto pass = callTool(
        "spatial:assess_result",
        params( { { "target", path },
                  { "expectations", params( { { "expected_classes", Json::Value( 3 ) } } ) } } ) );
    REQUIRE( pass.success );
    CHECK( pass.output[ "band_count" ].asInt() == 2 );
    CHECK( pass.output[ "verdict" ].asString() == "pass" );
    const Json::Value *classOk = findCheck( pass.output, "class_count" );
    REQUIRE( classOk != nullptr );
    CHECK( ( *classOk )[ "passed" ].asBool() );
    CHECK( ( *classOk )[ "details" ][ "observed_distinct_capped" ].asInt() == 3 );

    // Fewer distinguishable values than expected is the suspicion the check
    // exists for (pre-fix `observed <= expected` passed exactly then).
    const auto warn = callTool(
        "spatial:assess_result",
        params( { { "target", path },
                  { "expectations", params( { { "expected_classes", Json::Value( 5 ) } } ) } } ) );
    REQUIRE( warn.success );
    CHECK( warn.output[ "verdict" ].asString() == "warn" );
    const Json::Value *classBad = findCheck( warn.output, "class_count" );
    REQUIRE( classBad != nullptr );
    CHECK_FALSE( ( *classBad )[ "passed" ].asBool() );
    CHECK( ( *classBad )[ "code" ].asString() == "RESULT_CLASS_COUNT" );
}

TEST_CASE( "A zero-band raster yields a null band handle (AG-C-3/AG-C-4 precondition)",
           "[swarm][agent][spatial][gdal]" )
{
    // The guards in geometric_tool (GetRasterBand(1) dereference) and
    // assess_result (summarizeBand argument) exist for exactly this GDAL
    // contract: a dataset with no bands returns a NULL band handle. No on-disk
    // driver in this GDAL produces an openable 0-band raster (GTiff, VRT, HFA
    // and ERS all refuse to create one), so the precondition is anchored here
    // and the tool-level failure contracts are covered by the other cases.
    ensureGdalDrivers();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "MEM" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( "", 4, 4, 0, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    CHECK( ds->GetRasterCount() == 0 );
    CHECK( ds->GetRasterBand( 1 ) == nullptr );
    GDALClose( ds );
}

// ---------------------------------------------------------------------------
// AG-C-2 — apply_raster_ramp refuses an invalid raster layer
// ---------------------------------------------------------------------------

TEST_CASE( "ApplyRasterRamp refuses an invalid raster layer (AG-C-2)",
           "[swarm][agent][symbology]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();

    // A layer whose source cannot be opened: isValid() is false, the provider
    // exposes no bands (bandCount() == 0) — pre-fix the band clamp ran
    // std::clamp(v, 1, 0) (UB) and the statistics seam dereferenced the null
    // provider.
    auto *layer = new QgsRasterLayer( QStringLiteral( "/no/such/swarm/zeroband.tif" ),
                                      QStringLiteral( "swarm-broken-raster" ),
                                      QStringLiteral( "gdal" ) );
    QgsProject::instance()->addMapLayer( layer );

    const auto result = callTool( "symbology:apply_raster_ramp",
                                  params( { { "target", "swarm-broken-raster" } } ) );
    CHECK_FALSE( result.success );
    CHECK( result.errorCode == "EMPTY_DATA" );
    CHECK( result.errorCategory == "runtime" );

    QgsProject::instance()->removeMapLayer( layer );
}

// ---------------------------------------------------------------------------
// AG-C-6 — JM failures are NaN; ragged correlation matrices are width-checked
// ---------------------------------------------------------------------------

TEST_CASE( "JM computation failures report NaN, not zero separability (AG-C-6)",
           "[swarm][agent][classification]" )
{
    using rs::agent::ClassificationDiagnosisTool;
    const std::vector<double> mean = { 0.0, 0.0 };
    // Identical well-formed inputs still give the exact zero distance.
    const std::vector<double> cov = { 1.0, 0.0, 0.0, 1.0 };
    const double identical = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance(
        mean, cov, mean, cov, 2 );
    CHECK( identical == 0.0 );

    // Broken inputs (mismatched spans, non-positive dimension) are computation
    // FAILURES. Pre-fix they returned 0.0 — the "perfectly inseparable" value —
    // which fired WARN_SEVERE_SPECTRAL_CONFUSION on exactly the broken input
    // that carries no separability information at all.
    const double shortCov = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance(
        mean, cov, mean, std::vector<double> { 1.0 }, 2 );
    CHECK( std::isnan( shortCov ) );

    const double noDimension = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance(
        std::vector<double> {}, std::vector<double> {}, std::vector<double> {},
        std::vector<double> {}, 0 );
    CHECK( std::isnan( noDimension ) );

    const double negativeDimension = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance(
        mean, cov, mean, cov, -1 );
    CHECK( std::isnan( negativeDimension ) );
}

TEST_CASE( "Degenerate class stats do not trigger the spectral confusion warning (AG-C-6)",
           "[swarm][agent][classification]" )
{
    using rs::agent::ClassificationDiagnosisTool;

    auto jsonMatrix = []( const std::vector<std::vector<double>> &m ) {
        QJsonArray rows;
        for ( const auto &row : m )
        {
            QJsonArray r;
            for ( const double v : row )
                r.append( v );
            rows.append( r );
        }
        return rows;
    };

    // Class statistics whose covariance rows are too short for the declared
    // dimension: every pairwise JM computation fails. The warning must key off
    // real distances only, so a strong kappa stays OK and the failed pairs
    // serialize as JSON null (pre-fix: min JM 0.0 fired the severe confusion
    // warning on the broken input).
    auto run = [&]( const QJsonArray &means, const QJsonArray &covs ) {
        QJsonObject input;
        input.insert( QStringLiteral( "confusion_matrix" ), jsonMatrix( { { 95, 1 }, { 1, 95 } } ) );
        QJsonArray labels;
        labels.append( QStringLiteral( "water" ) );
        labels.append( QStringLiteral( "built" ) );
        input.insert( QStringLiteral( "class_labels" ), labels );
        QJsonObject stats;
        stats.insert( QStringLiteral( "means" ), means );
        stats.insert( QStringLiteral( "covs" ), covs );
        input.insert( QStringLiteral( "class_stats" ), stats );
        const ClassificationDiagnosisTool tool;
        return tool.execute( input );
    };

    const QJsonArray shortCovs = jsonMatrix( { { 1.0 }, { 1.0 } } );
    const QJsonObject out = run( jsonMatrix( { { 0.0, 0.0 }, { 100.0, 100.0 } } ), shortCovs );
    CHECK( out.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "OK" ) );
    CHECK_FALSE( out.contains( QStringLiteral( "code" ) ) );

    const QJsonArray jm = out.value( QStringLiteral( "jm_matrix" ) ).toArray();
    REQUIRE( jm.size() == 2 );
    CHECK( jm.at( 0 ).toArray().at( 1 ).isNull() );
    CHECK( jm.at( 1 ).toArray().at( 0 ).isNull() );

    // Empty means collapse the dimension to zero — same honest outcome.
    const QJsonObject empty = run( jsonMatrix( { {}, {} } ), jsonMatrix( { {}, {} } ) );
    CHECK( empty.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "OK" ) );

    // Well-formed separable statistics still warn-free with a real distance.
    const QJsonObject good =
        run( jsonMatrix( { { 0.0, 0.0 }, { 100.0, 100.0 } } ),
             jsonMatrix( { { 1.0, 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0, 1.0 } } ) );
    CHECK( good.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "OK" ) );
    const QJsonArray goodJm = good.value( QStringLiteral( "jm_matrix" ) ).toArray();
    REQUIRE( goodJm.size() == 2 );
    CHECK( goodJm.at( 0 ).toArray().at( 1 ).toDouble() == 2.0 );
}

TEST_CASE( "Ragged feature_correlations rows are width-checked (AG-C-6)",
           "[swarm][agent][classification]" )
{
    using rs::agent::ClassificationDiagnosisTool;

    auto jsonMatrix = []( const std::vector<std::vector<double>> &m ) {
        QJsonArray rows;
        for ( const auto &row : m )
        {
            QJsonArray r;
            for ( const double v : row )
                r.append( v );
            rows.append( r );
        }
        return rows;
    };

    QJsonObject input;
    input.insert( QStringLiteral( "confusion_matrix" ), jsonMatrix( { { 95, 1 }, { 1, 95 } } ) );
    QJsonArray labels;
    labels.append( QStringLiteral( "water" ) );
    labels.append( QStringLiteral( "built" ) );
    input.insert( QStringLiteral( "class_labels" ), labels );

    QJsonObject stats;
    QJsonArray gains;
    gains.append( 0.99 );
    gains.append( 0.01 );
    stats.insert( QStringLiteral( "gains" ), gains );
    QJsonArray names;
    names.append( QStringLiteral( "band_1_contrast" ) );
    names.append( QStringLiteral( "band_2_glcm_variance" ) );
    stats.insert( QStringLiteral( "feature_names" ), names );
    stats.insert( QStringLiteral( "means" ), jsonMatrix( { { 0.0, 0.0 }, { 100.0, 100.0 } } ) );
    stats.insert( QStringLiteral( "covs" ), jsonMatrix( { { 1.0, 0.0, 0.0, 1.0 },
                                                          { 1.0, 0.0, 0.0, 1.0 } } ) );
    // Ragged: the second row is one element short. Pre-fix the row-count check
    // passed and correlations[1][1] read past the end of the row.
    QJsonArray correlations;
    QJsonArray row0;
    row0.append( 1.0 );
    row0.append( 0.95 );
    correlations.append( row0 );
    QJsonArray row1;
    row1.append( 0.95 );
    correlations.append( row1 );
    stats.insert( QStringLiteral( "feature_correlations" ), correlations );
    input.insert( QStringLiteral( "class_stats" ), stats );

    const ClassificationDiagnosisTool tool;
    const QJsonObject out = tool.execute( input );
    const QJsonArray recommendations = out.value( QStringLiteral( "recommendations" ) ).toArray();
    for ( const auto &recommendation : recommendations )
        CHECK( recommendation.toObject().value( QStringLiteral( "action" ) ).toString() !=
               QStringLiteral( "PRUNE_FEATURE" ) );

    // A well-formed matrix still yields the prune recommendation.
    QJsonObject fixed = input;
    QJsonObject fixedStats = stats;
    fixedStats.insert( QStringLiteral( "feature_correlations" ),
                       jsonMatrix( { { 1.0, 0.95 }, { 0.95, 1.0 } } ) );
    fixed.insert( QStringLiteral( "class_stats" ), fixedStats );
    const QJsonObject outFixed = tool.execute( fixed );
    const QJsonArray fixedRecommendations =
        outFixed.value( QStringLiteral( "recommendations" ) ).toArray();
    bool foundPrune = false;
    for ( const auto &recommendation : fixedRecommendations )
    {
        const QJsonObject o = recommendation.toObject();
        if ( o.value( QStringLiteral( "action" ) ).toString() == QStringLiteral( "PRUNE_FEATURE" )
             && o.value( QStringLiteral( "feature_name" ) ).toString() ==
                    QStringLiteral( "band_2_glcm_variance" ) )
            foundPrune = true;
    }
    CHECK( foundPrune );
}

// ---------------------------------------------------------------------------
// AG-C-9 — preflight existence checks are keyed on path-role parameters
// ---------------------------------------------------------------------------

TEST_CASE( "Workflow preflight only existence-checks path-role parameters (AG-C-9)",
           "[swarm][agent][spatial][workflow]" )
{
    SpatialToolRegistry::instance().registerBuiltinTools();
    QTemporaryDir dir;
    const std::string raster = writeClassTiff( dir.filePath( "input.tif" ) );

    // Non-path parameters that merely contain '/' (MIME types, unit strings)
    // must not produce blocking WF_INPUT_NOT_FOUND issues.
    Json::Value step( Json::objectValue );
    step[ "id" ] = "ndvi";
    step[ "operatorId" ] = "rs:ndvi";
    step[ "params" ] = params( { { "input", raster },
                                 { "output", raster },
                                 { "format", "image/tiff" },
                                 { "units", "deg/sec" },
                                 { "projection", "EPSG:32650/UTM" } } );
    Json::Value steps( Json::arrayValue );
    steps.append( step );
    Json::Value workflow( Json::objectValue );
    workflow[ "id" ] = "path-heuristic-wf";
    workflow[ "steps" ] = steps;

    const auto result = callTool( "workflow:preflight", params( { { "workflow", workflow } } ) );
    REQUIRE( result.success );
    bool hasInputNotFound = false;
    for ( const auto &issue : result.output[ "issues" ] )
        hasInputNotFound = hasInputNotFound || issue[ "code" ].asString() == "WF_INPUT_NOT_FOUND";
    CHECK_FALSE( hasInputNotFound );

    // Negative control: a path-role parameter pointing at a missing file is
    // still reported.
    Json::Value brokenStep = step;
    brokenStep[ "params" ] = params( { { "input", "/no/such/swarm/input.tif" },
                                       { "output", raster } } );
    Json::Value brokenSteps( Json::arrayValue );
    brokenSteps.append( brokenStep );
    Json::Value brokenWorkflow( Json::objectValue );
    brokenWorkflow[ "id" ] = "missing-input-wf";
    brokenWorkflow[ "steps" ] = brokenSteps;
    const auto broken = callTool( "workflow:preflight",
                                  params( { { "workflow", brokenWorkflow } } ) );
    REQUIRE( broken.success );
    bool hasMissingInput = false;
    for ( const auto &issue : broken.output[ "issues" ] )
        hasMissingInput = hasMissingInput || issue[ "code" ].asString() == "WF_INPUT_NOT_FOUND";
    CHECK( hasMissingInput );
}

int main( int argc, char *argv[] )
{
    QgsApplication application( argc, argv, true );
    QgsApplication::initQgis();
    const int result = Catch::Session().run( argc, argv );
    QgsProject::instance()->clear();
    QgsApplication::exitQgis();
    return result;
}
