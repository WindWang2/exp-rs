// R28-T01 science anchors — two evidence families for this track:
//
//  A. Training-sample extraction after the two-pass restructure
//     (.planning/r28-t01-science): the kept set, row order, values, group-id
//     alignment, ignore-mode semantics (AnyBand vs AllBands), cap-after-NoData
//     behaviour and cancellation contracts. Expected values come from the
//     fixtures' value functions and independent GDAL reads, never from the
//     extraction module under test.
//  B. rs:change_detection(makeMask) re-publish through RSOperatorRegistry:
//     a second identical run must succeed from the execution cache instead
//     of reopening a magnitude temp that no longer exists (r26
//     PERF_BASELINE appendix; fix lands in rs_change_streaming.cpp).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <ogr_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "rs_training_data_extraction.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/satellite_products.h"

namespace
{

/// Float32 GeoTIFF with GT {0,1,0,H,0,-1} (pixel (r,c) centre → (c+0.5, H-r-0.5)).
void createRaster( const QString &path, int W, int H, int nBands,
                   const std::function<float( int, int, int )> &valueFn,
                   bool setNodata = false, double nodata = -9999.0 )
{
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(),
                                 W, H, nBands, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  std::vector<float> band( static_cast<size_t>( W ) * static_cast<size_t>( H ) );
  for ( int b = 0; b < nBands; ++b )
  {
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        band[static_cast<size_t>( r * W + c )] = valueFn( b, r, c );
    GDALRasterBand *rb = ds->GetRasterBand( b + 1 );
    rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    if ( setNodata )
      rb->SetNoDataValue( nodata );
  }
  double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
}

/// Axis-aligned square in map coordinates (cols [c0,c1), rows [r0,r1)).
QgsGeometry squareGeom( double x0, double y0, double x1, double y1 )
{
  QgsPolygonXY polygon;
  polygon.append( { QgsPointXY( x0, y0 ), QgsPointXY( x1, y0 ),
                    QgsPointXY( x1, y1 ), QgsPointXY( x0, y1 ),
                    QgsPointXY( x0, y0 ) } );
  return QgsGeometry::fromPolygonXY( polygon );
}

RsTrainingGeometry roi( int classId, const QgsGeometry &geom,
                        const QVector<quint64> &pixelIndices = {} )
{
  RsTrainingGeometry tg;
  tg.classId = classId;
  tg.geometry = geom;
  tg.pixelIndices = pixelIndices;
  return tg;
}

/// Deterministic non-trivial band value (independent of the module's reads).
float fixtureValue( int b, int r, int c )
{
  const float x = static_cast<float>( r * 37 + c * 11 + b * 101 );
  return std::fmod( 0.5f * x + 3.0f * std::sin( x * 0.07f ), 950.0f ) + 25.0f;
}

} // namespace

// ===========================================================================
// A1 — ignore-mode semantics through the extraction seam: AnyBand drops a
// pixel when ANY band matches NoData/ignore, AllBands only when EVERY band
// does. The two-pass validity logic must decompose isIgnorePixel exactly.
// ===========================================================================
TEST_CASE( "R28-T01 training extraction: AnyBand vs AllBands ignore semantics",
           "[r28][classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  // Band 1: column 0 is NoData (-9999); band 2: column 1 is NoData.
  // Column 0 pixels: exactly one invalid band → AnyBand drops, AllBands keeps.
  // Pixel (r=0,c=0) vs (r=0,c=1): each has one invalid band, never both.
  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 8, 8, 2,
                []( int b, int r, int c ) {
                  if ( b == 0 && c == 0 )
                    return -9999.0f;
                  if ( b == 1 && c == 1 )
                    return -9999.0f;
                  return fixtureValue( b, r, c );
                },
                true, -9999.0 );

  const QVector<RsTrainingGeometry> geoms = { roi( 1, squareGeom( 0, 8, 8, 0 ) ) };

  RsTrainingDataExtraction::Options anyBand; // default mode = AnyBand
  const RsTrainingDataResult anyRes = RsTrainingDataExtraction::extract(
    raster, { 1, 2 }, geoms, anyBand );
  REQUIRE( anyRes.ok );
  // 64 candidates; columns 0 and 1 are each dropped (one bad band each) → 48.
  REQUIRE( anyRes.X.rows == 48 );
  for ( int s = 0; s < anyRes.X.rows; ++s )
    for ( int b = 0; b < 2; ++b )
      REQUIRE( anyRes.X.at<float>( s, b ) != -9999.0f );

  RsTrainingDataExtraction::Options allBands;
  allBands.ignore.mode = RsPixelIgnoreOptions::Mode::AllBands;
  const RsTrainingDataResult allRes = RsTrainingDataExtraction::extract(
    raster, { 1, 2 }, geoms, allBands );
  REQUIRE( allRes.ok );
  // No pixel has every band invalid → AllBands keeps all 64.
  REQUIRE( allRes.X.rows == 64 );

  // And with every band invalid on column 0 only when both bands are NoData:
  // build a second raster where column 0 is NoData on BOTH bands.
  const QString raster2 = tmp.path() + "/both.tif";
  createRaster( raster2, 8, 8, 2,
                []( int b, int, int c ) {
                  return c == 0 ? -9999.0f : fixtureValue( b, 1, c );
                },
                true, -9999.0 );
  const RsTrainingDataResult allRes2 = RsTrainingDataExtraction::extract(
    raster2, { 1, 2 }, geoms, allBands );
  REQUIRE( allRes2.ok );
  REQUIRE( allRes2.X.rows == 56 ); // column 0 (8 px) has all bands invalid
}

// ===========================================================================
// A2 — value/order oracle for the uncapped path: every X entry must equal an
// independent GDAL read of the fixture, rows ascending in (class, pixel),
// and sampleGroupIds stay aligned with the kept rows.
// ===========================================================================
TEST_CASE( "R28-T01 training extraction: uncapped values, order and group ids",
           "[r28][classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  constexpr int W = 16, H = 16, B = 3;
  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, W, H, B,
                []( int b, int r, int c ) { return fixtureValue( b, r, c ); } );

  // Class 1: square(0,16,8,8) → rows 0..7 × cols 0..7 (64 px, group 0).
  // Class 5: square(4,12,12,0) → x∈[4,12), y∈[0,12) → rows 4..15 × cols 4..11
  // (96 px, group 1). Overlap rows 4..7 × cols 4..7 (16 px) → last wins →
  // class 5. Kept: 48 + 96 = 144.
  const QVector<RsTrainingGeometry> geoms = {
    roi( 1, squareGeom( 0, 16, 8, 8 ) ),
    roi( 5, squareGeom( 4, 12, 12, 0 ) ),
  };
  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1, 2, 3 }, geoms );
  REQUIRE( res.ok );
  REQUIRE( res.X.rows == 144 );
  REQUIRE( res.X.cols == B );
  REQUIRE( res.classCounts.value( 1 ) == 48 );
  REQUIRE( res.classCounts.value( 5 ) == 96 );
  REQUIRE( static_cast<int>( res.sampleGroupIds.size() ) == res.X.rows );

  // Independent reference read of the full bands (not via the module).
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( raster.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds != nullptr );
  std::vector<float> ref( static_cast<size_t>( W ) * H * B );
  for ( int b = 0; b < B; ++b )
    REQUIRE( ds->GetRasterBand( b + 1 )->RasterIO(
                GF_Read, 0, 0, W, H,
                ref.data() + static_cast<size_t>( b ) * W * H,
                W, H, GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );

  int s = 0;
  // Class 1 rows: rows 0..7 × cols 0..7 minus the overlap (rows 4..7 ×
  // cols 4..7, stolen by class 5) in ascending pixel order; class 5 rows:
  // rows 4..15 × cols 4..11 after them.
  for ( int rr = 0; rr < 8; ++rr )
    for ( int cc = 0; cc < 8; ++cc )
    {
      if ( rr >= 4 && cc >= 4 )
        continue; // overlap stole this pixel for class 5
      REQUIRE( res.y.at<int>( s, 0 ) == 1 );
      REQUIRE( res.sampleGroupIds[static_cast<size_t>( s )] == 0 );
      for ( int b = 0; b < B; ++b )
        REQUIRE( res.X.at<float>( s, b )
                 == ref[static_cast<size_t>( ( b * H + rr ) * W + cc )] );
      ++s;
    }
  for ( int rr = 4; rr < 16; ++rr )
    for ( int cc = 4; cc < 12; ++cc )
    {
      REQUIRE( res.y.at<int>( s, 0 ) == 5 );
      REQUIRE( res.sampleGroupIds[static_cast<size_t>( s )] == 1 );
      for ( int b = 0; b < B; ++b )
        REQUIRE( res.X.at<float>( s, b )
                 == ref[static_cast<size_t>( ( b * H + rr ) * W + cc )] );
      ++s;
    }
  REQUIRE( s == res.X.rows );
}

// ===========================================================================
// A3 — NoData-heavy cap: with most candidates invalid and a tiny per-class
// cap, the cap must still fill with VALID samples (R26-S2 contract preserved
// by the restructure), selection is deterministic across runs.
// ===========================================================================
TEST_CASE( "R28-T01 training extraction: NoData-heavy cap fills with valid samples",
           "[r28][classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  // 75% NoData: pixel valid iff (r + c) % 4 == 0.
  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 32, 32, 1,
                []( int, int r, int c ) {
                  return ( ( r + c ) % 4 == 0 ) ? fixtureValue( 0, r, c ) : -9999.0f;
                },
                true, -9999.0 );

  const QVector<RsTrainingGeometry> geoms = {
    roi( 3, squareGeom( 0, 32, 32, 0 ) ), // full frame, class 3
  };
  RsTrainingDataExtraction::Options options;
  options.maxSamplesPerClass = 7;

  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms, options );
  REQUIRE( res.ok );
  REQUIRE( res.X.rows == 7 );
  REQUIRE( res.classCounts.value( 3 ) == 7 );
  for ( int s = 0; s < res.X.rows; ++s )
    REQUIRE( res.X.at<float>( s, 0 ) != -9999.0f );

  const RsTrainingDataResult res2 = RsTrainingDataExtraction::extract(
    raster, { 1 }, geoms, options );
  REQUIRE( res2.ok );
  REQUIRE( res2.X.rows == res.X.rows );
  for ( int s = 0; s < res.X.rows; ++s )
    REQUIRE( res2.X.at<float>( s, 0 ) == res.X.at<float>( s, 0 ) );
}

// ===========================================================================
// A4 — cancellation inside the band-read phases (fractions past geometry
// collection) must fail with Error::Cancelled and empty matrices.
// ===========================================================================
TEST_CASE( "R28-T01 training extraction: cancel during band reads",
           "[r28][classification][training-data]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  const QString raster = tmp.path() + "/src.tif";
  createRaster( raster, 32, 32, 3,
                []( int b, int r, int c ) { return fixtureValue( b, r, c ); } );

  const QVector<RsTrainingGeometry> geoms = { roi( 1, squareGeom( 0, 32, 32, 0 ) ) };

  // Cancel as soon as the read phase reports (fraction ≥ 0.4).
  const RsTrainingDataResult res = RsTrainingDataExtraction::extract(
    raster, { 1, 2, 3 }, geoms, RsTrainingDataExtraction::Options(),
    []( double fraction, const QString & ) { return fraction < 0.4; } );
  REQUIRE( !res.ok );
  REQUIRE( res.error == RsTrainingDataResult::Error::Cancelled );
  REQUIRE( res.X.empty() );
  REQUIRE( res.y.empty() );

  // Same contract when cancelling later — 0.8 sits inside pass 2's
  // per-band report window (0.65 + 0.35·(b+1)/B), exercising the cancel
  // path where out.X.release() is load-bearing.
  const RsTrainingDataResult resLate = RsTrainingDataExtraction::extract(
    raster, { 1, 2, 3 }, geoms, RsTrainingDataExtraction::Options(),
    []( double fraction, const QString & ) { return fraction < 0.8; } );
  REQUIRE( !resLate.ok );
  REQUIRE( resLate.error == RsTrainingDataResult::Error::Cancelled );
  REQUIRE( resLate.X.empty() );
}

// ===========================================================================
// B1 — registry re-publish (RED on unfixed master): a second identical
// rs:change_detection(makeMask) run through RSOperatorRegistry must succeed
// from the execution cache. The magnitude temp never exists in run 2; the
// published mask must be the record read back (r26 PERF_BASELINE appendix).
// ===========================================================================
TEST_CASE( "R28-T01 registry change detection: identical re-run republishes from cache",
           "[r28][change][registry][repub]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( registry.create( "rs:change_detection" ) != nullptr );

  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 48, H = 48;
  const std::array<double, 6> gt = { 500000.0, 30.0, 0.0, 4400000.0, 0.0, -30.0 };

  const auto makeRaster = [&]( const QString &path, float base ) {
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    REQUIRE( GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) ) == CE_None );
    std::vector<float> data( W * H );
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        data[static_cast<size_t>( r * W + c )] = base + 5.0f * std::sin( 0.3f * r ) + static_cast<float>( c % 7 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, H,
                           data.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
  };
  const QString before = tmp.path() + "/before.tif";
  const QString after = tmp.path() + "/after.tif";
  const QString output = tmp.path() + "/mask.tif";
  makeRaster( before, 100.0f );
  makeRaster( after, 100.0f );
  {
    GDALDatasetH ds = GDALOpen( after.toUtf8().constData(), GA_Update );
    REQUIRE( ds != nullptr );
    std::vector<float> row( W, 500.0f );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, 1,
                           row.data(), W, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
  }

  const auto runOnce = [&]( double threshold ) {
    auto op = registry.create( "rs:change_detection" );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["before"] = before.toStdString();
    params["after"] = after.toStdString();
    params["output"] = output.toStdString();
    params["method"] = "difference";
    params["makeMask"] = true;
    params["thresholdMethod"] = "manual";
    params["threshold"] = threshold;
    return op->run( params, ctx );
  };

  const Json::Value run1 = runOnce( 50.0 );
  REQUIRE( run1.isMember( "changedPixels" ) );
  REQUIRE( run1["changedPixels"].asUInt64() == static_cast<Json::UInt64>( W ) );

  // Second identical run: execution-cache hit must republish the recorded
  // mask instead of failing to reopen a magnitude temp that never existed.
  const Json::Value run2 = runOnce( 50.0 );
  REQUIRE( run2.isMember( "changedPixels" ) );
  REQUIRE( run2["changedPixels"].asUInt64() == run1["changedPixels"].asUInt64() );
  REQUIRE( run2.isMember( "thresholdUsed" ) );
  REQUIRE( run2["thresholdUsed"].asDouble() == Catch::Approx( run1["thresholdUsed"].asDouble() ).epsilon( 1e-12 ) );
  REQUIRE( run2.isMember( "already_published" ) );
  // The hit must actually be the cache path (not an honest recompute that
  // would also satisfy the checks above).
  REQUIRE( run2["already_published"].asBool() );

  // Different threshold, same paths: the derivation parameters are part of
  // the run identity (R1 P1-2) — the run must recompute honestly instead of
  // serving the cached mask. The changed row maxes out near +400, so a 500
  // threshold flags nothing and rewrites the published mask accordingly.
  const Json::Value run3 = runOnce( 500.0 );
  REQUIRE( run3.isMember( "changedPixels" ) );
  REQUIRE( run3["changedPixels"].asUInt64() == 0 );
  REQUIRE( run3["thresholdUsed"].asDouble() == Catch::Approx( 500.0 ).epsilon( 1e-12 ) );

  // Returning to the ORIGINAL threshold hits run1's journal again, but the
  // output on disk now carries run3's parameterization — serving run1's
  // cached counts for it would be a silent wrong success. The identity
  // digest (SICNU_CHANGE_IDENTITY) must turn that into a typed error.
  bool identityMismatchRefused = false;
  try
  {
    runOnce( 50.0 );
    FAIL( "stale-artifact cache hit was not refused" );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    const std::string msg = e.what();
    CHECK( msg.find( "DIFFERENT parameterization" ) != std::string::npos );
    identityMismatchRefused = msg.find( "DIFFERENT parameterization" ) != std::string::npos;
  }
  REQUIRE( identityMismatchRefused );

  // The published mask reflects the LAST writer (run3, threshold 500 — the
  // changed row maxes near +400, so nothing survives): the honest on-disk
  // state after the sequence, not run1's mask.
  GDALDatasetH out = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( out != nullptr );
  const char *finalThreshold = GDALGetMetadataItem( out, "SICNU_CHANGE_THRESHOLD", nullptr );
  REQUIRE( finalThreshold != nullptr );
  CHECK( std::strtod( finalThreshold, nullptr ) == Catch::Approx( 500.0 ).epsilon( 1e-12 ) );
  std::vector<float> row0( W ), row1( W );
  REQUIRE( GDALRasterIO( GDALGetRasterBand( out, 1 ), GF_Read, 0, 0, W, 1,
                         row0.data(), W, 1, GDT_Float32, 0, 0 ) == CE_None );
  REQUIRE( GDALRasterIO( GDALGetRasterBand( out, 1 ), GF_Read, 0, 1, W, 1,
                         row1.data(), W, 1, GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( out );
  for ( int c = 0; c < W; ++c )
  {
    CHECK( row0[static_cast<size_t>( c )] == 0.0f );
    CHECK( row1[static_cast<size_t>( c )] == 0.0f );
  }
}

// ===========================================================================
// B2 — the published mask carries the full stat record (threshold, counts,
// magnitude stats) so cache-hit readback is faithful; keys match run1 JSON.
// ===========================================================================
TEST_CASE( "R28-T01 change mask persists the stat record for cache readback",
           "[r28][change][registry][repub]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 32, H = 32;
  const std::array<double, 6> gt = { 0.0, 10.0, 0.0, static_cast<double>( H ) * 10.0, 0.0, -10.0 };

  const auto makeRaster = [&]( const QString &path, float base ) {
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    REQUIRE( GDALSetGeoTransform( ds, const_cast<double *>( gt.data() ) ) == CE_None );
    std::vector<float> data( W * H );
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
        data[static_cast<size_t>( r * W + c )] = base + static_cast<float>( ( r * W + c ) % 13 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, W, H,
                           data.data(), W, H, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
  };
  const QString before = tmp.path() + "/b.tif";
  const QString after = tmp.path() + "/a.tif";
  const QString output = tmp.path() + "/m.tif";
  makeRaster( before, 100.0f );
  makeRaster( after, 120.0f ); // constant +20 difference everywhere

  auto op = registry.create( "rs:change_detection" );
  REQUIRE( op != nullptr );
  sicnu::operators::RSOperatorContext ctx;
  Json::Value params( Json::objectValue );
  params["before"] = before.toStdString();
  params["after"] = after.toStdString();
  params["output"] = output.toStdString();
  params["method"] = "difference";
  params["makeMask"] = true;
  params["thresholdMethod"] = "manual";
  params["threshold"] = 10.0;
  const Json::Value res = op->run( params, ctx );

  // Independent oracle: |after - before| = 20 > threshold 10 everywhere.
  REQUIRE( res.isMember( "changedPixels" ) );
  CHECK( res["changedPixels"].asUInt64() == static_cast<Json::UInt64>( W ) * H );
  CHECK( res["mean"].asDouble() == Catch::Approx( 20.0 ).epsilon( 1e-9 ) );
  CHECK( res["stddev"].asDouble() == Catch::Approx( 0.0 ).margin( 1e-9 ) );

  GDALDatasetH mask = GDALOpen( output.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( mask != nullptr );
  const char *method = GDALGetMetadataItem( mask, "SICNU_CHANGE_METHOD", nullptr );
  REQUIRE( method != nullptr );
  CHECK( std::string( method ) == "difference" );
  const char *changed = GDALGetMetadataItem( mask, "SICNU_CHANGE_CHANGED", nullptr );
  const char *evaluated = GDALGetMetadataItem( mask, "SICNU_CHANGE_EVALUATED", nullptr );
  const char *mean = GDALGetMetadataItem( mask, "SICNU_CHANGE_MEAN", nullptr );
  const char *stddev = GDALGetMetadataItem( mask, "SICNU_CHANGE_STDDEV", nullptr );
  REQUIRE( changed != nullptr );
  REQUIRE( evaluated != nullptr );
  REQUIRE( mean != nullptr );
  REQUIRE( stddev != nullptr );
  CHECK( std::strtoull( changed, nullptr, 10 ) == res["changedPixels"].asUInt64() );
  CHECK( std::strtoull( evaluated, nullptr, 10 ) == res["totalPixels"].asUInt64() );
  CHECK( std::strtod( mean, nullptr ) == Catch::Approx( res["mean"].asDouble() ).epsilon( 1e-12 ) );
  CHECK( std::strtod( stddev, nullptr ) == Catch::Approx( res["stddev"].asDouble() ).epsilon( 1e-12 ) );
  GDALClose( mask );
}

// ===========================================================================
// C1 — rs:apply_mask must carry the calibration contract through (WP04):
// a PB04-offset-stamped raster masked via rs:apply_mask keeps
// SICNU_NUMERIC_SCALE/OFFSET, and NDVI on the masked output equals NDVI on
// the original (physical = (stored − offset)/scale). On unfixed master the
// stamps are dropped and the ratio index drifts back to the #1755 bias
// (0.5 instead of 1/3).
// ===========================================================================
TEST_CASE( "R28-T01 apply_mask keeps the numeric calibration contract",
           "[r28][calibration][apply_mask]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();

  // Two-band fixture: red = 1000, nir = 3000 everywhere, with the affine
  // stamps stored = physical × 10000 + (−1000), i.e. red_phys = 0.2,
  // nir_phys = 0.4 → NDVI = 1/3 exactly.
  const QString raster = tmp.path() + "/stamped.tif";
  {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( raster.toUtf8().constData(), 16, 16, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> red( 256, 1000.0f ), nir( 256, 3000.0f );
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 16, 16, red.data(), 16, 16, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, 16, 16, nir.data(), 16, 16, GDT_Float32, 0, 0 ) == CE_None );
    double gt[6] = { 0, 10, 0, 160, 0, -10 };
    ds->SetGeoTransform( gt );
    for ( int b = 1; b <= 2; ++b )
    {
      GDALRasterBand *band = ds->GetRasterBand( b );
      // apply_mask needs a declared output-side NoData (or an explicit
      // no_data param); no pixel actually carries it.
      band->SetNoDataValue( -9999.0 );
      GDALSetMetadataItem( band, SatelliteProducts::kNumericScaleKey, "10000", nullptr );
      GDALSetMetadataItem( band, SatelliteProducts::kNumericOffsetKey, "-1000", nullptr );
    }
    GDALSetMetadataItem( ds, SatelliteProducts::kNumericScaleKey, "10000", nullptr );
    GDALSetMetadataItem( ds, SatelliteProducts::kNumericOffsetKey, "-1000", nullptr );
    GDALClose( ds );
  }

  // Small mask: > 0 only in the top-left 4x4 block.
  const QString maskPath = tmp.path() + "/mask.tif";
  {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( maskPath.toUtf8().constData(), 16, 16, 1, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0, 10, 0, 160, 0, -10 };
    ds->SetGeoTransform( gt );
    std::vector<unsigned char> m( 256, 0 );
    for ( int r = 0; r < 4; ++r )
      for ( int c = 0; c < 4; ++c )
        m[static_cast<size_t>( r * 16 + c )] = 1;
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 16, 16, m.data(), 16, 16, GDT_Byte, 0, 0 ) == CE_None );
    GDALClose( ds );
  }

  const auto runIndex = [&]( const QString &input, const QString &output ) {
    auto op = registry.create( "rs:spectral_index" );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 2;
    params["red"] = 1;
    return op->run( params, ctx );
  };

  const QString ndviOriginal = tmp.path() + "/ndvi_orig.tif";
  const Json::Value ref = runIndex( raster, ndviOriginal );
  REQUIRE( ref.isMember( "output" ) );

  // Mask the product, then verify the contract survived.
  const QString masked = tmp.path() + "/masked.tif";
  {
    auto op = registry.create( "rs:apply_mask" );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = raster.toStdString();
    params["mask"] = maskPath.toStdString();
    params["output"] = masked.toStdString();
    op->run( params, ctx );

    GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( masked.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds != nullptr );
    const char *bandOffset = GDALGetMetadataItem( ds->GetRasterBand( 2 ), SatelliteProducts::kNumericOffsetKey, nullptr );
    REQUIRE( bandOffset != nullptr );
    CHECK( std::string( bandOffset ) == "-1000" );
    const char *dsScale = GDALGetMetadataItem( ds, SatelliteProducts::kNumericScaleKey, nullptr );
    REQUIRE( dsScale != nullptr );
    CHECK( std::string( dsScale ) == "10000" );
    GDALClose( ds );
  }

  // NDVI on the masked output must equal NDVI on the original at an
  // unmasked pixel (row 8): 1/3, not the offset-blind 0.5.
  const QString ndviMasked = tmp.path() + "/ndvi_masked.tif";
  REQUIRE( runIndex( masked, ndviMasked ).isMember( "output" ) );
  const auto readPixel = []( const QString &path, int r, int c ) {
    GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds != nullptr );
    float v = 0.f;
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Read, c, r, 1, 1, &v, 1, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
    return v;
  };
  CHECK( readPixel( ndviOriginal, 8, 8 ) == Catch::Approx( 1.0 / 3.0 ).epsilon( 1e-6 ) );
  CHECK( readPixel( ndviMasked, 8, 8 ) == Catch::Approx( 1.0 / 3.0 ).epsilon( 1e-6 ) );
}

// ===========================================================================
// C2 — rs:radiometric_calibration refuses double calibration (WP04): an
// input stamped with a physical radiometric state is rejected up front;
// a digital_number-stamped raster passes the guard (fails later on missing
// metadata, not on the state check).
// ===========================================================================
TEST_CASE( "R28-T01 radiometric calibration refuses double calibration",
           "[r28][calibration][fail-closed]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( registry.create( "rs:radiometric_calibration" ) != nullptr );

  const QString raster = tmp.path() + "/phys.tif";
  {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( raster.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> v( 64, 0.25f );
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 8, 8, v.data(), 8, 8, GDT_Float32, 0, 0 );
    GDALClose( ds );
  }
  QString stateErr;
  REQUIRE( SatelliteProducts::setRadiometricState( raster, SatelliteProducts::kRadiometricStateToaReflectance, &stateErr ) );

  const auto runCalibration = [&]( const std::string &input ) {
    auto op = registry.create( "rs:radiometric_calibration" );
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = input;
    params["output"] = ( tmp.path() + "/out.tif" ).toStdString();
    params["unit"] = "toa_reflectance";
    return op->run( params, ctx );
  };

  bool refusedByStateGuard = false;
  try
  {
    runCalibration( raster.toStdString() );
    FAIL( "double calibration was not refused" );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    const std::string msg = e.what();
    CHECK( msg.find( "radiometric state" ) != std::string::npos );
    refusedByStateGuard = msg.find( "radiometric state" ) != std::string::npos;
  }
  REQUIRE( refusedByStateGuard );

  // The legitimate starting point (DN stamp) passes the guard: the failure
  // below is the missing-metadata path, not the state check.
  const QString dnRaster = tmp.path() + "/dn.tif";
  {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( dnRaster.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> v( 64, 7500.0f );
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 8, 8, v.data(), 8, 8, GDT_Float32, 0, 0 );
    GDALClose( ds );
  }
  REQUIRE( SatelliteProducts::setRadiometricState( dnRaster, SatelliteProducts::kRadiometricStateDigitalNumber, &stateErr ) );
  try
  {
    runCalibration( dnRaster.toStdString() );
    FAIL( "calibration without metadata should fail" );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    CHECK( std::string( e.what() ).find( "radiometric state" ) == std::string::npos );
  }
}

// ===========================================================================
// C4 — rs:band_math reports per-pixel output quality from the actual
// evaluation (WP10): divide-by-zero NaN pixels are counted and the share is
// verifiable against an independent read of the output raster.
// ===========================================================================
TEST_CASE( "R28-T01 band_math reports valid/nonfinite output counts",
           "[r28][band_math][quality]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( registry.create( "rs:band_math" ) != nullptr );

  // 8x8; b2 is zero on the first 16 pixels → b1/b2 NaN exactly there.
  const QString raster = tmp.path() + "/in.tif";
  {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( raster.toUtf8().constData(), 8, 8, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> b1( 64 ), b2( 64 );
    for ( int i = 0; i < 64; ++i )
    {
      b1[static_cast<size_t>( i )] = 6.0f;
      b2[static_cast<size_t>( i )] = ( i < 16 ) ? 0.0f : 3.0f;
    }
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 8, 8, b1.data(), 8, 8, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, 8, 8, b2.data(), 8, 8, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
  }

  const QString output = tmp.path() + "/ratio.tif";
  auto op = registry.create( "rs:band_math" );
  REQUIRE( op != nullptr );
  sicnu::operators::RSOperatorContext ctx;
  Json::Value params( Json::objectValue );
  params["input"] = raster.toStdString();
  params["output"] = output.toStdString();
  params["expression"] = "b1 / b2";
  const Json::Value res = op->run( params, ctx );

  REQUIRE( res.isMember( "valid_pixels" ) );
  CHECK( res["valid_pixels"].asUInt64() == 48 );
  REQUIRE( res.isMember( "nonfinite_output_pixels" ) );
  CHECK( res["nonfinite_output_pixels"].asUInt64() == 16 );
  CHECK( res["valid_pixel_ratio"].asDouble() == Catch::Approx( 0.75 ).epsilon( 1e-12 ) );

  // Independent verification against the written raster.
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( output.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds != nullptr );
  std::vector<float> out( 64 );
  REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, 8, 8, out.data(), 8, 8, GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  size_t finiteCount = 0;
  for ( float v : out )
    if ( std::isfinite( v ) )
      ++finiteCount;
  CHECK( finiteCount == 48 );
  CHECK( out[0] != out[0] ); // first pixel is the NaN of 6/0
  CHECK( out[63] == Catch::Approx( 2.0f ).epsilon( 1e-6 ) );
}

// ===========================================================================
// D1 — train → (re-run over existing output) → model reload → predict-only
// consistency through the rs:supervised_classification operator entry with
// NON-CONTIGUOUS class ids {2, 7} (WP03): the re-run over an existing
// output, the same-process repeat and the modelIn predict-only path must
// all publish the identical class map.
// ===========================================================================
TEST_CASE( "R28-T01 supervised classification: rerun, existing output and reload agree",
           "[r28][classification][pipeline][reload]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();
  OGRRegisterAll();

  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  REQUIRE( registry.create( "rs:supervised_classification" ) != nullptr );

  // 32x32, 2 bands; left half class 2 (band1 low), right half class 7
  // (band1 high) — well separated, non-contiguous ids, no class 1.
  const QString raster = tmp.path() + "/src.tif";
  {
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    GDALDataset *ds = drv->Create( raster.toUtf8().constData(), 32, 32, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> b1( 32 * 32 ), b2( 32 * 32 );
    for ( int r = 0; r < 32; ++r )
      for ( int c = 0; c < 32; ++c )
      {
        const size_t i = static_cast<size_t>( r ) * 32 + c;
        b1[i] = ( c < 16 ) ? 10.0f : 200.0f;
        b2[i] = static_cast<float>( ( r * 7 + c ) % 23 );
      }
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 32, 32, b1.data(), 32, 32, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, 32, 32, b2.data(), 32, 32, GDT_Float32, 0, 0 ) == CE_None );
    double gt[6] = { 0, 1, 0, 32, 0, -1 };
    ds->SetGeoTransform( gt );
    GDALClose( ds );
  }

  // Training vector: two square ROIs with class ids 2 and 7.
  const QString training = tmp.path() + "/train.gpkg";
  {
    GDALDriverH drv = GDALGetDriverByName( "GPKG" );
    REQUIRE( drv != nullptr );
    GDALDatasetH ds = GDALCreate( drv, training.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
    REQUIRE( ds != nullptr );
    OGRLayerH lyr = GDALDatasetCreateLayer( ds, "training", nullptr, wkbPolygon, nullptr );
    REQUIRE( lyr != nullptr );
    OGRFieldDefnH fld = OGR_Fld_Create( "class_id", OFTInteger );
    REQUIRE( OGR_L_CreateField( lyr, fld, true ) == OGRERR_NONE );
    OGR_Fld_Destroy( fld );
    const double squares[2][4] = {
      { 0, 8, 6, 2 },    // class 2: x 0-5, y 2-7 → rows 24-29, cols 0-5
      { 26, 30, 32, 24 } // class 7: x 26-29, y 24-31 → rows 0-7, cols 26-29
    };
    const int ids[2] = { 2, 7 };
    for ( int s = 0; s < 2; ++s )
    {
      OGRGeometryH ring = OGR_G_CreateGeometry( wkbLinearRing );
      OGR_G_AddPoint_2D( ring, squares[s][0], squares[s][1] );
      OGR_G_AddPoint_2D( ring, squares[s][2], squares[s][1] );
      OGR_G_AddPoint_2D( ring, squares[s][2], squares[s][3] );
      OGR_G_AddPoint_2D( ring, squares[s][0], squares[s][3] );
      OGR_G_AddPoint_2D( ring, squares[s][0], squares[s][1] );
      OGRGeometryH poly = OGR_G_CreateGeometry( wkbPolygon );
      REQUIRE( OGR_G_AddGeometryDirectly( poly, ring ) == OGRERR_NONE );
      OGRFeatureH feat = OGR_F_Create( OGR_L_GetLayerDefn( lyr ) );
      OGR_F_SetFieldInteger( feat, 0, ids[s] );
      REQUIRE( OGR_F_SetGeometryDirectly( feat, poly ) == OGRERR_NONE );
      REQUIRE( OGR_L_CreateFeature( lyr, feat ) == OGRERR_NONE );
      OGR_F_Destroy( feat );
    }
    GDALClose( ds );
  }

  const QString modelPath = tmp.path() + "/model.yml";
  const QString out1 = tmp.path() + "/out1.tif";

  const auto runOp = [&]( const QString &output, const std::string &modelIn ) {
    auto op = registry.create( "rs:supervised_classification" );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params( Json::objectValue );
    params["input"] = raster.toStdString();
    if ( modelIn.empty() )
      params["training"] = training.toStdString();
    params["output"] = output.toStdString();
    params["method"] = "normal_bayes";
    params["modelOut"] = modelPath.toStdString();
    if ( !modelIn.empty() )
      params["modelIn"] = modelIn;
    return op->run( params, ctx );
  };

  const auto readMap = [&]( const QString &path ) {
    GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( ds != nullptr );
    std::vector<uint8_t> m( 32 * 32 );
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, 32, 32, m.data(), 32, 32, GDT_Byte, 0, 0 ) == CE_None );
    GDALClose( ds );
    return m;
  };

  // Train + predict.
  const Json::Value r1 = runOp( out1, "" );
  REQUIRE( r1.isMember( "output" ) );
  const std::vector<uint8_t> map1 = readMap( out1 );
  REQUIRE( QFile::exists( modelPath ) );
  // Non-contiguous ids both present; left half class 2, right half class 7.
  bool saw2 = false, saw7 = false;
  for ( int r = 0; r < 32; ++r )
    for ( int c = 0; c < 32; ++c )
    {
      const uint8_t v = map1[static_cast<size_t>( r ) * 32 + c];
      saw2 = saw2 || v == 2;
      saw7 = saw7 || v == 7;
      if ( v != 0 )
        CHECK( v == ( c < 16 ? 2 : 7 ) );
    }
  REQUIRE( saw2 );
  REQUIRE( saw7 );

  // Re-run with the SAME output path (output exists) → identical map.
  const Json::Value r2 = runOp( out1, "" );
  REQUIRE( r2.isMember( "output" ) );
  CHECK( readMap( out1 ) == map1 );

  // Predict-only from the saved model (no training vector): same map again.
  const QString out3 = tmp.path() + "/out3.tif";
  const Json::Value r3 = runOp( out3, modelPath.toStdString() );
  REQUIRE( r3.isMember( "output" ) );
  CHECK( readMap( out3 ) == map1 );
}
