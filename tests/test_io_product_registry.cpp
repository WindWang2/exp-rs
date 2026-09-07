/***************************************************************************
  tests/test_io_product_registry.cpp — Foundation 5.0: product adapter
  registry, constituent enumeration and completeness verdicts (ADR 0137).
  Fixtures are synthetic and generated in-test (repo convention).
 ***************************************************************************/

#include "geospatial/products/product_registry.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace sicnu::geo;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_registry" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

void writeText( const std::string &path, const std::string &content )
{
  std::ofstream out( fs::u8path( path ), std::ios::binary );
  out << content;
}

void writeBytes( const std::string &path, const std::string &content = "fixture" )
{
  writeText( path, content );
}

/// Minimal Landsat Collection-2 style MTL with the declared file inventory.
std::string landsatMtl( const std::string &productId, int bandCount )
{
  std::string mtl = "GROUP = LANDSAT_METADATA_FILE\n";
  mtl += "  GROUP = PRODUCT_CONTENTS\n";
  mtl += "    LANDSAT_PRODUCT_ID = \"" + productId + "\"\n";
  mtl += "    SPACECRAFT_ID = \"LANDSAT_8\"\n";
  mtl += "    SENSOR_ID = \"OLI_TIRS\"\n";
  mtl += "    COLLECTION_NUMBER = \"02\"\n";
  mtl += "    COLLECTION_CATEGORY = \"L2SP\"\n";
  for ( int band = 1; band <= bandCount; ++band )
  {
    char name[32];
    std::snprintf( name, sizeof( name ), "B%02d", band == 4 ? 4 : band );
    mtl += std::string( "    FILE_NAME_BAND_" ) + std::to_string( band ) + " = \"" + productId + "_" +
           ( band == 4 ? "B4" : ( "B" + std::to_string( band ) ) ) + ".TIF\"\n";
  }
  mtl += "    FILE_NAME_QA_PIXEL = \"" + productId + "_QA_PIXEL.TIF\"\n";
  mtl += "    FILE_NAME_ST_B10 = \"" + productId + "_ST_B10.TIF\"\n";
  mtl += "  END_GROUP = PRODUCT_CONTENTS\n";
  mtl += "END_GROUP = LANDSAT_METADATA_FILE\n";
  return mtl;
}
} // namespace

TEST_CASE( "Landsat MTL scenes enumerate declared constituents with roles",
           "[io][products][registry][landsat]" )
{
  const std::string root = scratch( "landsat" );
  const std::string productId = "LC08_L2SP_042034_20260601_02_T1";
  writeText( root + "/" + productId + "_MTL.txt", landsatMtl( productId, 4 ) );
  for ( const std::string &band : { "B1", "B2", "B3", "B4" } )
    writeBytes( root + "/" + productId + "_" + band + ".TIF" );
  writeBytes( root + "/" + productId + "_QA_PIXEL.TIF" );
  writeBytes( root + "/" + productId + "_ST_B10.TIF" );

  ProductAdapter *adapter = ProductAdapterRegistry::instance().adapterFor( root );
  REQUIRE( adapter != nullptr );
  REQUIRE( adapter->kind() == ProductKind::LandsatMtl );

  const ProductAssets assets = adapter->enumerate( root );
  INFO( "assets json: " << assets.toJson() );
  CHECK( assets.completeness == ProductCompleteness::Complete );
  CHECK( assets.missingConstituents.empty() );
  CHECK( assets.productId == productId );
  CHECK( assets.metadata.platform == "LANDSAT_8" );
  CHECK( assets.metadata.sensor == "OLI_TIRS" );

  int measurements = 0;
  int masks = 0;
  bool redRoleFound = false;
  bool thermalFound = false;
  for ( const ProductAsset &asset : assets.assets )
  {
    if ( asset.role == "measurement" )
    {
      ++measurements;
      if ( asset.nativeBandName == "B4" )
      {
        redRoleFound = asset.bandRole == "red";
        CHECK( asset.hasWavelength );
      }
      if ( asset.bandRole == "thermal" )
        thermalFound = true;
    }
    if ( asset.role == "mask" )
    {
      ++masks;
      CHECK( asset.bandRole == "qa" );
    }
  }
  CHECK( measurements == 5 ); // B1..B3 + B4 + ST_B10
  CHECK( masks == 1 );
  CHECK( redRoleFound );
  CHECK( thermalFound );
}

TEST_CASE( "a Landsat scene missing a declared core band is PartialReadable",
           "[io][products][registry][landsat][fidelity]" )
{
  const std::string root = scratch( "landsat_partial" );
  const std::string productId = "LC08_L2SP_042034_20260601_02_T1";
  writeText( root + "/" + productId + "_MTL.txt", landsatMtl( productId, 4 ) );
  // Only two of the four declared bands exist.
  writeBytes( root + "/" + productId + "_B1.TIF" );
  writeBytes( root + "/" + productId + "_B2.TIF" );

  const ProductAssets assets = ProductAdapterRegistry::instance().describe( root );
  CHECK( assets.completeness == ProductCompleteness::PartialReadable );
  REQUIRE( assets.missingConstituents.size() >= 2 );
  bool missingB4 = false;
  bool missingQa = false;
  for ( const std::string &missing : assets.missingConstituents )
  {
    if ( missing.find( "_B4.TIF" ) != std::string::npos )
      missingB4 = true;
    if ( missing.find( "_QA_PIXEL" ) != std::string::npos )
      missingQa = true;
  }
  CHECK( missingB4 );
  CHECK( missingQa );
}

TEST_CASE( "an unknown Landsat collection number is UnsupportedVersion, not guessed",
           "[io][products][registry][landsat][version]" )
{
  const std::string root = scratch( "landsat_v99" );
  std::string mtl = landsatMtl( "LC08_L2SP_042034_20260601_99_T1", 1 );
  mtl.replace( mtl.find( "COLLECTION_NUMBER = \"02\"" ), 24, "COLLECTION_NUMBER = \"99\"" );
  writeText( root + "/LC08_L2SP_042034_20260601_99_T1_MTL.txt", mtl );

  const ProductAssets assets = ProductAdapterRegistry::instance().describe( root );
  CHECK( assets.completeness == ProductCompleteness::UnsupportedVersion );
  CHECK( assets.notes["collection_number"].asString() == "99" );
}

TEST_CASE( "Sentinel-2 SAFE granules keep resolution groups distinct",
           "[io][products][registry][s2]" )
{
  const std::string root = scratch( "s2" );
  const std::string safe =
    root + "/S2A_MSIL2A_20260610T100031_N0400_R122_T33UUU_20260610T120000.SAFE";
  fs::create_directories( fs::u8path( safe + "/GRANULE/L2A_T33UUU_A000000_20260610T100030/IMG_DATA/R10m" ) );
  fs::create_directories( fs::u8path( safe + "/GRANULE/L2A_T33UUU_A000000_20260610T100030/IMG_DATA/R20m" ) );
  writeText( safe + "/manifest.safe", "<filetype>xml</filetype>" );
  writeText( safe + "/MTD_MSIL2A.xml", "<n1:Level-2A_Tile_ID></n1:Level-2A_Tile_ID>" );
  writeBytes( safe + "/GRANULE/L2A_T33UUU_A000000_20260610T100030/IMG_DATA/R10m/T33UUU_20260610T100031_B02_10m.tif" );
  writeBytes( safe + "/GRANULE/L2A_T33UUU_A000000_20260610T100030/IMG_DATA/R10m/T33UUU_20260610T100031_B08_10m.tif" );
  writeBytes( safe + "/GRANULE/L2A_T33UUU_A000000_20260610T100030/IMG_DATA/R20m/T33UUU_20260610T100031_B04_20m.tif" );
  writeBytes( safe + "/GRANULE/L2A_T33UUU_A000000_20260610T100030/IMG_DATA/R20m/T33UUU_20260610T100031_SCL_20m.tif" );

  const ProductAssets assets = ProductAdapterRegistry::instance().describe( safe );
  CHECK( assets.completeness == ProductCompleteness::Complete );
  CHECK( assets.kind == ProductKind::Sentinel2Safe );

  bool b02TenMeter = false;
  bool b04TwentyMeter = false;
  int sclMasks = 0;
  for ( const ProductAsset &asset : assets.assets )
  {
    if ( asset.nativeBandName == "B02" )
    {
      b02TenMeter = asset.hasResolution && asset.resolutionMeters == 10.0;
      CHECK( asset.bandRole == "blue" );
    }
    if ( asset.nativeBandName == "B04" )
    {
      b04TwentyMeter = asset.hasResolution && asset.resolutionMeters == 20.0;
      CHECK( asset.bandRole == "red" );
    }
    if ( asset.nativeBandName == "SCL" && asset.role == "mask" )
      ++sclMasks;
  }
  // Different grids stay distinct entries — never merged or resampled.
  CHECK( b02TenMeter );
  CHECK( b04TwentyMeter );
  CHECK( sclMasks == 1 );
}

TEST_CASE( "Sentinel-1 SAFE enumerates measurements, annotations and calibration",
           "[io][products][registry][s1]" )
{
  const std::string root = scratch( "s1" );
  const std::string safe = root + "/S1B_IW_GRDH_1SDV_20260610T051234_20260610T051300_000000_000000_0000.SAFE";
  fs::create_directories( fs::u8path( safe + "/measurement" ) );
  fs::create_directories( fs::u8path( safe + "/annotation" ) );
  fs::create_directories( fs::u8path( safe + "/annotation/calibration" ) );
  writeText( safe + "/manifest.safe",
             "<urn:isd><safe:platform><safe:name>SENTINEL-1B</safe:name>"
             "<safe:familyName>SAR</safe:familyName></safe:platform>"
             "<s1sarl1:payload><s1sarl1:instrumentMode>IW</s1sarl1:instrumentMode>"
             "<s1sarl1:productType>GRD</s1sarl1:productType>"
             "<s1sarl1:polarisation>VV</s1sarl1:polarisation>"
             "<s1sarl1:polarisation>VH</s1sarl1:polarisation>"
             "<s1sarl1:orbitDirection>ASCENDING</s1sarl1:orbitDirection>"
             "</s1sarl1:payload></urn:isd>" );
  writeBytes( safe + "/measurement/s1b-iw-grd-vv-20260610t051234-20260610t051300-000000-000000-001.tiff" );
  writeBytes( safe + "/measurement/s1b-iw-grd-vh-20260610t051234-20260610t051300-000000-000000-002.tiff" );
  writeText( safe + "/annotation/s1b-iw-grd-vv-20260610.xml", "<product></product>" );
  writeText( safe + "/annotation/calibration/calibration-vv.xml", "<calibration></calibration>" );

  const ProductAssets assets = ProductAdapterRegistry::instance().describe( safe );
  CHECK( assets.completeness == ProductCompleteness::Complete );
  CHECK( assets.kind == ProductKind::Sentinel1Safe );

  int measurements = 0;
  int annotations = 0;
  int calibration = 0;
  bool vvFound = false;
  for ( const ProductAsset &asset : assets.assets )
  {
    if ( asset.role == "measurement" )
    {
      ++measurements;
      if ( asset.nativeBandName == "vv" )
        vvFound = true;
    }
    if ( asset.role == "annotation" )
      ++annotations;
    if ( asset.role == "metadata" && asset.nativeBandName.find( "calibration" ) != std::string::npos )
      ++calibration;
  }
  CHECK( measurements == 2 );
  CHECK( annotations == 1 );
  CHECK( calibration == 1 );
  CHECK( vvFound );
  CHECK( !assets.metadata.polarizations.empty() );
}

TEST_CASE( "a Sentinel-1 product without measurements is Invalid, without annotations PartialReadable",
           "[io][products][registry][s1][fidelity]" )
{
  const std::string root = scratch( "s1_broken" );
  const std::string safe = root + "/S1A_IW_GRDH_1SDV_20260610T051234_20260610T051300_000000_000000_0000.SAFE";
  fs::create_directories( fs::u8path( safe + "/measurement" ) );
  fs::create_directories( fs::u8path( safe + "/annotation" ) );
  writeText( safe + "/manifest.safe", "<safe:platform></safe:platform>" );

  const ProductAssets noMeasurement = ProductAdapterRegistry::instance().describe( safe );
  CHECK( noMeasurement.completeness == ProductCompleteness::Invalid );
  bool missingMeasurement = false;
  for ( const std::string &missing : noMeasurement.missingConstituents )
  {
    if ( missing.find( "measurement" ) != std::string::npos )
      missingMeasurement = true;
  }
  CHECK( missingMeasurement );

  writeText( safe + "/annotation/s1a-iw-grd-vv-20260610.xml", "<product></product>" );
  const ProductAssets noMeasurementStill = ProductAdapterRegistry::instance().describe( safe );
  CHECK( noMeasurementStill.completeness == ProductCompleteness::Invalid );

  // Now add the measurement but remove the annotation content → PartialReadable
  const std::string partial = scratch( "s1_partial" );
  const std::string safe2 = partial + "/S1A_IW_GRDH_1SDV_20260610T051234_20260610T051300_000000_000000_0000.SAFE";
  fs::create_directories( fs::u8path( safe2 + "/measurement" ) );
  writeText( safe2 + "/manifest.safe", "<safe:platform></safe:platform>" );
  writeBytes( safe2 + "/measurement/s1a-iw-grd-vv-20260610t051234-000000-001.tiff" );

  const ProductAssets assets = ProductAdapterRegistry::instance().describe( safe2 );
  CHECK( assets.completeness == ProductCompleteness::PartialReadable );
}

TEST_CASE( "generic rasters always enumerate through the fallback adapter",
           "[io][products][registry][generic]" )
{
  const std::string root = scratch( "generic" );
  // A real (tiny) GeoTIFF so GDAL identify succeeds — RasterWriter builds it.
  RasterWriter writer = RasterWriter::create( root + "/plain.tif", 8, 8, { RasterBandSpec {} } );
  writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
  std::vector<double> values( 64, 1.0 );
  writer.writeWindow( 1, { 0, 0, 8, 8 }, values.data() );
  writer.finalize();

  const ProductAssets assets = ProductAdapterRegistry::instance().describe( root + "/plain.tif" );
  CHECK( assets.completeness == ProductCompleteness::Complete );
  CHECK( assets.kind == ProductKind::GenericRaster );
  REQUIRE( assets.assets.size() == 1 );
  CHECK( assets.assets.front().role == "measurement" );

  // A text file is NOT a raster — the fallback reports Invalid (never a fake
  // "usable product" verdict).
  writeText( root + "/notes.txt", "not a dataset" );
  const ProductAssets notRaster = ProductAdapterRegistry::instance().describe( root + "/notes.txt" );
  CHECK( notRaster.completeness == ProductCompleteness::Invalid );
}

TEST_CASE( "completeness verdict names are stable", "[io][products][registry]" )
{
  CHECK( std::string( productCompletenessName( ProductCompleteness::Complete ) ) == "complete" );
  CHECK( std::string( productCompletenessName( ProductCompleteness::PartialReadable ) ) == "partial_readable" );
  CHECK( std::string( productCompletenessName( ProductCompleteness::Invalid ) ) == "invalid" );
  CHECK( std::string( productCompletenessName( ProductCompleteness::UnsupportedVersion ) ) == "unsupported_version" );
}

#include "geospatial/raster/raster_writer.h"
