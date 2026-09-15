// test_cn_product_fixtures.cpp — ADR 0159 CN product fixture corpus
// (tests/fixtures/cn_products/): sidecar XML fixtures + golden metadata.
//
// The measurement TIFFs are synthesized at runtime beside a copied fixture
// (no real imagery is committed). Golden files are the independent oracle:
// hand-written literals, never derived from the parser under test. The
// manifest doubles as a drift gate — the directory must contain exactly the
// declared file set (stray fixtures fail, committed fixtures must exist).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <json/json.h>

#include "geospatial/products/cn_product_metadata.h"
#include "geospatial/products/product_adapters.h"
#include "geospatial/products/product_registry.h"

#include <cmath>

using namespace sicnu::geo;
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

char appArgv0[] = "test_cn_product_fixtures";
int appArgc = 1;
char *appArgv[] = {appArgv0, nullptr};

void ensureAppShim()
{
  if ( !QCoreApplication::instance() )
    new QCoreApplication( appArgc, appArgv );
}

namespace
{

const std::string kFixtureRoot = std::string( CMAKE_SOURCE_DIR ) + "/tests/fixtures/cn_products";

Json::Value readJson( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  REQUIRE( in.is_open() );
  Json::Value json;
  Json::CharReaderBuilder builder;
  std::string errors;
  REQUIRE( Json::parseFromStream( builder, in, &json, &errors ) );
  return json;
}

std::string fileStem( const std::string &fileName )
{
  const std::size_t dot = fileName.rfind( '.' );
  return dot == std::string::npos ? fileName : fileName.substr( 0, dot );
}

void copyFile( const QString &from, const QString &to )
{
  REQUIRE( QFile::copy( from, to ) );
}

void writeStackTiff( const QString &path, int bands )
{
  static bool gdalReady = false;
  if ( !gdalReady ) {
    GDALAllRegister();
    gdalReady = true;
  }
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  const std::array<double, 6> gt = { 500000, 8.0, 0, 4400000, 0, -8.0 };
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 4, 4, bands, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  GDALSetGeoTransform( ds, gt.data() );
  std::vector<float> line( 4, 1.0f );
  for ( int b = 0; b < bands; ++b ) {
    GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
    for ( int row = 0; row < 4; ++row )
      REQUIRE( GDALRasterIO( band, GF_Write, 0, row, 4, 1, line.data(), 4, 1, GDT_Float32, 0, 0 ) ==
               CE_None );
  }
  GDALClose( ds );
}

/// Fixture stem (e.g. "gf1_pms_valid") → the product identity the golden
/// was written for: the temp image must carry a name the identity layer
/// recognizes. Mapping lives here so fixture files stay name-neutral.
struct ProductShape
{
  const char *fixtureStem;
  const char *imageBase;
};

const ProductShape kShapes[] = {
  { "gf1_pms_valid", "GF1_PMS_E113.5_N23.5_20230512_L1A0123456789-MSS1" },
  { "gf3_sar_valid", "GF3_QPS_E113.9_N22.2_20221121_L1A00000012345-HH" },
  { "gf5_ahsi_valid", "GF5_AHSI_E113.5_N31.5_20230512_L1A0123456789" },
  { "zy1_02d_pms_valid", "ZY1_02D_PMS_E113.0_N23.0_20210501_L1A0000003-MSS" },
  { "cbers4_mux_inpe", "CBERS4_MUX_20230415_0364_075_L1" },
};

std::string imageBaseFor( const std::string &fixtureStem )
{
  for ( const ProductShape &shape : kShapes )
  {
    if ( fixtureStem == shape.fixtureStem )
      return shape.imageBase;
  }
  return std::string();
}

} // namespace

TEST_CASE( "cn_fixtures: manifest matches the committed file set (drift gate)",
           "[cn11][fixtures][drift]" )
{
  const Json::Value manifest = readJson( kFixtureRoot + "/manifest.json" );

  std::set<std::string> expected;
  expected.insert( "manifest.json" );
  for ( const Json::Value &entry : manifest["golden"] )
  {
    expected.insert( entry.asString() );
    expected.insert( "golden/" + fileStem( entry.asString() ) + ".json" );
  }
  for ( const Json::Value &entry : manifest["corrupt"] )
    expected.insert( entry.asString() );

  std::set<std::string> actual;
  for ( auto it = std::filesystem::recursive_directory_iterator( kFixtureRoot ),
             end = std::filesystem::recursive_directory_iterator();
        it != end; ++it )
  {
    if ( it->is_regular_file() )
    {
      const std::string relative =
        std::filesystem::relative( it->path(), kFixtureRoot ).generic_string();
      actual.insert( relative );
    }
  }

  for ( const std::string &path : actual )
    INFO( "unexpected fixture file: " << path );
  for ( const std::string &path : expected )
    INFO( "missing fixture file: " << path );
  REQUIRE( actual == expected );
}

TEST_CASE( "cn_fixtures: golden products parse to their recorded metadata",
           "[cn11][fixtures][golden]" )
{
  ensureAppShim();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  const Json::Value manifest = readJson( kFixtureRoot + "/manifest.json" );
  for ( const Json::Value &entry : manifest["golden"] )
  {
    const std::string fixture = entry.asString();
    const std::string stem = fileStem( fixture );
    const Json::Value golden = readJson( kFixtureRoot + "/golden/" + stem + ".json" );
    const std::string imageBase = imageBaseFor( stem );
    REQUIRE( imageBase == golden["image_base"].asString() );

    // Assemble the product in the temp dir: fixture XML + synthesized TIFF.
    const QString dir = tmp.path() + QStringLiteral( "/" ) + QString::fromStdString( stem );
    REQUIRE( QDir().mkpath( dir ) );
    copyFile( QString::fromStdString( kFixtureRoot + "/" + fixture ),
              dir + QStringLiteral( "/" ) + QString::fromStdString( imageBase ) + ".xml" );
    writeStackTiff( dir + QStringLiteral( "/" ) + QString::fromStdString( imageBase ) + ".tiff",
                    golden["tiff_bands"].asInt() );
    const QString imagePath = dir + QStringLiteral( "/" ) + QString::fromStdString( imageBase ) +
                              ".tiff";

    // Identity.
    const CnProductIdentity identity = cnIdentifyProduct( imagePath.toStdString() );
    INFO( "fixture: " << fixture );
    REQUIRE( identity.recognized );
    REQUIRE( identity.supported );
    REQUIRE( identity.kindName == golden["kind_name"].asString() );
    REQUIRE( identity.satellite == golden["identity"]["satellite"].asString() );
    REQUIRE( identity.sensorKey == golden["identity"]["sensor_key"].asString() );

    // Declared metadata against the golden record.
    const ProductMetadata metadata = readCnProductMetadata( imagePath.toStdString(), identity );
    const Json::Value &expect = golden["metadata"];
    REQUIRE( metadata.productId == expect["product_id"].asString() );
    REQUIRE( metadata.platform == expect["platform"].asString() );
    REQUIRE( metadata.sensor == expect["sensor"].asString() );
    if ( !expect["sensor_mode"].isNull() )
      REQUIRE( metadata.sensorMode == expect["sensor_mode"].asString() );
    if ( expect.isMember( "processing_level" ) )
      REQUIRE( metadata.processingLevel == expect["processing_level"].asString() );
    if ( expect.isMember( "radiometric_state" ) )
      REQUIRE( metadata.radiometricState == expect["radiometric_state"].asString() );
    if ( expect.isMember( "acquisition_time" ) )
      REQUIRE( metadata.acquisitionTime == expect["acquisition_time"].asString() );
    auto approxMember = [ & ] ( const char *key, double actualValue ) {
      if ( expect.isMember( key ) && expect[key].isNumeric() )
        REQUIRE( actualValue == Catch::Approx( expect[key].asDouble() ).margin( 1e-6 ) );
    };
    approxMember( "resolution_meters", metadata.hasResolution ? metadata.resolutionMeters : -1.0 );
    approxMember( "sun_elevation_deg",
                  metadata.hasSunElevation ? metadata.sunElevationDeg : -1.0 );
    approxMember( "sun_azimuth_deg", metadata.hasSunAzimuth ? metadata.sunAzimuthDeg : -1.0 );
    if ( expect.isMember( "sun_elevation_source" ) )
      REQUIRE( metadata.sunElevationSource == expect["sun_elevation_source"].asString() );
    if ( expect.isMember( "cloud_cover" ) )
    {
      REQUIRE( metadata.hasCloudCover );
      REQUIRE( metadata.cloudCover == Catch::Approx( expect["cloud_cover"].asDouble() ) );
    }
    if ( expect.isMember( "orbit_id" ) )
      REQUIRE( metadata.orbitId == expect["orbit_id"].asString() );
    if ( expect.isMember( "crs_hint" ) )
      REQUIRE( metadata.crsHint == expect["crs_hint"].asString() );
    if ( expect.isMember( "declared_band_ids" ) )
    {
      std::vector<std::string> declared;
      for ( const std::string &band : metadata.declaredBandIds )
        declared.push_back( band );
      REQUIRE( declared.size() == static_cast<std::size_t>( expect["declared_band_ids"].size() ) );
      for ( Json::ArrayIndex i = 0; i < expect["declared_band_ids"].size() && i < declared.size(); ++i )
        REQUIRE( declared[i] == expect["declared_band_ids"][i].asString() );
    }
    if ( expect.isMember( "calibration_band_count" ) )
      REQUIRE( static_cast<int>( metadata.bandCalibration.size() ) ==
               expect["calibration_band_count"].asInt() );
    if ( expect.isMember( "polarizations" ) )
    {
      REQUIRE( metadata.polarizations.size() ==
               static_cast<std::size_t>( expect["polarizations"].size() ) );
      for ( Json::ArrayIndex i = 0;
            i < expect["polarizations"].size() && i < metadata.polarizations.size(); ++i )
        REQUIRE( metadata.polarizations[i] == expect["polarizations"][i].asString() );
    }
    if ( expect.isMember( "orbit_direction" ) )
      REQUIRE( metadata.orbitDirection == expect["orbit_direction"].asString() );
    if ( expect.isMember( "path_row" ) )
    {
      bool sawPathRow = false;
      for ( const auto &extra : metadata.extra )
        sawPathRow |= extra.first == "path_row" && extra.second == expect["path_row"].asString();
      REQUIRE( sawPathRow );
    }
    if ( expect.isMember( "incidence_angle_deg" ) )
    {
      bool sawIncidence = false;
      for ( const auto &extra : metadata.extra )
        sawIncidence |=
          extra.first == "incidence_angle_deg" &&
          extra.second == expect["incidence_angle_deg"].asString();
      REQUIRE( sawIncidence );
    }
    if ( expect.isMember( "beam_mode" ) )
    {
      bool sawBeam = false;
      for ( const auto &extra : metadata.extra )
        sawBeam |= extra.first == "beam_mode" && extra.second == expect["beam_mode"].asString();
      REQUIRE( sawBeam );
    }
    REQUIRE( metadata.parseDiagnostics["generation"].asString() ==
             expect["sidecar_generation"].asString() );
    REQUIRE( metadata.parseDiagnostics["root_element"].asString() ==
             expect["sidecar_root"].asString() );
    if ( expect.isMember( "unknown_top_level_contains" ) )
    {
      std::string joined;
      for ( const Json::Value &name :
            metadata.parseDiagnostics["unknown_top_level_elements"] )
        joined += name.asString();
      std::string needle = expect["unknown_top_level_contains"].asString();
      REQUIRE( joined.find( needle ) != std::string::npos );
    }

    // Registry band roles (camera-local layouts).
    if ( golden.isMember( "band_roles" ) )
    {
      const CnBandRoleTable table = cnBandRoleTable( golden["identity"]["sensor_key"].asString() );
      for ( auto roleIt = golden["band_roles"].begin();
            roleIt != golden["band_roles"].end(); ++roleIt )
      {
        const std::string bandId = roleIt.key().asString();
        const CnBandSpec *band = nullptr;
        for ( const CnBandSpec &candidate : table.bands )
        {
          if ( QString::fromStdString( candidate.band ).compare( QString::fromStdString( bandId ),
                                                                 Qt::CaseInsensitive ) == 0 )
          {
            band = &candidate;
            break;
          }
        }
        INFO( "band: " << bandId );
        REQUIRE( band != nullptr );
        REQUIRE( band->role == roleIt->asString() );
      }
    }

    // Hyperspectral axis contract.
    if ( golden.isMember( "band_axis" ) )
    {
      const SensorProfileRecord profile =
        loadSensorProfile( golden["identity"]["sensor_key"].asString() );
      REQUIRE( profile.hasBandAxis );
      REQUIRE( profile.bandAxisCount == golden["band_axis"]["count"].asInt() );
      REQUIRE( profile.bandAxisOrdering.find(
                 golden["band_axis"]["ordering_contains"].asString() ) != std::string::npos );
    }
    if ( golden.isMember( "measurement_assets" ) )
    {
      ProductAdapterRegistry &registry = ProductAdapterRegistry::instance();
      ProductAssets assets = registry.describe( imagePath.toStdString() );
      int measurements = 0;
      for ( const ProductAsset &asset : assets.assets )
        measurements += asset.role == "measurement" ? 1 : 0;
      REQUIRE( measurements == golden["measurement_assets"].asInt() );
    }
  }
  qunsetenv( "SICNU_DATA_DIR" );
}

TEST_CASE( "cn_fixtures: corrupt fixtures are typed refusals",
           "[cn11][fixtures][corrupt]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  // Malformed XML on a recognized GF-3 name → parse error.
  {
    const QString path = tmp.path() + "/GF3_QPS_E113.9_N22.2_20221121_L1A00000012345-HH.xml";
    copyFile( QString::fromStdString( kFixtureRoot + "/corrupt/malformed.xml" ), path );
    const CnProductIdentity identity = cnIdentifyProduct( path.toStdString() );
    REQUIRE( identity.supported );
    bool threw = false;
    try
    {
      (void)readCnProductMetadata( path.toStdString(), identity );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      REQUIRE( error.code() == sicnu::geo::ErrorCode::InvalidArgument );
      REQUIRE( std::string( error.what() ).find( "Malformed" ) != std::string::npos );
    }
    REQUIRE( threw );
  }

  // Valid XML, no product identity tags → unsupported, never guessed.
  {
    const QString path = tmp.path() + "/GF1_PMS_E113.5_N23.5_20230512_L1A0123456789-MSS1.xml";
    copyFile( QString::fromStdString( kFixtureRoot + "/corrupt/no_identity.xml" ), path );
    const CnProductIdentity identity = cnIdentifyProduct( path.toStdString() );
    REQUIRE( identity.supported );
    bool threw = false;
    try
    {
      (void)readCnProductMetadata( path.toStdString(), identity );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      REQUIRE( error.code() == sicnu::geo::ErrorCode::UnsupportedProduct );
      REQUIRE( std::string( error.what() ).find( "CRESDA" ) != std::string::npos );
    }
    REQUIRE( threw );
  }

  // CBERS with an unknown root → INPE generation refuses to guess.
  {
    const QString path = tmp.path() + "/CBERS4_WFI_20230415_0364_075_L1.xml";
    copyFile( QString::fromStdString( kFixtureRoot + "/corrupt/cbers_unknown_root.xml" ), path );
    const CnProductIdentity identity = cnIdentifyProduct( path.toStdString() );
    REQUIRE( identity.supported );
    bool threw = false;
    try
    {
      (void)readCnProductMetadata( path.toStdString(), identity );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      REQUIRE( error.code() == sicnu::geo::ErrorCode::UnsupportedProduct );
      REQUIRE( std::string( error.what() ).find( "Unknown CBERS sidecar generation" ) !=
               std::string::npos );
    }
    REQUIRE( threw );
  }
}
