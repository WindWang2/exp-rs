// tests/test_radiometric_state.cpp — radiometric FSM contract tests (D13 / ADR 0158)
//
// Independent truth source: the formal transition graph in
// docs/adr/0158-radiometric-physics-state-system.md, transcribed by hand into
// the expected-matrix table below (never derived from the implementation).
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/radiometric_state.h"

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include <QTemporaryDir>

#include <gdal_priv.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
  using exp_radiometric::RadiometricState;
  using exp_radiometric::RadiometricStateMismatchException;
  using exp_radiometric::RadiometricUnit;
  using U = RadiometricUnit;

  /// Hand-written ground truth for the 5×5 transition matrix, straight from
  /// the ADR edge table (identity rows lawful, everything else unlawful).
  bool expectedLawful( RadiometricUnit from, RadiometricUnit to )
  {
    if ( from == to )
      return true;
    if ( from == U::DigitalNumber && to == U::Radiance ) return true;
    if ( from == U::DigitalNumber && to == U::ToaReflectance ) return true;
    if ( from == U::Radiance && to == U::ToaReflectance ) return true;
    if ( from == U::Radiance && to == U::BoaReflectance ) return true;
    if ( from == U::Radiance && to == U::BrightnessTemperature ) return true;
    if ( from == U::ToaReflectance && to == U::BoaReflectance ) return true;
    return false;
  }

  /// Writes a minimal single-band Float32 GeoTIFF and returns its path.
  std::string writeTinyGeotiff( const QTemporaryDir &dir, const QString &name )
  {
    const std::string path = ( dir.filePath( name ) ).toStdString();
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    GDALDataset *ds = driver->Create( path.c_str(), 4, 4, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> line( 4, 0.5f );
    float *scanline[1] = { line.data() };
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 4, 4, line.data(), 4, 4, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
  }

  std::unique_ptr<QgsRasterLayer> openLayer( const std::string &path )
  {
    auto layer = std::make_unique<QgsRasterLayer>( QString::fromStdString( path ), QStringLiteral( "fixture" ) );
    REQUIRE( layer );
    return layer;
  }
} // namespace

int main( int argc, char *argv[] )
{
  QgsApplication app( argc, argv, false );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE( "RadiometricState FSM transition law", "[radiometric][state]" )
{
  const RadiometricUnit all[] = { U::DigitalNumber, U::Radiance, U::ToaReflectance,
                                  U::BoaReflectance, U::BrightnessTemperature };
  const char *names[] = { "DN", "Radiance", "TOA", "BOA", "BT" };

  // Full 5×5 matrix against the hand-transcribed ADR table.
  for ( int i = 0; i < 5; ++i )
  {
    for ( int j = 0; j < 5; ++j )
    {
      INFO( names[i] << " -> " << names[j] );
      REQUIRE( RadiometricState::canTransition( all[i], all[j] ) == expectedLawful( all[i], all[j] ) );
    }
  }

  // Spot-check the mission-critical fail-closed edges explicitly.
  REQUIRE_FALSE( RadiometricState::canTransition( U::DigitalNumber, U::BoaReflectance ) );
  REQUIRE_FALSE( RadiometricState::canTransition( U::DigitalNumber, U::BrightnessTemperature ) );
  REQUIRE_FALSE( RadiometricState::canTransition( U::ToaReflectance, U::BrightnessTemperature ) );
  REQUIRE_FALSE( RadiometricState::canTransition( U::BoaReflectance, U::DigitalNumber ) );
  REQUIRE( RadiometricState::canTransition( U::DigitalNumber, U::Radiance ) );
  REQUIRE( RadiometricState::canTransition( U::Radiance, U::ToaReflectance ) );
  REQUIRE( RadiometricState::canTransition( U::Radiance, U::BrightnessTemperature ) );
  REQUIRE( RadiometricState::canTransition( U::ToaReflectance, U::BoaReflectance ) );
}

TEST_CASE( "RadiometricState string round-trip and fail-safe parse", "[radiometric][state]" )
{
  // Round-trip idempotency over every state.
  const RadiometricUnit all[] = { U::DigitalNumber, U::Radiance, U::ToaReflectance,
                                  U::BoaReflectance, U::BrightnessTemperature };
  for ( RadiometricUnit unit : all )
  {
    const QString str = RadiometricState::unitToString( unit );
    CAPTURE( str );
    REQUIRE( RadiometricState::stringToUnit( str ) == unit );
    REQUIRE( str == str.toUpper() ); // canonical form is uppercase
  }

  // Case-insensitive parsing.
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "radiance" ) ) == U::Radiance );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "  Surface_Reflectance " ) ) == U::BoaReflectance );

  // Unrecognized strings degrade to DigitalNumber.
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "REFLECTANCE" ) ) == U::DigitalNumber );
  REQUIRE( RadiometricState::stringToUnit( QString() ) == U::DigitalNumber );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "42" ) ) == U::DigitalNumber );
}

TEST_CASE( "RadiometricState preflight exception blocks unlawful operator entry", "[radiometric][state]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Synthesize a layer carrying the DIGITAL_NUMBER marker.
  const std::string path = writeTinyGeotiff( dir, QStringLiteral( "dn.tif" ) );
  auto layer = openLayer( path );
  REQUIRE( RadiometricState::setLayerRadiometricState( layer.get(), U::DigitalNumber ) );

  // DN -> BOA is unlawful: the preflight MUST throw.
  REQUIRE_THROWS_AS( RadiometricState::validateBandPreflight( layer.get(), U::BoaReflectance ),
                     RadiometricStateMismatchException );
  REQUIRE_THROWS_AS( RadiometricState::validateBandPreflight( layer.get(), U::BrightnessTemperature ),
                     RadiometricStateMismatchException );

  // DN -> { DN identity, Radiance, TOA } are lawful: no exception.
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::DigitalNumber ) );
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::Radiance ) );
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::ToaReflectance ) );
}

TEST_CASE( "RadiometricState set/read round-trip and forward-chain promotion", "[radiometric][state]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string path = writeTinyGeotiff( dir, QStringLiteral( "chain.tif" ) );
  auto layer = openLayer( path );

  // DN -> Radiance -> TOA -> BOA lawful promotion chain through the seam.
  REQUIRE( RadiometricState::setLayerRadiometricState( layer.get(), U::Radiance ) );
  REQUIRE( RadiometricState::layerUnit( layer.get() ) == U::Radiance );
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::ToaReflectance ) );

  REQUIRE( RadiometricState::setLayerRadiometricState( layer.get(), U::ToaReflectance ) );
  REQUIRE( RadiometricState::layerUnit( layer.get() ) == U::ToaReflectance );
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::BoaReflectance ) );

  // TOA -> BT is unlawful even mid-chain.
  REQUIRE_THROWS_AS( RadiometricState::validateBandPreflight( layer.get(), U::BrightnessTemperature ),
                     RadiometricStateMismatchException );

  REQUIRE( RadiometricState::setLayerRadiometricState( layer.get(), U::BoaReflectance ) );
  REQUIRE( RadiometricState::layerUnit( layer.get() ) == U::BoaReflectance );
  // No lawful forward edge from BOA: every further requirement is refused.
  REQUIRE_THROWS_AS( RadiometricState::validateBandPreflight( layer.get(), U::Radiance ),
                     RadiometricStateMismatchException );
}

TEST_CASE( "RadiometricState honors GDAL file metadata when property is absent", "[radiometric][state]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string path = writeTinyGeotiff( dir, QStringLiteral( "gdal_marker.tif" ) );

  // Write the marker straight into GDAL metadata — independent of the seam
  // under test — as a previous-session persistence would.
  {
    GDALDataset *ds = static_cast<GDALDataset *>(
      GDALOpenEx( path.c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr ) );
    REQUIRE( ds != nullptr );
    ds->SetMetadataItem( "SICNU_RADIOMETRIC_STATE", "RADIANCE", nullptr );
    GDALClose( ds );
  }

  auto layer = openLayer( path );
  // No custom property set on this fresh layer object.
  REQUIRE( RadiometricState::layerUnit( layer.get() ) == U::Radiance );
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::BrightnessTemperature ) );
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::BoaReflectance ) );
  // Backwards inversion Radiance -> DN is refused.
  REQUIRE_THROWS_AS( RadiometricState::validateBandPreflight( layer.get(), U::DigitalNumber ),
                     RadiometricStateMismatchException );
}

TEST_CASE( "RadiometricState missing marker degrades to DigitalNumber", "[radiometric][state]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const std::string path = writeTinyGeotiff( dir, QStringLiteral( "unmarked.tif" ) );
  auto layer = openLayer( path );

  REQUIRE( RadiometricState::layerUnit( layer.get() ) == U::DigitalNumber );
  REQUIRE_NOTHROW( RadiometricState::validateBandPreflight( layer.get(), U::Radiance ) );
  REQUIRE_THROWS_AS( RadiometricState::validateBandPreflight( layer.get(), U::BoaReflectance ),
                     RadiometricStateMismatchException );
}

TEST_CASE( "RadiometricState null layer fails safe", "[radiometric][state]" )
{
  REQUIRE( RadiometricState::layerUnit( nullptr ) == U::DigitalNumber );
  REQUIRE_FALSE( RadiometricState::setLayerRadiometricState( nullptr, U::Radiance ) );
  REQUIRE_THROWS_AS( RadiometricState::validateBandPreflight( nullptr, U::BoaReflectance ),
                     RadiometricStateMismatchException );
}
