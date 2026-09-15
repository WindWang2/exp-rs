/***************************************************************************
  tests/test_io_operators.cpp — io:* operator family suite (Phase 11).
  Registry/schema/execution through RSOperatorContext on synthetic datasets.
  Links Catch2 + Qt6::Core + sicnu_operators + jsoncpp (test_rs_operator
  pattern); requires the full operator chain build (integration milestone).
 ***************************************************************************/

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/io/finalize_manifest.h"
#include "geospatial/io/stage_ledger.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_operators" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}

/// Minimal 8x8 georeferenced Byte raster with nodata.
std::string makeTinyRaster( const std::string &dir, const std::string &name )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / name ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 8, 8, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  double gt[6] = { 116.0, 0.01, 0.0, 31.0, 0.0, -0.01 };
  REQUIRE( GDALSetGeoTransform( dataset, gt ) == CE_None );
  OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
  REQUIRE( OSRImportFromEPSG( srs, 4326 ) == OGRERR_NONE );
  char *wkt = nullptr;
  REQUIRE( OSRExportToWkt( srs, &wkt ) == OGRERR_NONE );
  GDALSetProjection( dataset, wkt );
  CPLFree( wkt );
  OSRDestroySpatialReference( srs );
  GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
  GDALSetRasterNoDataValue( band, 255 );
  unsigned char pixels[64];
  for ( int i = 0; i < 64; ++i )
    pixels[i] = static_cast<unsigned char>( i % 200 );
  REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 8, 8, pixels, 8, 8, GDT_Byte, 0, 0 ) == CE_None );
  GDALClose( dataset );
  return path;
}

std::string makeTinyGeoJson( const std::string &dir, const std::string &name )
{
  const std::string path = ( fs::path( dir ) / name ).string();
  std::ofstream out( path );
  out << R"json({
    "type": "FeatureCollection", "name": "pts",
    "crs": { "type": "name", "properties": { "name": "urn:ogc:def:crs:OGC:1.3:CRS84" } },
    "features": [
      { "type": "Feature", "properties": { "id": 1 }, "geometry": { "type": "Point", "coordinates": [116.5, 30.5] } },
      { "type": "Feature", "properties": { "id": 2 }, "geometry": { "type": "Point", "coordinates": [116.6, 30.6] } }
    ]
  })json";
  return path;
}

} // namespace

TEST_CASE( "io: family registers all thirteen authoritative operators", "[io][operators][registry]" )
{
  const sicnu::operators::RSOperatorRegistry &registry = sicnu::operators::RSOperatorRegistry::instance();
  for ( const char *id : { "io:translate", "io:warp", "io:reproject", "io:clip", "io:convert_format",
                           "io:build_overviews", "io:make_cog", "io:vector_convert", "io:inspect", "io:doctor",
                           "io:subdatasets", "io:metadata_patch", "io:verify_dataset" } )
  {
    INFO( "operator: " << id );
    CHECK( registry.hasOperator( std::string( id ) ) );
  }
}

TEST_CASE( "io:inspect returns canonical metadata through the operator seam", "[io][operators]" )
{
  const std::string raster = makeTinyRaster( scratch( "inspect" ), "tiny.tif" );
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:inspect" );
  REQUIRE( op );
  REQUIRE( op->name() == "io:inspect" );

  sicnu::operators::RSOperatorContext context;
  const Json::Value result = op->execute( [ & ] {
    Json::Value params;
    params["input"] = raster;
    return params;
  }(), context );

  CHECK( result["kind"].asString() == "raster" );
  CHECK( result["driver"].asString() == "GTiff" );
  CHECK( result["width"].asInt() == 8 );
  CHECK( result["crs"]["authid"].asString() == "EPSG:4326" );
}

TEST_CASE( "io:translate converts raster; io:warp refuses missing targetCrs", "[io][operators]" )
{
  const std::string dir = scratch( "translate" );
  const std::string raster = makeTinyRaster( dir, "src.tif" );
  const std::string target = ( fs::path( dir ) / "out.vrt" ).string();

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:translate" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = target;
    params["driver"] = "VRT";
    const Json::Value result = op->execute( params, context );
    CHECK( result["output"].asString() == target );
    CHECK( sicnu::geo::atomic_fs::fileExists( target ) );
  }

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:warp" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = ( fs::path( dir ) / "warped.tif" ).string();
    // targetCrs missing → CRS policy refusal, not a silent guess.
    CHECK_THROWS_AS( op->execute( params, context ), sicnu::operators::RSOperatorError );
  }

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:warp" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = ( fs::path( dir ) / "warped.tif" ).string();
    params["targetCrs"] = "EPSG:3857";
    params["resampling"] = "bilinear";
    const Json::Value result = op->execute( params, context );
    CHECK( result["width"].asInt() > 0 );
  }
}

TEST_CASE( "io:make_cog validates and io:doctor reports findings", "[io][operators]" )
{
  const std::string dir = scratch( "cog_doctor" );
  const std::string raster = makeTinyRaster( dir, "src.tif" );

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:make_cog" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = ( fs::path( dir ) / "cog.tif" ).string();
    const Json::Value result = op->execute( params, context );
    CHECK( result["output"].asString().find( "cog.tif" ) != std::string::npos );
  }

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:doctor" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    const Json::Value report = op->execute( params, context );
    CHECK( report["kind"].asString() == "doctor_report" );
    CHECK( report["readable"].asBool() );
    CHECK( report["findings"].isArray() );
  }
}

TEST_CASE( "io:vector_convert converts GeoJSON to GeoPackage", "[io][operators][vector]" )
{
  const std::string dir = scratch( "vector" );
  const std::string geojson = makeTinyGeoJson( dir, "pts.geojson" );
  const std::string target = ( fs::path( dir ) / "pts.gpkg" ).string();

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:vector_convert" );
  REQUIRE( op );
  sicnu::operators::RSOperatorContext context;
  Json::Value params;
  params["input"] = geojson;
  params["output"] = target;
  params["driver"] = "GPKG";
  const Json::Value result = op->execute( params, context );
  CHECK( result["feature_count"].asInt64() == 2 );
  CHECK( sicnu::geo::atomic_fs::fileExists( target ) );
}

TEST_CASE( "io: operators declare memory policy and determinism grades", "[io][operators][contract]" )
{
  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  for ( const char *id : { "io:translate", "io:warp", "io:reproject", "io:clip", "io:convert_format",
                           "io:build_overviews", "io:make_cog", "io:vector_convert", "io:inspect", "io:doctor" } )
  {
    auto op = registry.create( std::string( id ) );
    REQUIRE( op );
    CHECK_FALSE( op->schema().isNull() );
    CHECK( ( op->determinismGrade() == "bit-exact" || op->determinismGrade() == "tolerance" ) );
  }
}

// --- F-OPS-4: srcCrsOverride must actually reach the warp -------------------

namespace
{

/// 8x8 Byte raster on a plain pixel grid with NO CRS at all.
std::string makeCrsLessRaster( const std::string &dir, const std::string &name )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / name ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 8, 8, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  double gt[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 }; // pixel grid, no SRS
  REQUIRE( GDALSetGeoTransform( dataset, gt ) == CE_None );
  unsigned char pixels[64];
  for ( int i = 0; i < 64; ++i )
    pixels[i] = static_cast<unsigned char>( i % 200 );
  REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Write, 0, 0, 8, 8,
                         pixels, 8, 8, GDT_Byte, 0, 0 ) == CE_None );
  GDALClose( dataset );
  return path;
}

} // namespace

TEST_CASE( "io:reproject honours srcCrsOverride for CRS-less input (F-OPS-4)",
           "[io][operators][f-ops-4]" )
{
  const std::string dir = scratch( "reproject_crsless" );
  const std::string raster = makeCrsLessRaster( dir, "crsless.tif" );
  const std::string output = ( fs::path( dir ) / "reproj.tif" ).string();

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:reproject" );
  REQUIRE( op );

  sicnu::operators::RSOperatorContext context;
  const Json::Value result = op->execute( [ & ] {
    Json::Value params;
    params["input"] = raster;
    params["output"] = output;
    // Source pixels live on grid (0,0)..(8,-8) declared as EPSG:4326; the
    // warp must transform them into UTM 33N, not pass them through.
    params["srcCrsOverride"] = "EPSG:4326";
    params["targetCrs"] = "EPSG:32633";
    return params;
  }(), context );

  CHECK( result["output"].asString() == output );

  // Output must carry the target CRS AND a geotransform actually derived
  // from the 4326->32633 transform of the source pixel grid. Before the fix
  // the pixels passed through untransformed: geotransform (0,1,0,0,0,-1)
  // tagged EPSG:32633 — absurd for UTM (metre coordinates near 0).
  GDALDatasetH dataset = GDALOpen( output.c_str(), GA_ReadOnly );
  REQUIRE( dataset );
  double gt[6];
  REQUIRE( GDALGetGeoTransform( dataset, gt ) == CE_None );
  // The OUTPUT dataset must carry the target CRS: compare its WKT against
  // EPSG:32633 with OSRIsSame (review R-B2 — the previous check inspected
  // GDAL's constant EPSG definition, which can never fail).
  OGRSpatialReferenceH target = OSRNewSpatialReference( nullptr );
  REQUIRE( OSRImportFromEPSG( target, 32633 ) == OGRERR_NONE );
  const char *outputWkt = GDALGetProjectionRef( dataset );
  REQUIRE( outputWkt != nullptr );
  REQUIRE( std::string( outputWkt ).size() > 0 );
  OGRSpatialReferenceH outputSrs = OSRNewSpatialReference( nullptr );
  // OSRImportFromWkt(hSRS, char**) — args were previously swapped, yielding
  // void**→char** under -fpermissive. Prefer OSRSetFromUserInput so the
  // const WKT from GDALGetProjectionRef needs no mutable/const_cast dance.
  REQUIRE( OSRSetFromUserInput( outputSrs, outputWkt ) == OGRERR_NONE );
  const bool sameCrs = OSRIsSame( outputSrs, target ) != 0;
  OSRDestroySpatialReference( outputSrs );
  OSRDestroySpatialReference( target );
  CHECK( sameCrs );
  // A metre-based UTM grid cannot span only 8 units; the untransformed
  // pixel-grid passthrough produced exactly that. Allow generous bounds —
  // the point is "coordinates transformed", not a specific GDAL version's
  // resampling placement.
  CHECK( std::abs( gt[1] ) > 1.0 ); // pixel size in metres, not degrees-as-pixels
  CHECK( std::abs( gt[0] ) > 100000.0 ); // UTM 33N easting of lon~0 is ~166k
  GDALClose( dataset );
}

// --- #1001: io:clip must treat srcCrsOverride as a SOURCE declaration, ------
// --- never as the clip target ----------------------------------------------

TEST_CASE( "io:clip keeps the source grid for a CRS-less input with srcCrsOverride (#1001)",
           "[io][operators][f-ops-4][1001]" )
{
  const std::string dir = scratch( "clip_crsless" );
  const std::string raster = makeCrsLessRaster( dir, "crsless.tif" );
  const std::string output = ( fs::path( dir ) / "clip.tif" ).string();

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:clip" );
  REQUIRE( op );

  sicnu::operators::RSOperatorContext context;
  // Source pixels live on the (0,0)..(8,-8) grid declared EPSG:4326. A clip
  // to [1,-6,6,-1] must SUBSET that grid — extent exactly the requested
  // bounds, CRS exactly the override — and never reproject (the pre-fix code
  // assigned the override to targetCrs AND left the grid tagged EPSG:4326
  // only by accident of the declaration).
  const Json::Value result = op->execute( [ & ] {
    Json::Value params;
    params["input"] = raster;
    params["output"] = output;
    params["srcCrsOverride"] = "EPSG:4326";
    Json::Value bounds;
    bounds.append( 1.0 );
    bounds.append( -6.0 );
    bounds.append( 6.0 );
    bounds.append( -1.0 );
    params["bounds"] = bounds;
    return params;
  }(), context );

  CHECK( result["output"].asString() == output );

  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( output );
  CHECK( meta.crs.valid );
  CHECK( meta.crs.authid == "EPSG:4326" );
  // Independent oracle: the clip window, checked against the requested
  // bounds (not against the implementation's own result JSON).
  CHECK( meta.minX == Catch::Approx( 1.0 ).margin( 1e-9 ) );
  CHECK( meta.maxX == Catch::Approx( 6.0 ).margin( 1e-9 ) );
  CHECK( meta.minY == Catch::Approx( -6.0 ).margin( 1e-9 ) );
  CHECK( meta.maxY == Catch::Approx( -1.0 ).margin( 1e-9 ) );
}

TEST_CASE( "io:clip refuses srcCrsOverride on an input that already has a CRS (#1001)",
           "[io][operators][negative][1001]" )
{
  const std::string dir = scratch( "clip_override_conflict" );
  const std::string raster = makeTinyRaster( dir, "geo.tif" ); // EPSG:4326 fixture
  const std::string output = ( fs::path( dir ) / "clip.tif" ).string();

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:clip" );
  REQUIRE( op );

  sicnu::operators::RSOperatorContext context;
  try
  {
    op->execute( [ & ] {
      Json::Value params;
      params["input"] = raster;
      params["output"] = output;
      params["srcCrsOverride"] = "EPSG:32648"; // contradicts the file CRS
      Json::Value bounds;
      bounds.append( 116.1 );
      bounds.append( 30.9 );
      bounds.append( 116.5 );
      bounds.append( 30.5 );
      params["bounds"] = bounds;
      return params;
    }(), context );
    FAIL( "io:clip must refuse a contradictory srcCrsOverride" );
  }
  catch ( const sicnu::operators::RSOperatorError &error )
  {
    CHECK( error.code() == sicnu::operators::ErrorCode::InvalidParameter );
    CHECK( error.details()["input_crs"].asString() == "EPSG:4326" );
    CHECK( error.details()["srcCrsOverride"].asString() == "EPSG:32648" );
  }
  // Refusal is fail-closed BEFORE any output: nothing was published.
  CHECK( !sicnu::geo::atomic_fs::fileExists( output ) );
}

TEST_CASE( "io:clip keeps the file CRS and applies bounds when no override is given",
           "[io][operators][1001]" )
{
  const std::string dir = scratch( "clip_georef" );
  const std::string raster = makeTinyRaster( dir, "geo.tif" ); // EPSG:4326, (116,31)..(116.08,30.92)
  const std::string output = ( fs::path( dir ) / "clip.tif" ).string();

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:clip" );
  REQUIRE( op );

  sicnu::operators::RSOperatorContext context;
  op->execute( [ & ] {
    Json::Value params;
    params["input"] = raster;
    params["output"] = output;
    Json::Value bounds;
    bounds.append( 116.02 );
    bounds.append( 30.94 );
    bounds.append( 116.06 );
    bounds.append( 30.98 );
    params["bounds"] = bounds;
    return params;
  }(), context );

  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( output );
  CHECK( meta.crs.authid == "EPSG:4326" );
  CHECK( meta.minX == Catch::Approx( 116.02 ).margin( 1e-6 ) );
  CHECK( meta.maxX == Catch::Approx( 116.06 ).margin( 1e-6 ) );
  CHECK( meta.minY == Catch::Approx( 30.94 ).margin( 1e-6 ) );
  CHECK( meta.maxY == Catch::Approx( 30.98 ).margin( 1e-6 ) );
}

// --- 11.0 interchange operators ----------------------------------------------

TEST_CASE( "io:verify_dataset verifies a manifest-bearing dataset and reports legacy absence",
           "[io][operators][verify]" )
{
  const std::string dir = scratch( "verify_op" );
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:verify_dataset" );
  REQUIRE( op );

  // Build a dataset + manifest through the library seam.
  const std::string staged = ( fs::path( dir ) / "out.4.5.tmp.tif" ).string();
  {
    ensureGdal();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, staged.c_str(), 5, 5, 1, GDT_Byte, nullptr );
    REQUIRE( dataset );
    GDALClose( dataset );
  }
  sicnu::geo::io::StageRecord record;
  record.runId = "run-verify-op";
  record.producer = "test-harness";
  record.finalPath = ( fs::path( dir ) / "out.tif" ).string();
  record.stagedPath = staged;
  record.driver = "GTiff";
  record.width = 5;
  record.height = 5;
  record.bandCount = 1;
  sicnu::geo::io::recordStaged( record );
  sicnu::geo::io::FinalizeManifestFields fields;
  fields.producer = "test-harness";
  fields.driver = "GTiff";
  fields.width = 5;
  fields.height = 5;
  fields.bandCount = 1;
  fields.dtype = "Byte";
  sicnu::geo::io::finalizeAttached( record.finalPath, &fields );

  sicnu::operators::RSOperatorContext context;
  {
    const Json::Value result = op->execute( [ & ] {
      Json::Value params;
      params["input"] = record.finalPath;
      return params;
    }(), context );
    CHECK( result["verified"].asBool() );
    CHECK( result["manifest_present"].asBool() );
    CHECK( result["digest_matched"].asBool() );
  }
  {
    // Legacy dataset (exists, but written without a manifest): fail-closed.
    const std::string legacy = ( fs::path( dir ) / "legacy.tif" ).string();
    {
      ensureGdal();
      GDALDriverH driver = GDALGetDriverByName( "GTiff" );
      REQUIRE( driver );
      GDALDatasetH dataset = GDALCreate( driver, legacy.c_str(), 3, 3, 1, GDT_Byte, nullptr );
      REQUIRE( dataset );
      GDALClose( dataset );
    }
    const Json::Value result = op->execute( [ & ] {
      Json::Value params;
      params["input"] = legacy;
      return params;
    }(), context );
    CHECK_FALSE( result["verified"].asBool() );
    CHECK( result["issues"][0]["code"].asString() == "manifest_missing" );
  }
  {
    // Legacy tolerance flips the issue code but never the verdict.
    const std::string legacy = ( fs::path( dir ) / "legacy.tif" ).string();
    const Json::Value result = op->execute( [ & ] {
      Json::Value params;
      params["input"] = legacy;
      params["allowMissingManifest"] = true;
      return params;
    }(), context );
    CHECK_FALSE( result["verified"].asBool() );
    CHECK( result["issues"][0]["code"].asString() == "manifest_missing_allowed" );
  }
}

TEST_CASE( "io:metadata_patch refuses non-whitelist fields through the operator seam",
           "[io][operators][patch]" )
{
  const std::string dir = scratch( "patch_op" );
  const std::string raster = makeTinyRaster( dir, "grid.tif" );
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:metadata_patch" );
  REQUIRE( op );

  sicnu::operators::RSOperatorContext context;
  try
  {
    op->execute( [ & ] {
      Json::Value params;
      params["input"] = raster;
      Json::Value patches;
      Json::Value entry;
      entry["band"] = 1;
      entry["field"] = "brightness"; // not in the whitelist
      entry["value"] = "10";
      patches.append( entry );
      params["patches"] = patches;
      return params;
    }(), context );
    FAIL( "non-whitelist field must be refused" );
  }
  catch ( const sicnu::operators::RSOperatorError &error )
  {
    CHECK( error.code() == sicnu::operators::ErrorCode::InvalidParameter );
  }
}

TEST_CASE( "io:subdatasets refuses plain rasters through the operator seam",
           "[io][operators][subdatasets]" )
{
  const std::string dir = scratch( "subdatasets_op" );
  const std::string raster = makeTinyRaster( dir, "grid.tif" );
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:subdatasets" );
  REQUIRE( op );

  sicnu::operators::RSOperatorContext context;
  try
  {
    op->execute( [ & ] {
      Json::Value params;
      params["input"] = raster;
      return params;
    }(), context );
    FAIL( "plain rasters carry no SUBDATASETS domain" );
  }
  catch ( const sicnu::operators::RSOperatorError &error )
  {
    CHECK( error.code() == sicnu::operators::ErrorCode::InvalidParameter );
  }
}
