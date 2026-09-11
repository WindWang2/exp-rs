/***************************************************************************
  tests/test_io_roundtrip_matrix.cpp
  Geospatial I/O Foundation 4.0 — certified round-trip matrix.
  GeoTIFF→GeoTIFF · GeoTIFF→COG→GeoTIFF · GeoPackage→GeoJSON→GeoPackage ·
  GeoTIFF→VRT · NetCDF slice→GeoTIFF (driver-gated) · STAC Item→canonical.
  Driver-gated cases self-skip WITH a logged reason.
 ***************************************************************************/

#include "geospatial/convert/raster_convert.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/multidim/multidim_view.h"
#include "geospatial/cog/cog_validator.h"
#include "geospatial/formats/format_profiles.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/stac/stac_mapper.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>
#include <netcdf.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_roundtrip" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

bool driverPresent( const char *name )
{
  ensureGdal();
  return GDALGetDriverByName( name ) != nullptr;
}

/// Source raster used across the matrix: 12x10, 2 bands, nodata+scale+offset,
/// EPSG:4326, deterministic pixel values.
std::string writeSourceGeoTiff( const std::string &dir )
{
  const std::string target = ( fs::path( dir ) / "source.tif" ).string();
  sicnu::geo::RasterBandSpec b1;
  b1.dtype = "Float32";
  b1.description = "Red";
  b1.role = "Red";
  b1.hasNoData = true;
  b1.noDataValue = -9999.0;
  b1.hasScale = true;
  b1.scale = 0.01;
  b1.hasOffset = true;
  b1.offset = -1.0;
  b1.hasWavelength = true;
  b1.wavelengthNm = 665.0;
  sicnu::geo::RasterBandSpec b2 = b1;
  b2.description = "NIR";
  b2.role = "NIR";
  b2.wavelengthNm = 842.0;

  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 12, 10, { b1, b2 }, {} );
  writer.setGeotransform( { 116.0, 0.01, 0.0, 31.0, 0.0, -0.01 } );
  writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
  writer.setDatasetMetadataItem( "SICNU_SENSOR", "MATRIX_SENSOR" );
  sicnu::geo::RasterWindow full;
  full.width = 12;
  full.height = 10;
  std::vector<double> values( 120 );
  for ( std::size_t i = 0; i < values.size(); ++i )
    values[i] = ( i % 17 == 0 ) ? -9999.0 : static_cast<double>( i );
  writer.writeWindow( 1, full, values.data() );
  for ( std::size_t i = 0; i < values.size(); ++i )
    values[i] = static_cast<double>( i ) * 2.0;
  writer.writeWindow( 2, full, values.data() );
  writer.finalize();
  return target;
}

void verifyRasterFidelity( sicnu::geo::RasterReader &reader, const sicnu::geo::RasterMetadata &expected )
{
  CHECK( reader.metadata().width == expected.width );
  CHECK( reader.metadata().height == expected.height );
  CHECK( reader.metadata().bandCount == expected.bandCount );
  CHECK( reader.metadata().crs.authid == expected.crs.authid );
  CHECK( reader.metadata().hasExtent );
  CHECK( reader.metadata().minX == Approx( expected.minX ).margin( 1e-9 ) );
  CHECK( reader.metadata().maxY == Approx( expected.maxY ).margin( 1e-9 ) );
  REQUIRE( reader.metadata().bands.size() == expected.bands.size() );
  for ( std::size_t i = 0; i < expected.bands.size(); ++i )
  {
    CHECK( reader.metadata().bands[i].hasNoData == expected.bands[i].hasNoData );
    CHECK( reader.metadata().bands[i].noDataValue == Approx( expected.bands[i].noDataValue ) );
    CHECK( reader.metadata().bands[i].hasScale == expected.bands[i].hasScale );
    CHECK( reader.metadata().bands[i].scale == Approx( expected.bands[i].scale ) );
    CHECK( reader.metadata().bands[i].hasOffset == expected.bands[i].hasOffset );
    CHECK( reader.metadata().bands[i].offset == Approx( expected.bands[i].offset ) );
    CHECK( reader.metadata().bands[i].role == expected.bands[i].role );
  }
}

} // namespace

TEST_CASE( "matrix: GeoTIFF → GeoTIFF preserves full fidelity", "[io][roundtrip][geotiff]" )
{
  const std::string dir = scratch( "tiff_tiff" );
  const std::string source = writeSourceGeoTiff( dir );
  const std::string target = ( fs::path( dir ) / "copy.tif" ).string();

  sicnu::geo::TranslateOptions options;
  options.creationOptions = { "COMPRESS=DEFLATE" };
  const sicnu::geo::TranslateResult result = sicnu::geo::translateRaster( source, target, options );
  CHECK( result.bandCount == 2 );

  sicnu::geo::RasterReader expectedReader = sicnu::geo::RasterReader::open( source );
  sicnu::geo::RasterReader actualReader = sicnu::geo::RasterReader::open( target );
  verifyRasterFidelity( actualReader, expectedReader.metadata() );

  // Bit-level value equality including nodata markers.
  sicnu::geo::RasterWindow full;
  full.width = 12;
  full.height = 10;
  const std::vector<double> srcValues = expectedReader.readWindow( { 1, 2 }, full );
  const std::vector<double> dstValues = actualReader.readWindow( { 1, 2 }, full );
  REQUIRE( srcValues.size() == dstValues.size() );
  for ( std::size_t i = 0; i < srcValues.size(); ++i )
    CHECK( dstValues[i] == srcValues[i] );
}

TEST_CASE( "matrix: GeoTIFF → COG → GeoTIFF stays lossless and validates", "[io][roundtrip][cog]" )
{
  const std::string dir = scratch( "cog" );
  const std::string source = writeSourceGeoTiff( dir );
  const std::string cog = ( fs::path( dir ) / "product.tif" ).string();
  const std::string back = ( fs::path( dir ) / "restored.tif" ).string();

  sicnu::geo::TranslateResult cogResult = sicnu::geo::makeCog( source, cog, sicnu::geo::CogPreset::LosslessScientific );
  CHECK( cogResult.bandCount == 2 );

  const sicnu::geo::CogValidationReport report = sicnu::geo::validateCog( cog );
  if ( !report.isCog )
  {
    WARN( "COG validation failed on this GDAL build: " << report.toJson().toStyledString() );
  }
  CHECK( report.isCog );

  sicnu::geo::TranslateOptions plain;
  sicnu::geo::translateRaster( cog, back, plain );

  sicnu::geo::RasterReader expectedReader = sicnu::geo::RasterReader::open( source );
  sicnu::geo::RasterReader actualReader = sicnu::geo::RasterReader::open( back );
  verifyRasterFidelity( actualReader, expectedReader.metadata() );
}

TEST_CASE( "matrix: GeoPackage → GeoJSON → GeoPackage preserves attributes and geometry", "[io][roundtrip][vector]" )
{
  const std::string dir = scratch( "vector" );
  const std::string gpkg1 = ( fs::path( dir ) / "a.gpkg" ).string();
  const std::string geojson = ( fs::path( dir ) / "b.geojson" ).string();
  const std::string gpkg2 = ( fs::path( dir ) / "c.gpkg" ).string();

  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    gpkg1, "sites", "Point",
    { { "label", "String", 64 }, { "score", "Real" }, { "zone", "Integer64" } },
    sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
  for ( int i = 0; i < 25; ++i )
  {
    Json::Value attrs( Json::objectValue );
    attrs["label"] = "site-" + std::to_string( i );
    attrs["score"] = i * 1.5;
    attrs["zone"] = static_cast<Json::Int64>( i % 5 );
    const double x = 100.0 + i * 0.1;
    const double y = 20.0 + i * 0.05;
    writer.writeFeature( attrs, "POINT (" + std::to_string( x ) + " " + std::to_string( y ) + ")" );
  }
  writer.finalize();

  sicnu::geo::vectorConvert( gpkg1, geojson, "GeoJSON", "sites", "", "", {} );
  sicnu::geo::vectorConvert( geojson, gpkg2, "GPKG", "sites", "", "", {} );

  // Compare final layer against the original through the streaming contract.
  sicnu::geo::VectorReader origin = sicnu::geo::VectorReader::open( gpkg1 );
  sicnu::geo::VectorReader restored = sicnu::geo::VectorReader::open( gpkg2 );
  CHECK( restored.layerInfo().featureCount == origin.layerInfo().featureCount );
  CHECK( restored.layerInfo().crs.authid == "EPSG:4326" );

  std::vector<sicnu::geo::VectorFeature> originBatch;
  std::vector<sicnu::geo::VectorFeature> restoredBatch;
  while ( origin.nextBatch( originBatch, 10 ) || !originBatch.empty() )
  {
    REQUIRE( restored.nextBatch( restoredBatch, 10 ) );
    REQUIRE( restoredBatch.size() == originBatch.size() );
    for ( std::size_t i = 0; i < originBatch.size(); ++i )
    {
      CHECK( restoredBatch[i].attributes["label"].asString() == originBatch[i].attributes["label"].asString() );
      CHECK( restoredBatch[i].attributes["score"].asDouble() == Approx( originBatch[i].attributes["score"].asDouble() ) );
      // Geometry survives as equivalent WKT points (format may reformat digits).
      const std::string &originWkt = originBatch[i].geometryWkt;
      const std::string &restoredWkt = restoredBatch[i].geometryWkt;
      const auto originOpen = originWkt.find( '(' );
      const auto restoredOpen = restoredWkt.find( '(' );
      REQUIRE( originOpen != std::string::npos );
      REQUIRE( restoredOpen != std::string::npos );
      const double originX = std::stod( originWkt.substr( originOpen + 1 ) );
      const double restoredX = std::stod( restoredWkt.substr( restoredOpen + 1 ) );
      CHECK( originX == Approx( restoredX ).margin( 1e-6 ) );
    }
    originBatch.clear();
    restoredBatch.clear();
  }
}

TEST_CASE( "matrix: GeoTIFF → VRT reads identical windows", "[io][roundtrip][vrt]" )
{
  const std::string dir = scratch( "vrt" );
  const std::string source = writeSourceGeoTiff( dir );
  const std::string vrt = ( fs::path( dir ) / "virtual.vrt" ).string();

  sicnu::geo::TranslateOptions options;
  options.outputFormat = "VRT";
  sicnu::geo::translateRaster( source, vrt, options );

  sicnu::geo::RasterReader vrtReader = sicnu::geo::RasterReader::open( vrt );
  sicnu::geo::RasterReader srcReader = sicnu::geo::RasterReader::open( source );
  verifyRasterFidelity( vrtReader, srcReader.metadata() );

  sicnu::geo::RasterWindow window;
  window.xOff = 2;
  window.yOff = 3;
  window.width = 7;
  window.height = 5;
  const std::vector<double> fromVrt = vrtReader.readWindow( { 1 }, window );
  const std::vector<double> fromSrc = srcReader.readWindow( { 1 }, window );
  REQUIRE( fromVrt.size() == fromSrc.size() );
  for ( std::size_t i = 0; i < fromVrt.size(); ++i )
    CHECK( fromVrt[i] == fromSrc[i] );
}

TEST_CASE( "matrix: NetCDF slice → GeoTIFF", "[io][roundtrip][netcdf][multidim]" )
{
  if ( !driverPresent( "netCDF" ) )
  {
    WARN( "netCDF driver not present in this build — round-trip case self-skipped" );
    return;
  }

  const std::string dir = scratch( "netcdf" );
  const std::string nc = ( fs::path( dir ) / "cube.nc" ).string();
  const std::string target = ( fs::path( dir ) / "slice.tif" ).string();

  // Author a small CF cube with the netCDF C library (the suite exercises
  // the foundation READ/slice side).
  {
    ensureGdal();
    int ncid = -1;
    REQUIRE( nc_create( nc.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
    int timeId = -1, yId = -1, xId = -1;
    REQUIRE( nc_def_dim( ncid, "time", 2, &timeId ) == NC_NOERR );
    REQUIRE( nc_def_dim( ncid, "y", 3, &yId ) == NC_NOERR );
    REQUIRE( nc_def_dim( ncid, "x", 4, &xId ) == NC_NOERR );
    int dimIds[3] = { timeId, yId, xId };
    int varId = -1;
    REQUIRE( nc_def_var( ncid, "temperature", NC_FLOAT, 3, dimIds, &varId ) == NC_NOERR );
    REQUIRE( nc_enddef( ncid ) == NC_NOERR );
    const float values[2 * 3 * 4] = {
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
      13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    };
    REQUIRE( nc_put_var_float( ncid, varId, values ) == NC_NOERR );
    REQUIRE( nc_close( ncid ) == NC_NOERR );
  }

  // Lazy listing: metadata shows the variable and its dimensions.
  {
    sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( nc );
    bool found = false;
    for ( const sicnu::geo::VariableInfo &variable : view.metadata().variables )
      found = found || variable.name == "temperature";
    CHECK( found );

    const sicnu::geo::MultidimGrid slice = view.readTemporalOrLevelSlice( "temperature", "time", 1 );
    REQUIRE( slice.rows == 3 );
    REQUIRE( slice.cols == 4 );
    CHECK( slice.values[0] == 13.0 );
    CHECK( slice.values[11] == 24.0 );
  }

  // Slice → GeoTIFF through the atomic writer.
  sicnu::geo::MultidimView view = sicnu::geo::MultidimView::open( nc );
  const sicnu::geo::MultidimGrid slice = view.readTemporalOrLevelSlice( "temperature", "time", 1 );
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, static_cast<int>( slice.cols ),
                                                                      static_cast<int>( slice.rows ),
                                                                      { sicnu::geo::RasterBandSpec{} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = static_cast<int>( slice.cols );
  full.height = static_cast<int>( slice.rows );
  writer.writeWindow( 1, full, slice.values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  CHECK( reader.metadata().width == 4 );
  CHECK( reader.metadata().height == 3 );
  const std::vector<double> pixels = reader.readWindow( { 1 }, full );
  for ( std::size_t i = 0; i < pixels.size(); ++i )
    CHECK( pixels[i] == slice.values[i] );
}

TEST_CASE( "matrix: STAC Item → canonical metadata", "[io][roundtrip][stac]" )
{
  const std::string itemJson = R"({
    "type": "Feature",
    "stac_version": "1.0.0",
    "id": "S2B_MSIL1C_TEST",
    "bbox": [116.0, 30.0, 117.5, 31.5],
    "geometry": { "type": "Polygon", "coordinates": [[[116.0,30.0],[117.5,30.0],[117.5,31.5],[116.0,31.5],[116.0,30.0]]] },
    "properties": {
      "datetime": "2026-08-30T02:31:00Z",
      "platform": "SENTINEL-2B",
      "constellation": "SENTINEL-2",
      "s2:processing_level": "Level-1C",
      "eo:cloud_cover": 12.5,
      "gsd": 10.0,
      "proj:epsg": 32648,
      "sar:polarizations": ["VV", "VH"]
    },
    "assets": {
      "thumbnail": { "href": "https://example.test/thumb.jpg", "type": "image/jpeg", "roles": ["thumbnail"] },
      "data": { "href": "https://example.test/scene.tif", "type": "image/tiff; application=geotiff", "roles": ["data"] }
    }
  })";

  const sicnu::geo::StacItem item = sicnu::geo::StacItem::parseText( itemJson );
  const sicnu::geo::RasterMetadata canonical = sicnu::geo::stacItemToCanonical( item );

  CHECK( canonical.productId == "S2B_MSIL1C_TEST" );
  CHECK( canonical.platform == "SENTINEL-2B" );
  CHECK( canonical.acquisitionTime == "2026-08-30T02:31:00Z" );
  CHECK( canonical.hasCloudCover );
  CHECK( canonical.cloudCover == Approx( 12.5 ) );
  CHECK( canonical.hasGsd );
  CHECK( canonical.gsd == Approx( 10.0 ) );
  CHECK( canonical.crs.valid );
  CHECK( canonical.crs.authid == "EPSG:32648" );
  CHECK( canonical.metadata.count( "sar:polarizations" ) == 1 );
  CHECK( canonical.path == "https://example.test/scene.tif" ); // data role wins

  // And back: canonical → STAC-compatible Item survives the key facts.
  sicnu::geo::RasterMetadata enriched = canonical;
  const Json::Value regenerated = sicnu::geo::canonicalToStacItem( enriched, canonical.path, canonical.productId );
  CHECK( regenerated["properties"]["datetime"].asString() == "2026-08-30T02:31:00Z" );
  CHECK( regenerated["properties"]["eo:cloud_cover"].asDouble() == Approx( 12.5 ) );
  CHECK( regenerated["properties"]["proj:epsg"].asInt() == 32648 );
  CHECK( regenerated["assets"]["data"]["href"].asString() == canonical.path );

  // Re-parse of the generated document is lossless for the mapped fields.
  const sicnu::geo::StacItem reparsed = sicnu::geo::StacItem::parse( regenerated );
  CHECK( reparsed.hasCloudCover );
  CHECK( reparsed.cloudCover == Approx( 12.5 ) );
  CHECK( reparsed.epsg == "EPSG:32648" );
}

TEST_CASE( "registry: certified profiles are actually exercised by this suite", "[io][roundtrip][registry]" )
{
  const sicnu::geo::FormatRegistry &registry = sicnu::geo::FormatRegistry::instance();
  const sicnu::geo::FormatProfile *geotiff = registry.find( "GeoTIFF" );
  REQUIRE( geotiff );
  CHECK( geotiff->certification == sicnu::geo::Certification::Certified );
  CHECK( registry.driverAvailable( *geotiff ) );

  const sicnu::geo::FormatProfile *cog = registry.find( "COG" );
  REQUIRE( cog );
  CHECK( cog->certification == sicnu::geo::Certification::Certified );

  // The support matrix must never claim certification without a driver.
  const Json::Value matrix = registry.supportMatrixJson();
  for ( const Json::Value &entry : matrix["formats"] )
  {
    if ( entry["certification"].asString() == "certified" )
    {
      const std::string id = entry["id"].asString();
      const sicnu::geo::FormatProfile *profile = registry.find( id );
      REQUIRE( profile );
      if ( !profile->driverNames.empty() )
        CHECK( registry.driverAvailable( *profile ) );
    }
  }
}

// ---------------------------------------------------------------------------
// 9.0 M6 — FlatGeobuf round-trip: the certification upgrade runs through the
// streaming read contract WITH filters (attribute projection + bbox), the
// coverage the profile note said was pending. Runtime driver-gated: a GDAL
// build without FlatGeobuf SKIPS honestly and the profile stays honest.
// ---------------------------------------------------------------------------

TEST_CASE( "matrix: GeoPackage → FlatGeobuf → GeoPackage preserves fidelity"
           " through filtered streaming reads",
           "[io][roundtrip][vector][fabric9][flatgeobuf]" )
{
  const sicnu::geo::FormatRegistry &registry = sicnu::geo::FormatRegistry::instance();
  const sicnu::geo::FormatProfile *fgb = registry.find( "FlatGeobuf" );
  REQUIRE( fgb );
  if ( !registry.driverAvailable( *fgb ) )
  {
    WARN( "FlatGeobuf driver not available in this GDAL build — round-trip skipped (profile stays honest)" );
    return;
  }

  const std::string dir = scratch( "fgb9" );
  const std::string gpkg1 = ( fs::path( dir ) / "a.gpkg" ).string();
  const std::string fgbPath = ( fs::path( dir ) / "b.fgb" ).string();
  const std::string gpkg2 = ( fs::path( dir ) / "c.gpkg" ).string();

  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    gpkg1, "sites", "Point",
    { { "label", "String", 64 }, { "score", "Real" }, { "zone", "Integer64" } },
    sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
  for ( int i = 0; i < 30; ++i )
  {
    Json::Value attrs( Json::objectValue );
    attrs["label"] = "site-" + std::to_string( i );
    attrs["score"] = i * 2.25;
    attrs["zone"] = static_cast<Json::Int64>( i % 3 );
    const double x = 100.0 + i * 0.1;
    const double y = 20.0 + i * 0.05;
    writer.writeFeature( attrs, "POINT (" + std::to_string( x ) + " " + std::to_string( y ) + ")" );
  }
  writer.finalize();

  sicnu::geo::vectorConvert( gpkg1, fgbPath, "FlatGeobuf", "sites", "", "", {} );
  sicnu::geo::vectorConvert( fgbPath, gpkg2, "GPKG", "sites", "", "", {} );

  sicnu::geo::VectorReader origin = sicnu::geo::VectorReader::open( gpkg1 );
  sicnu::geo::VectorReader restored = sicnu::geo::VectorReader::open( gpkg2 );
  CHECK( restored.layerInfo().featureCount == origin.layerInfo().featureCount );
  CHECK( restored.layerInfo().crs.authid == "EPSG:4326" );

  // Filtered streaming equivalence: the same attribute projection + bbox
  // filter over both sides must stream identical features.
  const std::string projectedFields[] = { "label", "zone" };
  origin.setAttributeProjection( { projectedFields[0], projectedFields[1] } );
  restored.setAttributeProjection( { projectedFields[0], projectedFields[1] } );
  origin.setSpatialFilter( { 100.0, 20.0, 101.5, 20.75 } ); // first ~15 features
  restored.setSpatialFilter( { 100.0, 20.0, 101.5, 20.75 } );

  // FlatGeobuf orders filtered iteration through its spatial index — the
  // streaming contract bounds memory, not cross-format feature order. So
  // the fidelity comparison is over the COLLECTED, label-sorted streams.
  std::vector<sicnu::geo::VectorFeature> originAll;
  std::vector<sicnu::geo::VectorFeature> restoredAll;
  std::vector<sicnu::geo::VectorFeature> originBatch;
  std::vector<sicnu::geo::VectorFeature> restoredBatch;
  while ( origin.nextBatch( originBatch, 7 ) || !originBatch.empty() )
  {
    REQUIRE( restored.nextBatch( restoredBatch, 7 ) );
    REQUIRE( restoredBatch.size() == originBatch.size() );
    for ( std::size_t i = 0; i < originBatch.size(); ++i )
      CHECK_FALSE( originBatch[i].attributes.isMember( "score" ) ); // projection honored on both sides
    for ( const sicnu::geo::VectorFeature &feature : restoredBatch )
      CHECK_FALSE( feature.attributes.isMember( "score" ) );
    originAll.insert( originAll.end(), originBatch.begin(), originBatch.end() );
    restoredAll.insert( restoredAll.end(), restoredBatch.begin(), restoredBatch.end() );
    originBatch.clear();
    restoredBatch.clear();
  }
  // i = 0..15 lie inside the inclusive bbox (x = 100 + i*0.1 <= 101.5,
  // y = 20 + i*0.05 <= 20.75) — 16 features on both sides.
  REQUIRE( originAll.size() == 16 );
  REQUIRE( restoredAll.size() == 16 );
  const auto byLabel = []( const sicnu::geo::VectorFeature &f ) { return f.attributes["label"].asString(); };
  std::sort( originAll.begin(), originAll.end(), [ & ]( const auto &a, const auto &b ) { return byLabel( a ) < byLabel( b ); } );
  std::sort( restoredAll.begin(), restoredAll.end(), [ & ]( const auto &a, const auto &b ) { return byLabel( a ) < byLabel( b ); } );
  for ( std::size_t i = 0; i < originAll.size(); ++i )
  {
    CHECK( restoredAll[i].attributes["label"].asString() == originAll[i].attributes["label"].asString() );
    CHECK( restoredAll[i].attributes["zone"].asInt64() == originAll[i].attributes["zone"].asInt64() );
  }
}
