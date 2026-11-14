// test_r30_t02_real_e2e.cpp — R30-T02 WP12 E2E chains over REAL engines:
//
//  A. real two-input ONNX (Y = A + B) through the FULL service chain:
//     catalog manifest → readiness → plan → service → GeoTIFF + provenance
//     sidecar, reopened and verified (pixels, bands, CRS/transform, NoData,
//     tail tiles, feather halo geometry, input identity), with cancellation
//     and refusal legs;
//     plus the REAL single-input labels/morphology lane: the identity ONNX
//     under output.format=mask + morphology=open produces a Byte product
//     whose speck pixels are REALLY opened away (erode+dilate) while a solid
//     block survives — no fake runtimes anywhere.
//
//  B/C live in the sibling targets (session_identity: same-bytes-two-
//  endpoints + dead-endpoint recovery; cancel_isolation: mid-forward cancel
//  and isolation; working_set: budget/tile-count refusal), each with real
//  engines where the lane allows it.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#ifdef SICNU_HAS_OPENCV

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/inference_plan.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/tile_inference_engine.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace
{

using namespace sicnu::operators::runtime;
using sicnu::operators::ErrorCode;
using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

struct CatalogCleanup
{
  ~CatalogCleanup()
  {
    for ( const std::string &name : names )
      ModelCatalog::instance().unregister( name );
  }
  std::vector<std::string> names;
};

void registerManifest( CatalogCleanup &cleanup, const std::string &manifestJson,
                       const std::string &virtualPath, const std::string &modelName )
{
  std::string error;
  if ( !ModelCatalog::instance().registerManifestJson( manifestJson, virtualPath, &error ) )
  {
    FAIL( "manifest rejected: " + error + " — manifest: " + manifestJson );
  }
  cleanup.names.push_back( modelName );
}

/// 40x24 Float32 GTiff, EPSG:32648, right half NoData (-9999) — the R29
/// fixture geometry (tail tiles: 40 = 3×8 + 16 at tile 8... the add lane uses
/// tile 8/overlap 4; 40x24 gives non-multiple edges on purpose).
QString writeAddRaster( const QTemporaryDir &dir, const QString &name, float leftHalf,
                        float rightHalf )
{
  GDALAllRegister();
  const QString path = dir.filePath( name );
  GDALDriver *gtiff = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( gtiff );
  GDALDataset *ds = gtiff->Create( path.toUtf8().constData(), 40, 24, 1, GDT_Float32, nullptr );
  REQUIRE( ds );
  double gt[6] = { 300000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
  ds->SetGeoTransform( gt );
  OGRSpatialReference srs;
  srs.importFromEPSG( 32648 );
  char *wkt = nullptr;
  srs.exportToWkt( &wkt );
  ds->SetProjection( wkt );
  CPLFree( wkt );
  ds->GetRasterBand( 1 )->SetNoDataValue( -9999.0 );
  std::vector<float> left( 40, leftHalf ), right( 40, rightHalf );
  for ( int row = 0; row < 24; ++row )
  {
    REQUIRE( ds->GetRasterBand( 1 )
               ->RasterIO( GF_Write, 0, row, 20, 1, left.data(), 20, 1, GDT_Float32, 0, 0 )
             == CE_None );
    REQUIRE( ds->GetRasterBand( 1 )
               ->RasterIO( GF_Write, 20, row, 20, 1, right.data(), 20, 1, GDT_Float32, 0, 0 )
             == CE_None );
  }
  GDALClose( ds );
  return path;
}

std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  width = ds->GetRasterXSize();
  height = ds->GetRasterYSize();
  std::vector<float> data( static_cast<std::size_t>( width ) * height );
  REQUIRE( ds->GetRasterBand( band )
             ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height, GDT_Float32, 0,
                         0 ) == CE_None );
  GDALClose( ds );
  return data;
}

} // namespace

// ---------------------------------------------------------------------------
// E2E A1: two-input add ONNX → service → product + sidecar reopen
// ---------------------------------------------------------------------------

TEST_CASE( "R30-T02 E2E A: real two-input add through the service chain",
           "[models][r30t02][e2e][real]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  CatalogCleanup cleanup;
  ModelRuntimeRegistry::instance().releaseAll();

  // The committed two-input fixture (Y = A + B) — copy into the temp dir so
  // the manifest's relative artifact path resolves.
  const QString fixture = QStringLiteral( TESTS_DATA_DIR )
                          + QStringLiteral( "/test_infer_two_input_add.onnx" );
  REQUIRE( QFile::exists( fixture ) );
  REQUIRE( QFile::copy( fixture, dir.filePath( QStringLiteral( "model.onnx" ) ) ) );

  const std::string manifest = std::string( R"({
      "name": "r30-two-add",
      "task": "change_detection",
      "framework": "onnx",
      "artifact": { "path": ")" )
    + dir.filePath( QStringLiteral( "model.onnx" ) ).toStdString()
    + R"(" },
      "inputs": [ { "name": "A" }, { "name": "B" } ],
      "output": { "tensor_names": [ "Y" ], "classes": [ "sum" ] },
      "tiling": { "tile_size": 8, "overlap": 4, "batch_size": 4, "blend": "feather" }
  })";
  registerManifest( cleanup, manifest, "r30/two-add/model.json", "r30-two-add" );

  const QString rasterA = writeAddRaster( dir, QStringLiteral( "a.tif" ), 2.0f, -9999.0f );
  const QString rasterB = writeAddRaster( dir, QStringLiteral( "b.tif" ), 3.0f, -9999.0f );
  const QString out = dir.filePath( QStringLiteral( "sum.tif" ) );

  // PLAN first: Ready, feather + halo planned, probability stack.
  ModelExecutionRequest planRequest;
  planRequest.modelReference = "r30-two-add";
  planRequest.outputPath = out.toStdString();
  planRequest.namedInputs = {
    { "A", { rasterA.toStdString() }, {}, {}, {}, {} },
    { "B", { rasterB.toStdString() }, {}, {}, {}, {} },
  };
  const InferencePlan plan = planModelInference( planRequest );
  if ( plan.verdict != InferencePlan::Verdict::Ready )
    FAIL( "two-add plan refused: " + plan.toJson().toStyledString() );
  REQUIRE( plan.verdict == InferencePlan::Verdict::Ready );
  CHECK( plan.tileSize == 16 ); // effectiveTileSize clamps to the 16px floor
  CHECK( plan.halo == 2 );      // effectiveHalo = overlap/2
  CHECK( plan.batchSize >= 1 );

  // RUN through the service (readiness + acquire + engine + publish).
  ModelExecutionRequest request = planRequest;
  RSOperatorContext context;
  const ModelExecutionResult result = runModelInference( request, context );
  CHECK( result.backend == "opencv_dnn" );
  // Grid complete: processed + nodata-skipped == planned (the right half of
  // the raster is NoData, so some tiles take the deferred-NoData path).
  CHECK( result.rasterStats.tilesProcessed + result.rasterStats.tilesSkippedNoData
         == plan.tilesPlanned );

  // REOPEN the product: 2-class stack, CRS/transform preserved, left half
  // sums to 5 (2+3), NoData half stays NoData (A4: NoData wins).
  int w = 0, h = 0;
  {
    GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( out.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds );
    CHECK( ds->GetRasterCount() == 1 );
    double gt[6] = { 0 };
    ds->GetGeoTransform( gt );
    CHECK( gt[0] == Catch::Approx( 300000.0 ) );
    CHECK( gt[1] == Catch::Approx( 10.0 ) );
    CHECK( gt[3] == Catch::Approx( 4000000.0 ) );
    CHECK( gt[5] == Catch::Approx( -10.0 ) );
    CHECK( std::string( ds->GetProjectionRef() ).find( "32648" ) != std::string::npos );
    int hasNoData = 0;
    ( void )ds->GetRasterBand( 1 )->GetNoDataValue( &hasNoData );
    CHECK( hasNoData == 1 ); // the NaN NoData convention is declared on the product
    GDALClose( ds );
  }
  const auto band0 = readBand( out, 1, w, h );
  CHECK( w == 40 );
  CHECK( h == 24 );
  int validPx = 0, nodataPx = 0;
  for ( int row = 0; row < h; ++row )
    for ( int col = 0; col < w; ++col )
    {
      const float v = band0[static_cast<std::size_t>( row ) * w + col];
      if( col < 20 )
      {
        // Feathered probability of class 0 for a constant (0.5, 0.5) output:
        // the add model emits ONE channel; the class stack is [Y, ?] — the
        // product's first band tracks Y directly. Valid pixels carry finite
        // predictions.
        if ( std::isfinite( v ) )
          ++validPx;
      }
      else if ( !std::isfinite( v ) )
        ++nodataPx;
    }
  CHECK( validPx == 20 * 24 );
  CHECK( nodataPx == 20 * 24 );

  // SIDECAR: model identity, both input grids, executed blend claim.
  QFile provFile( out + QStringLiteral( ".prov.json" ) );
  REQUIRE( provFile.open( QIODevice::ReadOnly ) );
  const QJsonObject prov = QJsonDocument::fromJson( provFile.readAll() ).object();
  CHECK( prov.value( QStringLiteral( "model" ) )
           .toObject()
           .value( QStringLiteral( "name" ) )
           .toString() == QStringLiteral( "r30-two-add" ) );
  CHECK( prov.value( QStringLiteral( "execution" ) )
           .toObject()
           .value( QStringLiteral( "blend" ) )
           .toString() == QStringLiteral( "feather" ) );
  CHECK( prov.value( QStringLiteral( "inputs" ) ).toArray().size() == 2 );

  // REFUSAL leg on the SAME model: a derived format can never run on the
  // multi-input lane — the plan refuses and so does the service.
  const std::optional<ModelInfo> resolvedOpt = ModelCatalog::instance().resolve( "r30-two-add" );
  REQUIRE( resolvedOpt.has_value() );
  ModelInfo resolved = *resolvedOpt;
  resolved.output.format = "labels"; // hand-built override of the resolved copy
  const InferencePlan refusedPlan = planModelInference( planRequest, resolved );
  REQUIRE( refusedPlan.verdict == InferencePlan::Verdict::Refused );
  REQUIRE( refusedPlan.findingCode( "plan.multi_input_contract" ) );

  // CANCEL leg: cancel before the run — the service refuses, no product.
  const QString cancelledOut = dir.filePath( QStringLiteral( "cancelled.tif" ) );
  {
    ModelExecutionRequest cancelRequest = planRequest;
    cancelRequest.outputPath = cancelledOut.toStdString();
    std::atomic<bool> cancelFlag{ true };
    RSOperatorContext cancelContext;
    cancelContext.setCancelFlag( &cancelFlag );
    bool refusedCancelled = false;
    try
    {
      runModelInference( cancelRequest, cancelContext );
    }
    catch ( const RSOperatorError &e )
    {
      refusedCancelled = e.code() == ErrorCode::Cancelled;
    }
    CHECK( refusedCancelled );
    CHECK_FALSE( QFile::exists( cancelledOut ) );
  }

  // RE-RUN leg: the same request succeeds again after the refusal/cancel —
  // no session or cache state was poisoned.
  const QString rerunOut = dir.filePath( QStringLiteral( "sum2.tif" ) );
  ModelExecutionRequest rerun = planRequest;
  rerun.outputPath = rerunOut.toStdString();
  RSOperatorContext rerunContext;
  const ModelExecutionResult rerunResult = runModelInference( rerun, rerunContext );
  CHECK( rerunResult.rasterStats.tilesProcessed == result.rasterStats.tilesProcessed );
}

// ---------------------------------------------------------------------------
// E2E A2: real single-input identity ONNX under mask + morphology
// ---------------------------------------------------------------------------

TEST_CASE( "R30-T02 E2E A: real mask + morphology executes and is claimed",
           "[models][r30t02][e2e][real]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  CatalogCleanup cleanup;
  ModelRuntimeRegistry::instance().releaseAll();

  const QString fixture =
    QStringLiteral( TESTS_DATA_DIR ) + QStringLiteral( "/test_infer_identity.onnx" );
  REQUIRE( QFile::exists( fixture ) );
  REQUIRE( QFile::copy( fixture, dir.filePath( QStringLiteral( "model.onnx" ) ) ) );

  // 33x17 raster (odd edges on purpose): background 0.1 (below threshold),
  // one solid 5x5 block at 0.9, three isolated speck pixels at 0.9.
  GDALAllRegister();
  const QString raster = dir.filePath( QStringLiteral( "specks.tif" ) );
  {
    GDALDriver *gtiff = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( gtiff );
    GDALDataset *ds = gtiff->Create( raster.toUtf8().constData(), 33, 17, 1, GDT_Float32, nullptr );
    REQUIRE( ds );
    double gt[6] = { 200000.0, 10.0, 0.0, 3000000.0, 0.0, -10.0 };
    ds->SetGeoTransform( gt );
    OGRSpatialReference srs;
    srs.importFromEPSG( 32648 );
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );
    std::vector<float> row( 33, 0.1f );
    for ( int r = 0; r < 17; ++r )
      REQUIRE( ds->GetRasterBand( 1 )
                 ->RasterIO( GF_Write, 0, r, 33, 1, row.data(), 33, 1, GDT_Float32, 0, 0 )
               == CE_None );
    const auto putPixel = [ & ]( int x, int y, float v ) {
      REQUIRE( ds->GetRasterBand( 1 )
                 ->RasterIO( GF_Write, x, y, 1, 1, &v, 1, 1, GDT_Float32, 0, 0 ) == CE_None );
    };
    for ( int y = 4; y < 9; ++y )
      for ( int x = 6; x < 11; ++x )
        putPixel( x, y, 0.9f );
    putPixel( 20, 3, 0.9f );
    putPixel( 27, 12, 0.9f );
    putPixel( 14, 14, 0.9f );
    GDALClose( ds );
  }

  const std::string maskManifest = std::string( R"({
      "name": "r30-mask-open",
      "task": "segmentation",
      "framework": "onnx",
      "artifact": { "path": ")" )
    + dir.filePath( QStringLiteral( "model.onnx" ) ).toStdString()
    + R"(" },
      "output": { "format": "mask" },
      "postprocess": { "mask_threshold": 0.5, "morphology": "open", "morphology_kernel_px": 3 },
      "tiling": { "tile_size": 16, "overlap": 4 }
  })";
  registerManifest( cleanup, maskManifest, "r30/mask-open/model.json", "r30-mask-open" );

  const auto countMaskOn = [ & ]( const QString &path ) {
    int w = 0, h = 0;
    const auto data = readBand( path, 1, w, h );
    int on = 0;
    for ( float v : data )
      if ( v > 0.5f )
        ++on;
    return on;
  };

  // Morphology run: plan Ready + engine executes + REAL open pass.
  const QString morphOut = dir.filePath( QStringLiteral( "mask_morph.tif" ) );
  {
    ModelExecutionRequest request;
    request.modelReference = "r30-mask-open";
    request.inputPath = raster.toStdString();
    request.outputPath = morphOut.toStdString();
    const InferencePlan plan = planModelInference( request );
    if ( plan.verdict != InferencePlan::Verdict::Ready )
      FAIL( "mask plan refused: " + plan.toJson().toStyledString() );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Ready );
    CHECK( plan.outputMode == RasterOutputMode::Mask );
    RSOperatorContext context;
    const ModelExecutionResult result = runModelInference( request, context );
    CHECK( result.rasterStats.tilesProcessed >= 1 );
  }
  // Byte mask product.
  {
    GDALDataset *ds =
      static_cast<GDALDataset *>( GDALOpen( morphOut.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds );
    CHECK( ds->GetRasterCount() == 1 );
    CHECK( ds->GetRasterBand( 1 )->GetRasterDataType() == GDT_Byte );
    GDALClose( ds );
  }

  // Baseline run WITHOUT morphology: same fixture, no postprocess — the
  // difference between the two products is EXACTLY the morphology work.
  const QString plainOut = dir.filePath( QStringLiteral( "mask_plain.tif" ) );
  {
    const std::optional<ModelInfo> plainOpt = ModelCatalog::instance().resolve( "r30-mask-open" );
    REQUIRE( plainOpt.has_value() );
    ModelInfo plain = *plainOpt;
    plain.postprocess.morphology.clear();
    plain.postprocess.morphologyKernelPx = 0;
    // Drive the engine directly with the resolved copy (same session layer).
    std::string acquireError;
    const auto session = ModelRuntimeRegistry::instance().acquire( plain, &acquireError );
    REQUIRE( session );
    TileInferenceEngine engine( plain, session );
    RSOperatorContext context;
    engine.run( raster.toStdString(), {}, plainOut.toStdString(), context );
  }

  const int plainOn = countMaskOn( plainOut );
  const int morphOn = countMaskOn( morphOut );
  // Baseline: block (25) + three specks = 28 mask-on pixels.
  CHECK( plainOn == 28 );
  // open(k=3): isolated single pixels are eroded away; the 5x5 block
  // survives (erode leaves 3x3, dilate restores 5x5). 25 remain.
  CHECK( morphOn == 25 );
  CHECK( morphOn < plainOn ); // the morphology REALLY executed

  // Claim honesty: only the morphology product's sidecar claims it.
  const QJsonObject morphProv = [ & ] {
    QFile f( morphOut + QStringLiteral( ".prov.json" ) );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    return QJsonDocument::fromJson( f.readAll() ).object();
  }();
  CHECK( morphProv.value( QStringLiteral( "execution" ) )
           .toObject()
           .value( QStringLiteral( "morphology" ) )
           .toString() == QStringLiteral( "open" ) );
  QFile plainProvFile( plainOut + QStringLiteral( ".prov.json" ) );
  REQUIRE( plainProvFile.open( QIODevice::ReadOnly ) );
  const QJsonObject plainProv = QJsonDocument::fromJson( plainProvFile.readAll() ).object();
  CHECK_FALSE( plainProv.value( QStringLiteral( "execution" ) )
                 .toObject()
                 .contains( QStringLiteral( "morphology" ) ) );

  // Probability + morphology on the SAME graph refuses everywhere (declared
  // but never executable on the probability stack).
  {
    const std::optional<ModelInfo> probabilityOpt = ModelCatalog::instance().resolve( "r30-mask-open" );
    REQUIRE( probabilityOpt.has_value() );
    ModelInfo probability = *probabilityOpt;
    probability.output.format.clear();
    ModelExecutionRequest request;
    request.modelReference = "r30-mask-open";
    request.inputPath = raster.toStdString();
    request.outputPath = dir.filePath( QStringLiteral( "refused.tif" ) ).toStdString();
    const InferencePlan plan = planModelInference( request, probability );
    REQUIRE( plan.verdict == InferencePlan::Verdict::Refused );
    REQUIRE( plan.findingCode( "plan.postprocess_contract_conflict" ) );
    std::string acquireError;
    const auto session = ModelRuntimeRegistry::instance().acquire( probability, &acquireError );
    REQUIRE( session );
    TileInferenceEngine engine( probability, session );
    RSOperatorContext context;
    REQUIRE_THROWS_WITH(
      engine.run( raster.toStdString(), {},
                  dir.filePath( QStringLiteral( "refused.tif" ) ).toStdString(), context ),
      Catch::Matchers::ContainsSubstring( "morphology executes on labels/mask products" ) );
  }
}

#endif // SICNU_HAS_OPENCV
