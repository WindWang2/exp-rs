// test_obia_operators.cpp — rs:obia_* operator contract tests (#663).
//
// Kernel semantics stay pinned by the analysis-layer tests (test_obia_
// segmentation / test_object_hierarchy); these tests pin the OPERATOR layer:
// engine selection + fallback policy, feature/label CSV interchange
// (round-trip parity with the kernels), the interactive classify path
// (labels + segmentClasses, hyperparameters, accuracy, uncertainty sidecar,
// palette output), the hierarchy rehydrate path (classify without OTB), and
// boundary validation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#ifdef SICNU_HAS_OPENCV

#include "operators/rs/rs_obia_classify_operator.h"
#include "operators/rs/rs_obia_features_operator.h"
#include "operators/rs/rs_obia_hierarchy_operator.h"
#include "operators/rs/rs_obia_label_operator.h"
#include "operators/rs/rs_obia_segment_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include "analysis/segmentation/rs_object_hierarchy.h"
#include "analysis/segmentation/rs_otb_segmenter.h"
#include "analysis/segmentation/rs_roi_labeler.h"
#include "analysis/segmentation/rs_segment_features.h"
#include "analysis/segmentation/rs_segment_map.h"

#include <gdal.h>
#include <ogr_api.h>
#include <cpl_error.h>

#include <QColor>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

namespace
{

bool g_gdalInit = ( GDALAllRegister(), OGRRegisterAll(), true );

/// Two-half raster (left 100 / right 200), optional band count.
QString createTestRaster( const QString &dir, int w, int h, int nBands = 1,
                          const QString &name = QStringLiteral( "input.tif" ) )
{
    const QString path = dir + QLatin1Char( '/' ) + name;
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, nBands,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return {};
    QVector<float> row( w );
    for ( int b = 0; b < nBands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
                row[c] = ( c < w / 2 ) ? 100.0f : 200.0f;
            GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
        }
    }
    // Minimal square-cell geotransform so vector reprojection paths are exercised.
    double gt[6] = { 100.0, 1.0, 0.0, 200.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALClose( ds );
    return path;
}

/// Left/right two-segment label map (ids 1|2) written as a UInt32 GeoTIFF.
QString createTwoSegmentLabels( const QString &dir, int w, int h )
{
    QVector<quint32> labels( static_cast<size_t>( w ) * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
            labels[static_cast<size_t>( r ) * w + c] = ( c < w / 2 ) ? 1u : 2u;
    RsSegmentMap segMap( labels, w, h );
    const QString ref = createTestRaster( dir, w, h, 1, QStringLiteral( "ref.tif" ) );
    const QString path = dir + QStringLiteral( "/labels.tif" );
    QString err;
    REQUIRE( segMap.toGeoTIFF( path, ref, &err ) );
    return path;
}

// Stripe boundaries with four DISTINCT widths so per-segment geometry
// features (area, perimeter, aspectRatio, ...) differ — equal-width stripes
// make those columns constant, and ANN training fails on the scaled zeros.
const int kStripeEdges[5] = { 0, 4, 7, 12, 16 };

int stripeIndex( int c, int w )
{
    // w is 16 in the fixtures; scale boundaries proportionally for safety.
    const double x = static_cast<double>( c ) * 16.0 / w;
    for ( int i = 3; i >= 0; --i )
        if ( x >= kStripeEdges[i] )
            return i;
    return 0;
}

/// Four vertical-stripe segments (ids 1..4) written as a UInt32 GeoTIFF.
QString createFourSegmentLabels( const QString &dir, int w, int h )
{
    QVector<quint32> labels( static_cast<size_t>( w ) * h );
    for ( int r = 0; r < h; ++r )
        for ( int c = 0; c < w; ++c )
            labels[static_cast<size_t>( r ) * w + c] =
                static_cast<quint32>( 1 + stripeIndex( c, w ) );
    RsSegmentMap segMap( labels, w, h );
    const QString ref = createTestRaster( dir, w, h, 1, QStringLiteral( "ref4.tif" ) );
    const QString path = dir + QStringLiteral( "/labels4.tif" );
    QString err;
    REQUIRE( segMap.toGeoTIFF( path, ref, &err ) );
    return path;
}

/// Raster with a DISTINCT DN level per vertical stripe (stripe b → 50·b on
/// every band) plus deterministic per-pixel variation — per-segment feature
/// rows stay mutually consistent and non-degenerate (constant segments give
/// zero-variance GLCM columns whose z-score breaks ANN training).
QString createFourLevelRaster( const QString &dir, int w, int h, int nBands )
{
    const QString path = dir + QStringLiteral( "/four_level.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, nBands,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return {};
    QVector<float> row( w );
    for ( int b = 0; b < nBands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
                row[c] = 50.0f * static_cast<float>( 1 + stripeIndex( c, w ) )
                         + static_cast<float>( ( r * 7 + c * 3 + b ) % 11 );
            GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
        }
    }
    GDALClose( ds );
    return path;
}

/// GPKG training square with an integer class field (test_object_hierarchy pattern).
void writeTrainingSquare( const QString &path, const QString &fieldName, int classId,
                          double x0, double y0, double x1, double y1, bool append = false )
{
    GDALDriverH drv = GDALGetDriverByName( "GPKG" );
    REQUIRE( drv != nullptr );
    GDALDatasetH ds = append
        ? GDALOpenEx( path.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_UPDATE,
                      nullptr, nullptr, nullptr )
        : GDALCreate( drv, path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
    REQUIRE( ds != nullptr );
    OGRLayerH lyr = append
        ? GDALDatasetGetLayer( ds, 0 )
        : GDALDatasetCreateLayer( ds, "training", nullptr, wkbPolygon, nullptr );
    REQUIRE( lyr != nullptr );
    if ( !append )
    {
        OGRFieldDefnH fld = OGR_Fld_Create( fieldName.toUtf8().constData(), OFTInteger );
        REQUIRE( OGR_L_CreateField( lyr, fld, true ) == OGRERR_NONE );
        OGR_Fld_Destroy( fld );
    }
    OGRGeometryH ring = OGR_G_CreateGeometry( wkbLinearRing );
    OGR_G_AddPoint_2D( ring, x0, y0 );
    OGR_G_AddPoint_2D( ring, x1, y0 );
    OGR_G_AddPoint_2D( ring, x1, y1 );
    OGR_G_AddPoint_2D( ring, x0, y1 );
    OGR_G_AddPoint_2D( ring, x0, y0 );
    OGRGeometryH poly = OGR_G_CreateGeometry( wkbPolygon );
    REQUIRE( OGR_G_AddGeometryDirectly( poly, ring ) == OGRERR_NONE );
    OGRFeatureH feat = OGR_F_Create( OGR_L_GetLayerDefn( lyr ) );
    OGR_F_SetFieldInteger( feat, 0, classId );
    REQUIRE( OGR_F_SetGeometryDirectly( feat, poly ) == OGRERR_NONE );
    REQUIRE( OGR_L_CreateFeature( lyr, feat ) == OGRERR_NONE );
    OGR_F_Destroy( feat );
    GDALClose( ds );
}

using namespace sicnu::operators;
using namespace sicnu::operators::rs;

} // namespace

// ---------------------------------------------------------------------------
// Registry + schema contract
// ---------------------------------------------------------------------------

TEST_CASE( "rs:obia operators are registered with schema contracts", "[obia][operator][registry]" )
{
    auto &registry = RSOperatorRegistry::instance();
    for ( const char *id : { "rs:obia_segment", "rs:obia_features", "rs:obia_label",
                             "rs:obia_classify", "rs:obia_hierarchy" } )
    {
        auto op = registry.create( id );
        INFO( id );
        REQUIRE( op != nullptr );
        const Json::Value schema = op->schema();
        REQUIRE( schema["properties"].isObject() );
        REQUIRE( schema["properties"].size() > 0 );
    }
}

TEST_CASE( "rs:obia_segment schema declares engine + OTB parameters", "[obia][operator][schema]" )
{
    RsObiaSegmentOperator op;
    const Json::Value schema = op.schema();
    REQUIRE( schema["properties"]["engine"]["default"].asString() == "simple" );
    for ( const char *p : { "spatialRadius", "rangeRadius", "maxIterations", "threshold",
                            "smoothKernel", "quantizeBins", "minRegionSize" } )
    {
        INFO( p );
        REQUIRE( schema["properties"].isMember( p ) );
    }

    // Value-level defaults — the single source of truth the OBIA GUI toolbar
    // (and RsObiaOperatorAdapter::SegmentOptions) initializes from (#663).
    REQUIRE( schema["properties"]["smoothKernel"]["default"].asInt() == 5 );
    REQUIRE( schema["properties"]["quantizeBins"]["default"].asInt() == 32 );
    REQUIRE( schema["properties"]["minRegionSize"]["default"].asInt() == 50 );
    REQUIRE( schema["properties"]["spatialRadius"]["default"].asInt() == 5 );
    REQUIRE( schema["properties"]["rangeRadius"]["default"].asDouble() == 15.0 );
    REQUIRE( schema["properties"]["maxIterations"]["default"].asInt() == 100 );
    REQUIRE( schema["properties"]["threshold"]["default"].asDouble() == 0.1 );
}

// ---------------------------------------------------------------------------
// rs:obia_segment — engine matrix
// ---------------------------------------------------------------------------

TEST_CASE( "rs:obia_segment simple engine writes a rehydratable label raster", "[obia][operator][segment]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = createTestRaster( tmp.path(), 16, 16 );
    REQUIRE( !input.isEmpty() );
    const QString output = tmp.path() + QStringLiteral( "/labels.tif" );

    RsObiaSegmentOperator op;
    RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["engine"] = "simple";
    params["smoothKernel"] = 3;
    params["quantizeBins"] = 4;
    params["minRegionSize"] = 10;

    const Json::Value result = op.run( params, ctx );
    REQUIRE( result["engine"].asString() == "simple" );
    REQUIRE( result["segments"].asInt() >= 1 );
    REQUIRE( QFile::exists( output ) );

    // Round-trip: the output is a valid session label raster (the GUI adapter
    // rehydrates RsSegmentMap from exactly this file).
    const RsSegmentMap segMap = RsSegmentMap::fromGeoTIFF( output );
    REQUIRE( !segMap.isEmpty() );
    REQUIRE( segMap.width() == 16 );
    REQUIRE( segMap.height() == 16 );
    REQUIRE( segMap.segmentCount() >= 1 );
}

TEST_CASE( "rs:obia_segment auto engine falls back without OTB", "[obia][operator][segment][engine]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = createTestRaster( tmp.path(), 16, 16 );
    const QString output = tmp.path() + QStringLiteral( "/labels.tif" );

    RsObiaSegmentOperator op;
    RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["engine"] = "auto";
    params["smoothKernel"] = 3;
    params["quantizeBins"] = 4;

    const Json::Value result = op.run( params, ctx );
    // With OTB: engine=otb; without: the ADR 0058 teaching fallback.
    const std::string engine = result["engine"].asString();
    REQUIRE( ( engine == "otb" || engine == "simple" ) );
    if ( engine == "simple" )
        REQUIRE( result["segments"].asInt() >= 1 );
}

TEST_CASE( "rs:obia_segment otb engine fails closed without OTB", "[obia][operator][segment][engine]" )
{
    QTemporaryDir tmp;
    const QString input = createTestRaster( tmp.path(), 16, 16 );
    const QString output = tmp.path() + QStringLiteral( "/labels.tif" );

    RsObiaSegmentOperator op;
    RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["engine"] = "otb";

    if ( !RsOtbSegmenter::isAvailable() )
    {
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    else
    {
        const Json::Value result = op.run( params, ctx );
        REQUIRE( result["engine"].asString() == "otb" );
    }
}

TEST_CASE( "rs:obia_segment validates engine parameters", "[obia][operator][segment][validation]" )
{
    QTemporaryDir tmp;
    const QString input = createTestRaster( tmp.path(), 16, 16 );

    RsObiaSegmentOperator op;
    RSOperatorContext ctx;

    SECTION( "even smoothKernel rejected for engines that may run simple" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = ( tmp.path() + "/o.tif" ).toStdString();
        params["engine"] = "simple";
        params["smoothKernel"] = 4;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    SECTION( "quantizeBins out of range rejected" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = ( tmp.path() + "/o.tif" ).toStdString();
        params["engine"] = "simple";
        params["quantizeBins"] = 0;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    SECTION( "negative spatialRadius rejected even when only OTB may run" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = ( tmp.path() + "/o.tif" ).toStdString();
        params["engine"] = "otb";
        params["spatialRadius"] = 0;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
}

// ---------------------------------------------------------------------------
// rs:obia_features — CSV interchange parity
// ---------------------------------------------------------------------------

TEST_CASE( "rs:obia_features CSV round-trips kernel statistics", "[obia][operator][features]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = createTestRaster( tmp.path(), 16, 16, 2 );
    const QString labels = createTwoSegmentLabels( tmp.path(), 16, 16 );
    const QString csv = tmp.path() + QStringLiteral( "/features.csv" );

    RsObiaFeaturesOperator op;
    RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["labels"] = labels.toStdString();
    params["output"] = csv.toStdString();

    const Json::Value result = op.run( params, ctx );
    REQUIRE( result["segments"].asInt() == 2 );
    REQUIRE( result["bands"].asInt() == 2 );
    REQUIRE( result["features"].asInt() == 6 + 8 * 2 );
    REQUIRE( QFile::exists( csv ) );

    // Kernel parity: same input, same statistics as the analysis layer.
    const RsSegmentMap segMap = RsSegmentMap::fromGeoTIFF( labels );
    const auto kernelStats = RsSegmentFeatures::extract( input, segMap, { 1, 2 } );
    REQUIRE( kernelStats.size() == 2 );

    QFile f( csv );
    REQUIRE( f.open( QIODevice::ReadOnly | QIODevice::Text ) );
    QTextStream in( &f );
    const QStringList header = in.readLine().split( ',' );
    REQUIRE( header.first() == QStringLiteral( "segment_id" ) );
    REQUIRE( header.contains( QStringLiteral( "mean_b1" ) ) );
    REQUIRE( header.contains( QStringLiteral( "glcm_contrast_b2" ) ) );
    int rows = 0;
    while ( !in.atEnd() )
    {
        const QStringList row = in.readLine().split( ',' );
        REQUIRE( row.size() == header.size() );
        ++rows;
    }
    REQUIRE( rows == 2 );

    // Value parity on one stat (area) via a fresh kernel read: half the raster.
    REQUIRE( kernelStats.constBegin().value().area == 16 * 8 );
}

TEST_CASE( "rs:obia_features rejects grid-mismatched labels", "[obia][operator][features][validation]" )
{
    QTemporaryDir tmp;
    const QString input = createTestRaster( tmp.path(), 16, 16 );
    const QString labels = createTwoSegmentLabels( tmp.path(), 32, 32 );

    RsObiaFeaturesOperator op;
    RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["labels"] = labels.toStdString();
    params["output"] = ( tmp.path() + "/f.csv" ).toStdString();
    REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
}

// ---------------------------------------------------------------------------
// rs:obia_label — RsRoiLabeler parity
// ---------------------------------------------------------------------------

TEST_CASE( "rs:obia_label emits the canonical majority labels", "[obia][operator][label]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    // Geotransform (100,200) 1m cells: columns 100..115 / rows 185..199.
    const QString input = createTestRaster( tmp.path(), 16, 16 );
    const QString labels = createTwoSegmentLabels( tmp.path(), 16, 16 );
    const QString vec = tmp.path() + QStringLiteral( "/train.gpkg" );
    // Class 7 over the left half (segment 1), class 9 over the right (segment 2).
    writeTrainingSquare( vec, QStringLiteral( "class_id" ), 7, 100.0, 184.1, 107.9, 199.9 );
    writeTrainingSquare( vec, QStringLiteral( "class_id" ), 9, 108.1, 184.1, 115.9, 199.9, true );

    RsObiaLabelOperator op;
    RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["labels"] = labels.toStdString();
    params["training"] = vec.toStdString();
    params["output"] = ( tmp.path() + "/labels.csv" ).toStdString();

    const Json::Value result = op.run( params, ctx );
    REQUIRE( result["labeled"].asInt() == 2 );

    // Kernel parity: RsRoiLabeler over the same inputs.
    const RsSegmentMap segMap = RsSegmentMap::fromGeoTIFF( labels );
    QString err;
    const auto kernel = RsRoiLabeler::labelByMajority(
        segMap, input, vec, QStringLiteral( "class_id" ), 3, &err );
    REQUIRE( kernel.size() == 2 );
    REQUIRE( kernel.value( 1 ) == 7 );
    REQUIRE( kernel.value( 2 ) == 9 );

    QFile csv( tmp.path() + QStringLiteral( "/labels.csv" ) );
    REQUIRE( csv.open( QIODevice::ReadOnly | QIODevice::Text ) );
    QTextStream in( &csv );
    REQUIRE( in.readLine() == QStringLiteral( "segment_id,class_id" ) );
    QSet<int> seen;
    while ( !in.atEnd() )
    {
        const QStringList row = in.readLine().split( ',' );
        REQUIRE( row.size() == 2 );
        seen.insert( row.at( 1 ).toInt() );
    }
    REQUIRE( seen == QSet<int>{ 7, 9 } );
}

// ---------------------------------------------------------------------------
// rs:obia_classify — interactive labels path
// ---------------------------------------------------------------------------

namespace
{

Json::Value classifyParams( const QString &input, const QString &labels, const QString &output,
                            const char *method = "svm" )
{
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["labels"] = labels.toStdString();
    params["output"] = output.toStdString();
    Json::Value segmentClasses( Json::objectValue );
    segmentClasses["1"] = 1;
    segmentClasses["2"] = 2;
    params["segmentClasses"] = segmentClasses;
    params["method"] = method;
    return params;
}

} // namespace

TEST_CASE( "rs:obia_classify labels+segmentClasses path end to end", "[obia][operator][classify]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = createTestRaster( tmp.path(), 16, 16, 3 );
    const QString labels = createTwoSegmentLabels( tmp.path(), 16, 16 );
    const QString output = tmp.path() + QStringLiteral( "/classified.tif" );
    const QString uncertainty = tmp.path() + QStringLiteral( "/uncertainty.csv" );

    RsObiaClassifyOperator op;
    RSOperatorContext ctx;
    Json::Value params = classifyParams( input, labels, output );
    params["features"] = "full";
    Json::Value colors( Json::objectValue );
    colors["1"] = "#ff0000";
    colors["2"] = "#00ff00";
    params["classColors"] = colors;
    params["outputUncertainty"] = uncertainty.toStdString();

    const Json::Value result = op.run( params, ctx );
    REQUIRE( result["output"].asString() == output.toStdString() );
    REQUIRE( result["segments"].asInt() == 2 );
    REQUIRE( result["labeledSegments"].asInt() == 2 );
    REQUIRE( result["classes"].asInt() == 2 );
    REQUIRE( result["features"].asInt() > 3 ); // full model, not mean-only
    REQUIRE( QFile::exists( output ) );

    // Perfectly separable input → perfect training accuracy.
    REQUIRE( result["accuracy"]["overallAccuracy"].asDouble() == Catch::Approx( 1.0 ) );
    REQUIRE( result["accuracy"]["kappa"].asDouble() == Catch::Approx( 1.0 ) );
    REQUIRE( result["accuracy"]["classes"].size() == 2 );

    // Uncertainty sidecar carries the predicted classes.
    REQUIRE( QFile::exists( uncertainty ) );
    QFile csv( uncertainty );
    REQUIRE( csv.open( QIODevice::ReadOnly | QIODevice::Text ) );
    QTextStream in( &csv );
    REQUIRE( in.readLine().startsWith( QStringLiteral( "segment_id" ) ) );
    int rows = 0;
    while ( !in.atEnd() )
    {
        const QStringList row = in.readLine().split( ',' );
        REQUIRE( row.size() == 3 );
        ++rows;
    }
    REQUIRE( rows == 2 );

    // Palette output contract (GUI session canvas colors).
    GDALDatasetH ds = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALGetRasterColorTable( band ) != nullptr );
    REQUIRE( GDALGetRasterColorInterpretation( band ) == GCI_PaletteIndex );
    GDALClose( ds );
}

TEST_CASE( "rs:obia_classify featureSelection narrows the matrix", "[obia][operator][classify][features]" )
{
    QTemporaryDir tmp;
    const QString input = createTestRaster( tmp.path(), 16, 16, 3 );
    const QString labels = createTwoSegmentLabels( tmp.path(), 16, 16 );
    const QString output = tmp.path() + QStringLiteral( "/classified.tif" );

    RsObiaClassifyOperator op;
    RSOperatorContext ctx;
    Json::Value params = classifyParams( input, labels, output );
    params["features"] = "full";
    // Absent keys default to enabled (schema: "default all true"), so the
    // narrow matrix must disable the other families explicitly.
    Json::Value selection( Json::objectValue );
    for ( const char *key : { "stddev", "min", "max", "glcmContrast", "glcmCorrelation",
                              "glcmEnergy", "glcmHomogeneity", "area", "perimeter",
                              "shapeIndex", "compactness", "rectangularity", "aspectRatio" } )
        selection[key] = false;
    selection["mean"] = true;
    params["featureSelection"] = selection;

    const Json::Value result = op.run( params, ctx );
    REQUIRE( result["features"].asInt() == 3 ); // 3 bands, mean only
}

TEST_CASE( "rs:obia_classify supports every schema method", "[obia][operator][classify][method]" )
{
    QTemporaryDir tmp;
    // KMeans/MLP need a non-degenerate training matrix, so the matrix runs on
    // 4 segments × 3 bands with the full feature set and a distinct DN level
    // per segment (contradictory identical rows make ANN training fail).
    const QString input = createFourLevelRaster( tmp.path(), 16, 16, 3 );
    const QString labels = createFourSegmentLabels( tmp.path(), 16, 16 );

    for ( const char *method : { "svm", "normal_bayes", "random_forest", "kmeans", "mlp" } )
    {
        INFO( method );
        RsObiaClassifyOperator op;
        RSOperatorContext ctx;
        const QString output = tmp.path() + QStringLiteral( "/out_" ) + method + ".tif";
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["labels"] = labels.toStdString();
        params["output"] = output.toStdString();
        Json::Value segmentClasses( Json::objectValue );
        segmentClasses["1"] = 1;
        segmentClasses["2"] = 2;
        segmentClasses["3"] = 1;
        segmentClasses["4"] = 2;
        params["segmentClasses"] = segmentClasses;
        params["method"] = method;
        params["features"] = "full";
        if ( std::string( method ) == "mlp" )
        {
            // OpenCV 5.0.0's ANN_MLP (SIGMOID_SYM + RPROP) is known to produce
            // NaN output models on this build; RsMlpBackend::fit then refuses
            // (returns false) and the operator surfaces a typed OpenCvError —
            // the same documented branch the backend tests take
            // (test_classifier_mlp.cpp). Accept either the clean run or the
            // documented refusal; anything else is a contract break.
            try
            {
                const Json::Value result = op.run( params, ctx );
                REQUIRE( result["method"].asString() == method );
                REQUIRE( result["classes"].asInt() >= 1 );
            }
            catch ( const RSOperatorError &e )
            {
                REQUIRE( e.code() == ErrorCode::OpenCvError );
                REQUIRE( e.message().find( "training failed" ) != std::string::npos );
            }
            continue;
        }
        const Json::Value result = op.run( params, ctx );
        REQUIRE( result["method"].asString() == method );
        REQUIRE( result["classes"].asInt() >= 1 );
    }
}

TEST_CASE( "rs:obia_classify validates the training contract", "[obia][operator][classify][validation]" )
{
    QTemporaryDir tmp;
    const QString input = createTestRaster( tmp.path(), 16, 16 );
    const QString labels = createTwoSegmentLabels( tmp.path(), 16, 16 );

    RsObiaClassifyOperator op;
    RSOperatorContext ctx;

    SECTION( "both training sources" )
    {
        Json::Value params = classifyParams( input, labels, tmp.path() + "/o.tif" );
        params["training"] = ( tmp.path() + "/train.gpkg" ).toStdString();
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    SECTION( "neither training source" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = ( tmp.path() + "/o.tif" ).toStdString();
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    SECTION( "segmentClasses without labels" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["output"] = ( tmp.path() + "/o.tif" ).toStdString();
        Json::Value segmentClasses( Json::objectValue );
        segmentClasses["1"] = 1;
        params["segmentClasses"] = segmentClasses;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    SECTION( "unknown segment ids fail closed" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["labels"] = labels.toStdString();
        params["output"] = ( tmp.path() + "/o.tif" ).toStdString();
        Json::Value segmentClasses( Json::objectValue );
        segmentClasses["1"] = 1;
        segmentClasses["999"] = 2;
        params["segmentClasses"] = segmentClasses;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    SECTION( "single class rejected" )
    {
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["labels"] = labels.toStdString();
        params["output"] = ( tmp.path() + "/o.tif" ).toStdString();
        Json::Value segmentClasses( Json::objectValue );
        segmentClasses["1"] = 1;
        segmentClasses["2"] = 1; // same class
        params["segmentClasses"] = segmentClasses;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
    SECTION( "featureSelection rejected for features=mean" )
    {
        Json::Value params = classifyParams( input, labels, tmp.path() + "/o.tif" );
        Json::Value selection( Json::objectValue );
        selection["mean"] = true;
        params["featureSelection"] = selection;
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
}

TEST_CASE( "rs:obia_classify string-encoded segmentClasses (pipeline JSON)", "[obia][operator][classify]" )
{
    QTemporaryDir tmp;
    const QString input = createTestRaster( tmp.path(), 16, 16 );
    const QString labels = createTwoSegmentLabels( tmp.path(), 16, 16 );
    const QString output = tmp.path() + QStringLiteral( "/classified.tif" );

    RsObiaClassifyOperator op;
    RSOperatorContext ctx;
    Json::Value params = classifyParams( input, labels, output );
    // Simulate a CLI/pipeline JSON file where the object is quoted.
    params["segmentClasses"] = std::string( R"({"1": 1, "2": 2})" );

    const Json::Value result = op.run( params, ctx );
    REQUIRE( result["classes"].asInt() == 2 );
}

// ---------------------------------------------------------------------------
// rs:obia_hierarchy — rehydrate + classify without OTB
// ---------------------------------------------------------------------------

TEST_CASE( "rs:obia_hierarchy rehydrates labels and classifies without OTB", "[obia][operator][hierarchy]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString input = createTestRaster( tmp.path(), 16, 16, 3 );
    const QString fine = createTwoSegmentLabels( tmp.path(), 16, 16 );

    // Coarse level: one segment covering everything (id 3).
    {
        QVector<quint32> labels( 16 * 16, 3u );
        RsSegmentMap coarse( labels, 16, 16 );
        QString err;
        const QString coarsePath = tmp.path() + QStringLiteral( "/coarse.tif" );
        REQUIRE( coarse.toGeoTIFF( coarsePath, input, &err ) );
    }
    // Parent CSV: both fine segments roll up to coarse 3.
    {
        QFile f( tmp.path() + QStringLiteral( "/parents.csv" ) );
        REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Text ) );
        QTextStream out( &f );
        out << "fine_id,parent_id\n1,3\n2,3\n";
    }

    RsObiaHierarchyOperator op;
    RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["labelsFine"] = fine.toStdString();
    params["labelsCoarse"] = ( tmp.path() + "/coarse.tif" ).toStdString();
    params["parents"] = ( tmp.path() + "/parents.csv" ).toStdString();
    params["classifyLevel"] = 0;
    params["method"] = "svm";
    Json::Value segmentClasses( Json::objectValue );
    segmentClasses["1"] = 1;
    segmentClasses["2"] = 2;
    params["segmentClasses"] = segmentClasses;
    params["outputClass"] = ( tmp.path() + "/hier_class.tif" ).toStdString();

    // No OTB required in rehydrate mode — this must succeed even when the
    // OTB CLI is absent.
    const Json::Value result = op.run( params, ctx );
    REQUIRE( result["fineSegments"].asInt() == 2 );
    REQUIRE( result["coarseSegments"].asInt() == 1 );
    REQUIRE( result["levels"].asInt() == 2 );
    REQUIRE( result["labeledSegments"].asInt() == 2 );
    REQUIRE( result["accuracy"]["overallAccuracy"].asDouble() == Catch::Approx( 1.0 ) );
    REQUIRE( QFile::exists( QString::fromStdString( result["outputClass"].asString() ) ) );
}

TEST_CASE( "rs:obia_hierarchy build mode still fails closed without OTB", "[obia][operator][hierarchy][otb]" )
{
    if ( RsOtbSegmenter::isAvailable() )
        SUCCEED( "OTB present; fail-closed path not exercised" );
    else
    {
        QTemporaryDir tmp;
        const QString input = createTestRaster( tmp.path(), 16, 16 );
        RsObiaHierarchyOperator op;
        RSOperatorContext ctx;
        Json::Value params( Json::objectValue );
        params["input"] = input.toStdString();
        params["outputFine"] = ( tmp.path() + "/fine.tif" ).toStdString();
        REQUIRE_THROWS_AS( op.run( params, ctx ), RSOperatorError );
    }
}

#endif // SICNU_HAS_OPENCV
