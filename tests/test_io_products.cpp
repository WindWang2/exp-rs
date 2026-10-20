/***************************************************************************
  tests/test_io_products.cpp — product metadata adapters suite (Phase 9).
  Synthetic Landsat MTL / Sentinel-2 SAFE / Sentinel-1 manifest / MODIS /
  generic GeoTIFF fixtures, generated at runtime.
 ***************************************************************************/

#include "geospatial/products/cn_product_metadata.h"
#include "geospatial/products/product_adapters.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_products" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}

void writeText( const std::string &path, const std::string &content )
{
  std::ofstream out( path );
  REQUIRE( out.is_open() );
  out << content;
}
} // namespace

TEST_CASE( "Landsat MTL parsing extracts sensor, dates, cloud and scaling", "[io][products][landsat]" )
{
  const std::string dir = scratch( "landsat" );
  const std::string mtl = ( fs::path( dir ) / "LC08_L2SP_126052_20260801_20260809_02_T1_MTL.txt" ).string();
  writeText( mtl, R"mtl(GROUP = LANDSAT_METADATA_FILE
  GROUP = PRODUCT_CONTENTS
    LANDSAT_PRODUCT_ID = "LC08_L2SP_126052_20260801_20260809_02_T1"
  END_GROUP = PRODUCT_CONTENTS
  GROUP = LEVEL2_SURFACE_REFLECTANCE
    REFLECTANCE_MULT_BAND_1 = 2.75e-05
    CLOUD_COVER = 7.0
  END_GROUP = LEVEL2_SURFACE_REFLECTANCE
  GROUP = IMAGE_ATTRIBUTES
    SPACECRAFT_ID = "LANDSAT_8"
    SENSOR_ID = "OLI_TIRS"
    DATE_ACQUIRED = 2026-08-01
    SCENE_CENTER_TIME = "02:45:12.1234560Z"
    CLOUD_COVER = 7.0
    MAP_PROJECTION = "UTM"
    UTM_ZONE = 48
  END_GROUP = IMAGE_ATTRIBUTES
END_GROUP = LANDSAT_METADATA_FILE
)mtl" );

  const sicnu::geo::ProductMetadata product = sicnu::geo::readProductMetadataAuto( mtl );
  CHECK( product.productId == "LC08_L2SP_126052_20260801_20260809_02_T1" );
  CHECK( product.platform == "LANDSAT_8" );
  CHECK( product.sensor == "OLI_TIRS" );
  CHECK( product.acquisitionTime == "2026-08-01T02:45:12.1234560Z" );
  CHECK( product.hasCloudCover );
  CHECK( product.cloudCover == Approx( 7.0 ) );
  // Collection-2 Level-2 scene: the LEVEL2_SURFACE_REFLECTANCE group makes
  // this SURFACE reflectance even though REFLECTANCE_MULT_BAND_1 is present
  // (the regression that mislabeled L2 products as TOA).
  CHECK( product.radiometricState == "surface_reflectance" );
  CHECK( product.modality == "optical" );

  // Canonical enrichment respects existing values.
  sicnu::geo::RasterMetadata canonical;
  canonical.sensor = "ALREADY_SET";
  enrichWithProductMetadata( canonical, product );
  CHECK( canonical.sensor == "ALREADY_SET" );
  CHECK( canonical.platform == "LANDSAT_8" );
  CHECK( canonical.hasCloudCover );

  CHECK( sicnu::geo::productBandRole( sicnu::geo::ProductKind::LandsatMtl, "B4" ) == "red" );
  CHECK( sicnu::geo::productBandRole( sicnu::geo::ProductKind::LandsatMtl, "B10" ) == "thermal" );
  double wavelength = 0.0;
  CHECK( sicnu::geo::productBandWavelengthNm( sicnu::geo::ProductKind::LandsatMtl, "B5", wavelength ) );
  CHECK( wavelength == Approx( 865.0 ) );
}

TEST_CASE( "Sentinel-2 SAFE product XML extracts processing level and cloud", "[io][products][sentinel2]" )
{
  const std::string dir = scratch( "sentinel2" );
  const fs::path safe = fs::path( dir ) / "S2B_MSIL2A_20260610T030529_N0509_R032_T48RUS_20260610T070111.SAFE";
  fs::create_directories( safe );
  writeText( ( safe / "MTD_MSIL2A.xml" ).string(), R"xml(<?xml version="1.0"?>
<n1:Level-2A_Tile_ID xmlns:n1="https://psd-14.sentinel2.eo.esa.int/PSD/S2_PDI_Level-2A_Tile_Metadata.xsd">
  <Product_Info>
    PRODUCT_START_TIME="2026-06-10T03:05:29.024Z"
  </Product_Info>
  <Cloud_Coverage_Assessment>23.4</Cloud_Coverage_Assessment>
</n1:Level-2A_Tile_ID>)xml" );

  const sicnu::geo::ProductMetadata product = sicnu::geo::readProductMetadataAuto( safe.string() );
  CHECK( product.platform == "SENTINEL-2B" );
  CHECK( product.processingLevel == "Level-2A" );
  CHECK( product.radiometricState == "surface_reflectance" );
  CHECK( product.numericScale == Approx( 10000.0 ) );
  CHECK( product.hasCloudCover );
  CHECK( product.cloudCover == Approx( 23.4 ) );
  CHECK( sicnu::geo::productBandRole( sicnu::geo::ProductKind::Sentinel2Safe, "B8A" ) == "narrow_nir" );
  CHECK( sicnu::geo::productBandRole( sicnu::geo::ProductKind::Sentinel2Safe, "B12" ) == "swir2" );
}

TEST_CASE( "Sentinel-1 manifest extracts polarizations and orbit", "[io][products][sentinel1]" )
{
  const std::string dir = scratch( "sentinel1" );
  const fs::path safe = fs::path( dir ) / "S1A_IW_GRDH_1SDV_20260714T220341_20260714T220406_059742_7667E_9A67.SAFE";
  fs::create_directories( safe );
  writeText( ( safe / "manifest.safe" ).string(), R"xml(<?xml version="1.0" encoding="UTF-8"?>
<xfdu:XFDU xmlns:s1sarl1="urn:esa:psd:grd" xmlns:safe="http://www.esa.int/safe/sentinel-1.0">
  <metadataSection>
    <metadataObject><xdata><safe:platform>SENTINEL-1A</safe:platform></xdata></metadataObject>
    <metadataObject><xdata><s1sarl1:instrumentMode>IW</s1sarl1:instrumentMode></xdata></metadataObject>
    <metadataObject><xdata><s1sarl1:producttype>GRD</s1sarl1:producttype></xdata></metadataObject>
    <metadataObject><xdata><safe:pass>ASCENDING</safe:pass></xdata></metadataObject>
    <metadataObject><xdata><s1sarl1:polarisation>VV</s1sarl1:polarisation></xdata></metadataObject>
    <metadataObject><xdata><s1sarl1:polarisation>VH</s1sarl1:polarisation></xdata></metadataObject>
  </metadataSection>
</xfdu:XFDU>)xml" );

  const sicnu::geo::ProductMetadata product = sicnu::geo::readProductMetadataAuto( safe.string() );
  CHECK( product.platform == "SENTINEL-1A" );
  CHECK( product.instrumentMode == "IW" );
  CHECK( product.processingLevel == "GRD" );
  REQUIRE( product.polarizations.size() == 2 );
  CHECK( product.polarizations[0] == "VV" );
  CHECK( product.orbitDirection == "ASCENDING" );
  CHECK( product.modality == "sar" );
}

TEST_CASE( "generic rasters take only declared metadata; unknown adapters fail structured", "[io][products]" )
{
  CHECK_THROWS_AS( sicnu::geo::readProductMetadataAuto( "/any/plain.tif" ), sicnu::geo::GeoError );
  CHECK( sicnu::geo::detectProductKind( "/any/plain.tif" ) == sicnu::geo::ProductKind::GenericRaster );
  CHECK( sicnu::geo::productBandRole( sicnu::geo::ProductKind::GenericRaster, "B4" ).empty() );

  // Malformed XML in an S1 manifest is a structured parse error, not a crash.
  const std::string dir = scratch( "bad_s1" );
  const fs::path safe = fs::path( dir ) / "S1B_IW_GRDH_1SDV_BROKEN.SAFE";
  fs::create_directories( safe );
  writeText( ( safe / "manifest.safe" ).string(), "<unclosed>" );
  CHECK_THROWS_AS( sicnu::geo::readProductMetadataAuto( safe.string() ), sicnu::geo::GeoError );
}

TEST_CASE( "an oversized Sentinel-1 manifest is refused typed, never slurped (#1637-3)",
           "[io][products][bounds]" )
{
  // The generic product adapters stream manifest.safe with the same
  // unbounded `buffer << in.rdbuf()` the CN reader had — the bound must
  // hold on this path too (R2-P3 twin closure).
  const std::string dir = scratch( "s1_oversized" );
  const fs::path safe = fs::path( dir ) / "S1A_IW_GRDH_1SDV_HUGE.SAFE";
  fs::create_directories( safe );
  const std::string pad( 5 * 1024 * 1024, 'x' );
  writeText( ( safe / "manifest.safe" ).string(),
             "<?xml version=\"1.0\"?>\n<safe: oasis=" + pad + "/>" );
  try
  {
    ( void )sicnu::geo::readProductMetadataAuto( safe.string() );
    FAIL( "a >4 MiB manifest.safe must be refused, not parsed" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::InvalidMetadata );
  }
}

TEST_CASE( "an oversized CN metadata sidecar is refused typed, never slurped (#1637-3)",
           "[io][products][cn][bounds]" )
{
  // The CN metadata reader used to stream the WHOLE sidecar into memory via
  // `buffer << in.rdbuf()` with no byte bound — a misidentified or hostile
  // multi-MiB "MTD_*.xml" became an unbounded allocation before the parser
  // could refuse it. Real CRESDA/PMI sidecars are a few KB; anything past
  // the bound must fail as InvalidMetadata BEFORE the read.
  const std::string dir = scratch( "cn_oversized" );
  const std::string base = ( fs::path( dir ) / "GF3_QPS_E113.9_N22.2_20221121_L1A00000432156-HH.tiff" ).string();
  // Presence placeholder for the image half; the metadata path never opens it.
  writeText( base, "x" );
  // Valid legacy <MetaInfo> sidecar padded past 4 MiB with a well-formed
  // XML comment, so the refusal is about SIZE, not parseability.
  const std::string pad( 5 * 1024 * 1024, 'x' );
  writeText( ( fs::path( dir ) / "GF3_QPS_E113.9_N22.2_20221121_L1A00000432156-HH.xml" ).string(),
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<MetaInfo>\n"
             "  <ProductID>31890</ProductID>\n  <SatelliteID>GF3</SatelliteID>\n"
             "  <SensorID>SAR</SensorID>\n  <ModeID>QPS</ModeID>\n"
             "  <ProductLevel>L1A</ProductLevel>\n"
             "  <ReceiveDate>2022-11-21</ReceiveDate>\n"
             "  <ReceiveTime>10:23:45</ReceiveTime>\n"
             "  <OrbitID>08871</OrbitID>\n"
             "<!--" + pad + "-->\n</MetaInfo>\n" );

  const sicnu::geo::CnProductIdentity identity =
    sicnu::geo::cnIdentifyProduct( base );
  REQUIRE( identity.recognized );
  REQUIRE( identity.supported );
  try
  {
    ( void )sicnu::geo::readCnProductMetadata( base, identity );
    FAIL( "a >4 MiB CN metadata sidecar must be refused, not parsed" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::InvalidMetadata );
    CHECK( std::string( error.what() ).find( "metadata" ) != std::string::npos );
  }
}
