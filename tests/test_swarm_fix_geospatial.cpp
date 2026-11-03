/***************************************************************************
  tests/test_swarm_fix_geospatial.cpp
  swarm-review 2026-11-04 — GEO-1..GEO-11 + crs_policy regression anchors.
  ---------------------------
  Each case pins one finding from SWARM_REVIEW.md against the geospatial
  module. Fixtures are generated at runtime (repo convention: no committed
  geodata); driver-gated cases self-skip with a logged reason.

  Findings anchored here:
    GEO-1  vector writer accepts OGR's DISPLAY geometry dialect
           ("Line String", "Multi Polygon", "3D Point", ...) — the only
           spelling OGRGeometryTypeToName emits, hence the only one a
           conversion ever carries.
    GEO-2  FWHM is normalized through WAVELENGTH_UNITS like WAVELENGTH.
    GEO-3  inspectRaster extent takes all four corners (rotated grids).
    GEO-4  the cube scatter carries the source dtype into the #874
           Float32 sentinel comparison.
    GEO-6  product sidecars open through the wide-char filesystem path
           (Windows contract anchor: on POSIX the narrow path also works,
           so this case documents the contract rather than detecting a
           host-dependent failure).
    GEO-8  ResourceUri's VSI prefix table and offline_gate's kNetworkVsi
           name the same network family (interlock test).
    GEO-9  GET search URLs render bbox at round-trip precision.
    GEO-11 the raster writer refuses 64-bit integer dtypes (reader symmetry).
    crs   malformed EPSG authids are refused, never silently truncated.

  Not anchored here (by design):
    GEO-5  config key spelling inside a function-local scope object — no
           observable surface without a network origin.
    GEO-7  the removed declarations are a compile-time contract: this TU
           including stac_client.h and compiling IS the anchor.
    GEO-10 the fallbackRead calls moved outside the fetch mutex keep their
           exact semantics; the behavioral anchors are the existing
           test_io_range_cache.cpp cases ("mid-range connection reset
           degrades to the fallback without wrong bytes", "truncated 206
           bodies are refused — fallback keeps reads byte-correct"), which
           drive the same transportFailed/empty-run fallback paths.
 ***************************************************************************/

#include "geospatial/convert/raster_convert.h"
#include "geospatial/crs/crs_policy.h"
#include "geospatial/fabric/virtual_cube.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/stac/stac_client.h"
#include "geospatial/util/gdal_compat.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"

#include "platform/portable.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_srs_api.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_swarm_fix_geospatial" / name;
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

// ─── GEO-1 fixtures ────────────────────────────────────────────────────────

/// Writes a one-feature GPKG layer declared with the given geometry type
/// name (either dialect) and returns the path.
std::string writeVectorLayer( const std::string &dir, const std::string &fileName,
                              const std::string &layerName, const std::string &geometryTypeName,
                              const std::string &wkt )
{
  const std::string path = ( fs::path( dir ) / fileName ).string();
  std::vector<sicnu::geo::VectorFieldSpec> fields = { { "label", "String", 32 } };
  sicnu::geo::VectorWriter writer =
    sicnu::geo::VectorWriter::create( path, layerName, geometryTypeName, fields,
                                      sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
  Json::Value attributes( Json::objectValue );
  attributes["label"] = layerName;
  writer.writeFeature( attributes, wkt );
  writer.finalize();
  return path;
}

} // namespace

// ---------------------------------------------------------------------------
// GEO-1 [P1]: the mapping table spoke the compact WKT dialect while the only
// producer of geometryTypeName (OGRGeometryTypeToName) speaks OGR's display
// dialect — LineString/Multi*/25D/collection layers all threw
// "unknown geometry type" at the writer.
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-1: OGR display-dialect geometry layers convert end to end",
           "[swarm][geo-1][vector]" )
{
  if ( !driverPresent( "GPKG" ) || !driverPresent( "GeoJSON" ) )
  {
    WARN( "GPKG/GeoJSON drivers unavailable on this GDAL build; case skipped" );
    return;
  }
  const std::string dir = scratch( "geo1_convert" );

  struct Case
  {
    const char *label;
    const char *createName;   // spelling handed to the writer
    const char *expectedName; // spelling OGRGeometryTypeToName emits back
    const char *wkt;
  };
  // Road-network style LineString, multi-part polygons, and a 25D layer:
  // the three families that were 100% broken before the alias table.
  const Case cases[] = {
    { "linestring", "LineString", "Line String", "LINESTRING (100 20, 101 21, 102 22)" },
    { "multipolygon", "MultiPolygon", "Multi Polygon",
      "MULTIPOLYGON (((100 20, 101 20, 101 21, 100 20)), ((102 22, 103 22, 103 23, 102 22)))" },
    { "point3d", "3D Point", "3D Point", "POINT Z (100 20 3)" },
  };

  for ( const Case &c : cases )
  {
    const std::string source = writeVectorLayer( dir, std::string( c.label ) + ".gpkg",
                                                 c.label, c.createName, c.wkt );
    // The reader reports the layer in OGR's display dialect — this is the
    // exact string the writer used to reject.
    const sicnu::geo::VectorReader before = sicnu::geo::VectorReader::open( source, c.label );
    REQUIRE( before.layerInfo().geometryTypeName == std::string( c.expectedName ) );

    const std::string target = ( fs::path( dir ) / ( std::string( c.label ) + ".geojson" ) ).string();
    // Before the fix: GeoError "unknown geometry type" from VectorWriter.
    sicnu::geo::vectorConvert( source, target, "GeoJSON", c.label, "", "", {} );

    // nextBatch() advances the stream, so the reader cannot be const here.
    sicnu::geo::VectorReader restored = sicnu::geo::VectorReader::open( target, c.label );
    CHECK( restored.layerInfo().featureCount == 1 );
    CHECK( restored.layerInfo().geometryTypeName == std::string( c.expectedName ) );
    std::vector<sicnu::geo::VectorFeature> batch;
    REQUIRE( restored.nextBatch( batch, 8 ) );
    REQUIRE( batch.size() == 1 );
    CHECK( batch[0].attributes["label"].asString() == std::string( c.label ) );
    CHECK( batch[0].geometryWkt.find( "100" ) != std::string::npos );
    if ( std::string( c.label ) == "point3d" )
      CHECK( batch[0].geometryWkt.find( "20 3" ) != std::string::npos ); // Z survived
  }
}

TEST_CASE( "GEO-1: VectorWriter accepts every OGR display-dialect geometry name",
           "[swarm][geo-1][vector]" )
{
  if ( !driverPresent( "GPKG" ) )
  {
    WARN( "GPKG driver unavailable on this GDAL build; case skipped" );
    return;
  }
  const std::string dir = scratch( "geo1_names" );
  const std::vector<sicnu::geo::VectorFieldSpec> fields = { { "label", "String", 32 } };

  // Every name OGRGeometryTypeToName can emit for a creatable layer type,
  // verified against the runtime libgdal. The base + 25D families are the
  // regression (they were 100% broken); the curve spellings only have to
  // RESOLVE — whether a driver can create the type is its own decision, and
  // a refusal there is a typed WriteFailed, never "unknown geometry type".
  static const char *const kRequiredNames[] = {
    "Line String", "Multi Point", "Multi Line String", "Multi Polygon",
    "Geometry Collection",
    "3D Point", "3D Line String", "3D Polygon", "3D Multi Point",
    "3D Multi Line String", "3D Multi Polygon", "3D Geometry Collection",
  };
  int index = 0;
  for ( const char *name : kRequiredNames )
  {
    const std::string path = ( fs::path( dir ) / ( "l" + std::to_string( index++ ) + ".gpkg" ) ).string();
    try
    {
      sicnu::geo::VectorWriter writer =
        sicnu::geo::VectorWriter::create( path, "layer", name, fields,
                                          sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
      writer.cancel();
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      FAIL( "VectorWriter refused the OGR display name \"" << name << "\": " << error.what() );
    }
  }

  static const char *const kCurveNames[] = {
    "Circular String", "Compound Curve", "Curve Polygon", "Multi Curve",
    "Multi Surface", "Curve", "Surface", "PolyhedralSurface", "TIN", "Triangle",
    "3D Circular String", "3D Compound Curve", "3D Curve Polygon",
    "3D Multi Curve", "3D Multi Surface", "3D Curve", "3D Surface",
    "3D PolyhedralSurface", "3D TIN", "3D Triangle",
  };
  for ( const char *name : kCurveNames )
  {
    const std::string path = ( fs::path( dir ) / ( "c" + std::to_string( index++ ) + ".gpkg" ) ).string();
    try
    {
      sicnu::geo::VectorWriter writer =
        sicnu::geo::VectorWriter::create( path, "layer", name, fields,
                                          sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
      writer.cancel();
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      // The name RESOLVED (no InvalidArgument "unknown geometry type"); a
      // driver that cannot create the type may still refuse it.
      INFO( name << " -> " << error.what() );
      CHECK( error.code() != sicnu::geo::ErrorCode::InvalidArgument );
    }
  }

  // The compact dialect keeps working (operator schemas / direct callers).
  {
    const std::string path = ( fs::path( dir ) / "compact.gpkg" ).string();
    sicnu::geo::VectorWriter writer =
      sicnu::geo::VectorWriter::create( path, "layer", "LineString", fields,
                                        sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
    writer.cancel();
  }

  // Fail-closed gate: an unknown/mixed-geometry layer is still refused, and
  // the refusal names the spelling the reader actually produced.
  {
    const std::string path = ( fs::path( dir ) / "unknown.gpkg" ).string();
    bool threw = false;
    try
    {
      sicnu::geo::VectorWriter::create( path, "layer", "Unknown (any)", fields,
                                        sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      threw = true;
      CHECK( error.code() == sicnu::geo::ErrorCode::InvalidArgument );
      CHECK( error.details()["geometry_type"].asString() == "Unknown (any)" );
    }
    CHECK( threw );

    const std::string typo = ( fs::path( dir ) / "typo.gpkg" ).string();
    CHECK_THROWS_AS( sicnu::geo::VectorWriter::create( typo, "layer", "Line Strng", fields,
                                                       sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} ),
                     sicnu::geo::GeoError );
  }
}

// ---------------------------------------------------------------------------
// GEO-2 [P2]: FWHM was read raw while WAVELENGTH was normalized through
// WAVELENGTH_UNITS — an ENVI µm library FWHM was off by the unit factor.
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-2: FWHM shares the WAVELENGTH_UNITS normalization", "[swarm][geo-2][metadata]" )
{
  ensureGdal();
  const std::string dir = scratch( "geo2" );
  const std::string path = ( fs::path( dir ) / "spectral.tif" ).string();

  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 4, 3, 4, GDT_Float32, nullptr );
  REQUIRE( dataset );
  double geotransform[6] = { 100.0, 0.25, 0.0, 40.0, 0.0, -0.25 };
  REQUIRE( GDALSetGeoTransform( dataset, geotransform ) == CE_None );

  struct BandMeta
  {
    const char *wavelength;
    const char *units;
    const char *fwhm;
  };
  const BandMeta bands[4] = {
    // ENVI/HDF-EOS µm family: both values in µm (0.665 µm / 0.06 µm).
    { "0.665", "um", "0.06" },
    // Already nm: both values pass through unchanged.
    { "560", "nm", "60" },
    // No unit key at all: the historical raw reading.
    { "490", nullptr, "50" },
    // Unrecognized unit: fail closed — absence stays absence for BOTH.
    { "0.5", "furlongs", "0.05" },
  };
  for ( int i = 1; i <= 4; ++i )
  {
    GDALRasterBandH band = GDALGetRasterBand( dataset, i );
    REQUIRE( band );
    GDALSetMetadataItem( band, "WAVELENGTH", bands[i - 1].wavelength, nullptr );
    if ( bands[i - 1].units )
      GDALSetMetadataItem( band, "WAVELENGTH_UNITS", bands[i - 1].units, nullptr );
    GDALSetMetadataItem( band, "FWHM", bands[i - 1].fwhm, nullptr );
    float pixels[4 * 3];
    for ( int p = 0; p < 4 * 3; ++p )
      pixels[p] = static_cast<float>( p );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 4, 3, pixels, 4, 3, GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALClose( dataset );

  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( path );
  REQUIRE( meta.bands.size() == 4 );

  // Band 1: 0.665 µm → 665 nm; 0.06 µm → 60 nm (before: "0.06 nm").
  CHECK( meta.bands[0].hasWavelength );
  CHECK( meta.bands[0].wavelengthNm == Approx( 665.0 ) );
  CHECK( meta.bands[0].hasFwhm );
  CHECK( meta.bands[0].fwhmNm == Approx( 60.0 ) );

  // Band 2: nm values pass through.
  CHECK( meta.bands[1].wavelengthNm == Approx( 560.0 ) );
  CHECK( meta.bands[1].fwhmNm == Approx( 60.0 ) );

  // Band 3: no unit key — raw numbers, exactly as before.
  CHECK( meta.bands[2].wavelengthNm == Approx( 490.0 ) );
  CHECK( meta.bands[2].fwhmNm == Approx( 50.0 ) );

  // Band 4: unrecognized unit leaves BOTH values unset (no guessing).
  CHECK_FALSE( meta.bands[3].hasWavelength );
  CHECK_FALSE( meta.bands[3].hasFwhm );
}

// ---------------------------------------------------------------------------
// GEO-3 [P2]: the extent used only the (0,0) and (w,h) diagonal corners, so a
// rotated geotransform lost the other two corners (grid_descriptor's
// approximateExtent was already right — same-repo drift).
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-3: a rotated geotransform extent takes all four corners",
           "[swarm][geo-3][metadata]" )
{
  ensureGdal();
  const std::string dir = scratch( "geo3" );
  const std::string path = ( fs::path( dir ) / "rotated.tif" ).string();

  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 4, 2, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  // Rotation terms gt[2]=5, gt[4]=3 — the diagonal-only bbox misses corners.
  double geotransform[6] = { 100.0, 10.0, 5.0, 40.0, 3.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( dataset, geotransform ) == CE_None );
  GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
  REQUIRE( band );
  unsigned char pixels[4 * 2] = { 1, 2, 3, 4, 5, 6, 7, 8 };
  REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 4, 2, pixels, 4, 2, GDT_Byte, 0, 0 ) == CE_None );
  GDALClose( dataset );

  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( path );
  REQUIRE( meta.hasGeotransform );
  REQUIRE( meta.hasExtent );

  // Four corners in map space:
  //   (0,0) → (100,40)   (4,0) → (150,52)
  //   (4,2) → (150,32)   (0,2) → (110,20)
  CHECK( meta.minX == Approx( 100.0 ).margin( 1e-9 ) );
  CHECK( meta.maxX == Approx( 150.0 ).margin( 1e-9 ) );
  CHECK( meta.minY == Approx( 20.0 ).margin( 1e-9 ) ); // diagonal-only said 32
  CHECK( meta.maxY == Approx( 52.0 ).margin( 1e-9 ) ); // diagonal-only said 40
}

// ---------------------------------------------------------------------------
// GEO-4 [P2]: the scatter's hand-built BandInfo omitted dtype, so
// bandSentinelMatches skipped its Float32 narrowing (#874) and a
// float-quantized sentinel matched nothing — NoData pixels were written as
// data.
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-4: the cube scatter keeps the source dtype in the sentinel comparison",
           "[swarm][geo-4][fabric]" )
{
  const std::string dir = scratch( "geo4" );
  const std::string tif = ( fs::path( dir ) / "scene.tif" ).string();
  const std::string vrt = ( fs::path( dir ) / "scene.vrt" ).string();

  // Float32 band whose declared NoData (-9999.9) is NOT float-exact: the
  // stored pixel is -9999.9f = -9999.900390625 in double space. The VRT
  // wrapper is what makes the mismatch observable — GTiff narrows the
  // declared NoData to float at write time, while a VRT preserves the full
  // double, which is exactly the shape #874 guards against.
  sicnu::geo::RasterBandSpec band;
  band.dtype = "Float32";
  band.hasNoData = true;
  band.noDataValue = -9999.9;
  const int size = 32;
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    tif, size, size, { band }, { "GTiff", {}, true } );
  writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
  const std::array<double, 6> gt = { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 };
  writer.setGeotransform( gt );
  std::vector<double> raster( static_cast<std::size_t>( size ) * size, 12.5 );
  raster[0] = -9999.9;                    // (0,0)
  raster[static_cast<std::size_t>( 5 ) * size + 5] = -9999.9; // (5,5)
  writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
  writer.finalize();

  {
    std::ofstream out( vrt, std::ios::binary );
    REQUIRE( out.is_open() );
    out << "<VRTDataset rasterXSize=\"" << size << "\" rasterYSize=\"" << size << "\">\n"
        << "  <GeoTransform>0.0, 1.0, 0.0, 32.0, 0.0, -1.0</GeoTransform>\n"
        << "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n"
        << "    <NoDataValue>-9999.9</NoDataValue>\n"
        << "    <SimpleSource>\n"
        << "      <SourceFilename relativeToVRT=\"0\">" << tif << "</SourceFilename>\n"
        << "      <SourceBand>1</SourceBand>\n"
        << "      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"" << size << "\" ySize=\"" << size << "\"/>\n"
        << "      <DstRect xOff=\"0\" yOff=\"0\" xSize=\"" << size << "\" ySize=\"" << size << "\"/>\n"
        << "    </SimpleSource>\n"
        << "  </VRTRasterBand>\n"
        << "</VRTDataset>\n";
  }

  sicnu::geo::AssetRecord record;
  record.id = "scene";
  record.path = vrt;
  record.hasBbox = true;
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 32.0;
  record.maxY = 32.0;

  sicnu::geo::VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 32.0;
  grid.maxY = 32.0;

  const sicnu::geo::VirtualCube cube = sicnu::geo::VirtualCube::build(
    { record }, grid, sicnu::geo::OverlapPolicy::FirstWins, {}, {} );
  const sicnu::geo::VirtualCubeWindowResult window = cube.readWindow( 0, 0, size, size );
  REQUIRE( window.values.size() == static_cast<std::size_t>( size ) * size );

  CHECK( window.gridNoData == Approx( -9999.9 ).margin( 1e-9 ) );
  // The sentinel pixels must stay HOLES (gridNoData), not the widened
  // -9999.900390625 the dtype-less comparison let through.
  CHECK( window.values[0] == Approx( window.gridNoData ).margin( 1e-9 ) );
  CHECK( window.values[static_cast<std::size_t>( 5 ) * size + 5] ==
         Approx( window.gridNoData ).margin( 1e-9 ) );
  // A valid pixel is untouched.
  CHECK( window.values[static_cast<std::size_t>( 7 ) * size + 7] == Approx( 12.5 ) );
}

// ---------------------------------------------------------------------------
// GEO-8 [P3]: ResourceUri's prefix table drifted from offline_gate's
// kNetworkVsi (/vsiwebhdfs/, /vsiadls_streaming/, /vsioss_streaming/ were
// refused as remote by the gate yet classified as invalid local paths).
// The two tables must name the same network family — forever.
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-8: VSI prefix tables stay locked between classification and the gate",
           "[swarm][geo-8][uri]" )
{
  static const char *const kNetwork[] = {
    "/vsicurl/", "/vsicurl_streaming/", "/vsis3/", "/vsis3_streaming/",
    "/vsigs/", "/vsigs_streaming/", "/vsiaz/", "/vsiaz_streaming/",
    "/vsiadls/", "/vsiadls_streaming/", "/vsihdfs/", "/vsiwebhdfs/",
    "/vsioss/", "/vsioss_streaming/", "/vsiswift/",
  };
  for ( const char *prefix : kNetwork )
  {
    const std::string handle = std::string( prefix ) + "bucket/key.tif";
    const sicnu::geo::ResourceUri uri = sicnu::geo::ResourceUri::parse( handle );
    INFO( prefix );
    CHECK( uri.kind == sicnu::geo::ResourceKind::VsiRemote );
    CHECK( sicnu::geo::offline::isRemoteTarget( handle ) );
  }

  static const char *const kLocal[] = { "/vsizip/", "/vsitar/", "/vsigzip/" };
  for ( const char *prefix : kLocal )
  {
    const std::string handle = std::string( prefix ) + "archive.zip/inner.tif";
    const sicnu::geo::ResourceUri uri = sicnu::geo::ResourceUri::parse( handle );
    INFO( prefix );
    CHECK( uri.kind == sicnu::geo::ResourceKind::VsiVirtual );
    CHECK_FALSE( sicnu::geo::offline::isRemoteTarget( handle ) );
  }

  const sicnu::geo::ResourceUri memory = sicnu::geo::ResourceUri::parse( "/vsimem/scratch.tif" );
  CHECK( memory.kind == sicnu::geo::ResourceKind::InMemory );
  CHECK_FALSE( sicnu::geo::offline::isRemoteTarget( "/vsimem/scratch.tif" ) );

  // Wrapping handler: remote only when the inner target is.
  CHECK( sicnu::geo::offline::isRemoteTarget( "/vsirangecache//vsicurl/https://host/a.tif" ) );
  CHECK_FALSE( sicnu::geo::offline::isRemoteTarget( "/vsirangecache//vsizip/a.zip/b.tif" ) );
}

// ---------------------------------------------------------------------------
// GEO-9 [P3]: the GET search URL rendered bbox with the default 6-significant-
// digit stream precision (104.065758° → "104.066", ~20 m of narrowing);
// the POST body serialized the double exactly.
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-9: GET search URLs render bbox at round-trip precision",
           "[swarm][geo-9][stac]" )
{
  sicnu::geo::StacSearchQuery query;
  query.bbox = { 104.065758, 30.123456, 105.987654, 31.654321 };

  const std::string url =
    sicnu::geo::StacClient::buildSearchUrl( "https://example.com/stac", query );
  INFO( url );
  // The report's headline number renders exactly.
  CHECK( url.find( "104.065758" ) != std::string::npos );

  // Round-trip contract: every corner must parse back to the SAME double.
  // (17 significant digits is not always the shortest spelling — 30.123456
  // renders as "30.123456000000001" — so assert on the round trip, not on a
  // literal substring.)
  const std::size_t bboxAt = url.find( "bbox=" );
  REQUIRE( bboxAt != std::string::npos );
  std::string bboxParam = url.substr( bboxAt + 5 );
  if ( const std::size_t amp = bboxParam.find( '&' ); amp != std::string::npos )
    bboxParam = bboxParam.substr( 0, amp );
  std::vector<double> parsed;
  std::size_t start = 0;
  while ( start < bboxParam.size() )
  {
    const std::size_t comma = bboxParam.find( "%2C", start );
    const std::string piece =
      bboxParam.substr( start, comma == std::string::npos ? std::string::npos : comma - start );
    if ( !piece.empty() )
      parsed.push_back( std::stod( piece ) );
    if ( comma == std::string::npos )
      break;
    start = comma + 3;
  }
  REQUIRE( parsed.size() == query.bbox.size() );
  for ( std::size_t i = 0; i < parsed.size(); ++i )
    CHECK( parsed[i] == query.bbox[i] ); // bit-exact round-trip

  // Whole-number corners keep their compact spelling (existing callers).
  sicnu::geo::StacSearchQuery integral;
  integral.bbox = { 10.0, 40.0, 11.0, 41.0 };
  const std::string integralUrl =
    sicnu::geo::StacClient::buildSearchUrl( "https://example.com/stac", integral );
  CHECK( integralUrl.find( "bbox=10%2C40%2C11%2C41" ) != std::string::npos );
}

// ---------------------------------------------------------------------------
// GEO-11 [P3]: the writer accepted Int64/UInt64 dtypes, but the writeWindow
// range gate has no case for them (GDAL saturates silently) and RasterReader
// refuses to read them back. Refuse at create time, symmetric with the reader.
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-11: RasterWriter refuses 64-bit integer dtypes at create",
           "[swarm][geo-11][raster]" )
{
  const std::string dir = scratch( "geo11" );
  sicnu::geo::RasterBandSpec band;
  band.dtype = "Int64";
  const std::string path = ( fs::path( dir ) / "i64.tif" ).string();

  bool threw = false;
  try
  {
    sicnu::geo::RasterWriter::create( path, 4, 4, { band }, { "GTiff", {}, true } );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    threw = true;
    CHECK( error.code() == sicnu::geo::ErrorCode::Unsupported );
    CHECK( error.details()["dtype"].asString() == "Int64" );
  }
  CHECK( threw );
  CHECK_FALSE( fs::exists( path ) ); // refused before staging

  band.dtype = "UInt64";
  CHECK_THROWS_AS( sicnu::geo::RasterWriter::create( ( fs::path( dir ) / "u64.tif" ).string(),
                                                     4, 4, { band }, { "GTiff", {}, true } ),
                   sicnu::geo::GeoError );

  // Control: the widest supported integer still creates.
  band.dtype = "Int32";
  const std::string ok = ( fs::path( dir ) / "i32.tif" ).string();
  sicnu::geo::RasterWriter writer =
    sicnu::geo::RasterWriter::create( ok, 4, 4, { band }, { "GTiff", {}, true } );
  writer.cancel();

  // Reader symmetry: an Int64 raster opens, but the reader refuses it at
  // read time ("exceeds exact double representation") — so the writer must
  // never produce one.
#if SICNU_GDAL_INT64_DATATYPES
  ensureGdal();
  const std::string i64Raster = ( fs::path( dir ) / "source_i64.tif" ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, i64Raster.c_str(), 4, 4, 1, GDT_Int64, nullptr );
  REQUIRE( dataset );
  GDALClose( dataset );
  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( i64Raster );
  CHECK_THROWS_AS( reader.readWindow( { 1 }, { 0, 0, 4, 4 } ), sicnu::geo::GeoError );
#endif
}

// ---------------------------------------------------------------------------
// crs_policy (存疑项): "EPSG:3857-" resolved to EPSG:3857 via atoi truncation
// (PROJ itself stops at the first non-digit, so the generic user-input path
// cannot be the guard). Malformed authids are now refused.
// ---------------------------------------------------------------------------

TEST_CASE( "crs_policy: malformed EPSG authids are refused, never truncated", "[swarm][crs]" )
{
  static const char *const kMalformed[] = {
    "EPSG:3857-", "EPSG:3857x", "EPSG:3857 ", "EPSG:3857.0", "EPSG:38.57",
    "EPSG:+3857", "EPSG:3857-1", "EPSG:0", "EPSG:999999999999", "EPSG:notanumber",
  };
  for ( const char *authid : kMalformed )
  {
    INFO( authid );
    bool threw = false;
    try
    {
      ( void )sicnu::geo::Crs::fromAuthid( authid );
    }
    catch ( const sicnu::geo::GeoError & )
    {
      threw = true;
    }
    CHECK( threw );
  }

  // Clean spellings keep resolving (including leading zeros).
  const sicnu::geo::Crs wgs84 = sicnu::geo::Crs::fromAuthid( "EPSG:4326" );
  CHECK( wgs84.authid() == "EPSG:4326" );
  const sicnu::geo::Crs padded = sicnu::geo::Crs::fromAuthid( "EPSG:0004326" );
  CHECK( padded.authid() == "EPSG:4326" );
}

// ---------------------------------------------------------------------------
// GEO-6 [P2]: product sidecars opened through a narrow std::ifstream, which
// on Windows cannot address non-ANSI (Chinese) directories. The wide-char
// path is the contract; on POSIX both spellings work, so this case anchors
// the behavior rather than a host-dependent failure.
// ---------------------------------------------------------------------------

TEST_CASE( "GEO-6: product sidecars resolve through the portable path", "[swarm][geo-6][products]" )
{
  const std::string dir = scratch( "geo6" );
  // Build and address the directory through the portable helpers: on Windows
  // a narrow fs::path is ACP-encoded and cannot represent these characters at
  // all — which is the whole point of the fix. The name is spelled as
  // explicit UTF-8 bytes so no compiler's source-charset assumption matters.
  // ("中文产品_LT05")
  const std::string nonAnsiName = "\xE4\xB8\xAD\xE6\x96\x87\xE4\xBA\xA7\xE5\x93\x81_LT05";
  const fs::path productDir = fs::path( dir ) / sicnu::portable::pathFromUtf8( nonAnsiName );
  std::error_code ec;
  fs::create_directories( productDir, ec );
  REQUIRE( !ec );

  const fs::path mtl = productDir / "LT05_MTL.txt";
  {
    std::ofstream out( mtl, std::ios::binary );
    REQUIRE( out.is_open() );
    out << "GROUP = L1_METADATA_FILE\n"
        << "  LANDSAT_SCENE_ID = LT05_L1TP_119038_19881222_20170220_01_T1\n"
        << "  SPACECRAFT_ID = LANDSAT_5\n"
        << "END_GROUP = L1_METADATA_FILE\n";
  }

  const std::map<std::string, std::string> keys =
    sicnu::geo::parseLandsatMtlKeys( sicnu::portable::pathToUtf8( mtl ) );
  REQUIRE_FALSE( keys.empty() );
  const auto scene = keys.find( "LANDSAT_SCENE_ID" );
  REQUIRE( scene != keys.end() );
  CHECK( scene->second == "LT05_L1TP_119038_19881222_20170220_01_T1" );
}
