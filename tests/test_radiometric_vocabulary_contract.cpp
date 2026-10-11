// tests/test_radiometric_vocabulary_contract.cpp
// Cross-module radiometric vocabulary contract (#1450): every token the
// operators write into SICNU_RADIOMETRIC_STATE — the EO calibration writers
// (SatelliteProducts constants), the SAR writers (sigma0/gamma0/beta0 and
// the derived tokens) and the manifest eo.calibration_state vocabulary —
// must be recognized by the single platform vocabulary
// (exp_radiometric::vocabulary / RadiometricState). On master this contract
// was violated: every SAR and derived token degraded to DigitalNumber, so
// already-calibrated SAR data looked like raw DN to the FSM.
#include <catch2/catch_test_macros.hpp>

#include "core/radiometric_state.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/algorithms/sar/sar_metadata.h"

#include <QString>
#include <QStringList>

using exp_radiometric::RadiometricState;
using U = exp_radiometric::RadiometricUnit;
namespace V = exp_radiometric::vocabulary;

TEST_CASE( "Every operator-written radiometric token is known to the platform vocabulary",
           "[radiometric][vocabulary][contract]" )
{
  struct Writer
  {
    QString token;
    QString who;
  };
  const Writer writers[] = {
    // EO calibration / atmospheric correction writers (satellite_products.h).
    { QLatin1String( SatelliteProducts::kRadiometricStateRadiance ), "satellite_products" },
    { QLatin1String( SatelliteProducts::kRadiometricStateToaReflectance ), "satellite_products" },
    { QLatin1String( SatelliteProducts::kRadiometricStateSurfaceReflectance ), "satellite_products" },
    { QLatin1String( SatelliteProducts::kRadiometricStateBrightnessTemperature ), "satellite_products" },
    { QLatin1String( SatelliteProducts::kRadiometricStateDigitalNumber ), "satellite_products" },
    // SAR writers: rs_sar_calibrate writes "sigma0" (:341), rs_sar_terrain_
    // flatten / terrain_correction write "gamma0" (:285/:272), rs_sar_
    // backscatter writes sigma0/gamma0/beta0/dn per its conversion target.
    { QStringLiteral( "sigma0" ), "rs_sar_calibrate" },
    { QStringLiteral( "gamma0" ), "rs_sar_terrain_flatten" },
    { QStringLiteral( "beta0" ), "rs_sar_backscatter" },
    { QStringLiteral( "dn" ), "rs_sar_backscatter" },
    // Derived-product tokens (sar_metadata.h): never a calibration state.
    { QLatin1String( sicnu::sar::kDerivedPairMetricState ), "rs_sar_ratio" },
    { QLatin1String( sicnu::sar::kDerivedTextureState ), "rs_sar_texture" },
    // Manifest eo.calibration_state vocabulary (model_catalog.h).
    { QStringLiteral( "indices" ), "model_manifest" },
  };

  for ( const Writer &w : writers )
  {
    INFO( "writer=" << w.who.toStdString() << " token=" << w.token.toStdString() );
    REQUIRE( V::isKnownToken( w.token ) );
  }

  // The specific #1450 trap: SAR tokens must NOT degrade to DigitalNumber.
  // (These three lines are red on master, where stringToUnit falls through.)
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "sigma0" ) ) != U::DigitalNumber );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "gamma0" ) ) != U::DigitalNumber );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "beta0" ) ) != U::DigitalNumber );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "dn" ) ) == U::DigitalNumber );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "sar_pair_metric" ) ) == U::SarPairMetric );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "sar_texture" ) ) == U::SarTexture );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "indices" ) ) == U::SpectralIndices );
  REQUIRE( RadiometricState::stringToUnit( QStringLiteral( "SIGMA0" ) ) == U::Sigma0 );

  // Pin the SAR-side canonical spellings too: the literal writers above must
  // stay identical to sicnu::sar::normalizeCalibration's canonical forms, or
  // a spelling drift in either direction breaks re-ingestion.
  REQUIRE( sicnu::sar::normalizeCalibration( QStringLiteral( "sigma0" ) ) == QStringLiteral( "sigma0" ) );
  REQUIRE( sicnu::sar::normalizeCalibration( QStringLiteral( "gamma0" ) ) == QStringLiteral( "gamma0" ) );
  REQUIRE( sicnu::sar::normalizeCalibration( QStringLiteral( "beta0" ) ) == QStringLiteral( "beta0" ) );
  REQUIRE( sicnu::sar::normalizeCalibration( QStringLiteral( "dn" ) ) == QStringLiteral( "dn" ) );
}

TEST_CASE( "SAR radiometric transitions move forward only", "[radiometric][vocabulary][contract]" )
{
  using R = RadiometricState;
  // Calibration: raw counts reach every backscatter state.
  REQUIRE( R::canTransition( U::DigitalNumber, U::Sigma0 ) );
  REQUIRE( R::canTransition( U::DigitalNumber, U::Gamma0 ) );
  REQUIRE( R::canTransition( U::DigitalNumber, U::Beta0 ) );
  // Domain/terrain chain: beta0 → sigma0 → gamma0 forward, never backward.
  REQUIRE( R::canTransition( U::Beta0, U::Sigma0 ) );
  REQUIRE( R::canTransition( U::Beta0, U::Gamma0 ) );
  REQUIRE( R::canTransition( U::Sigma0, U::Gamma0 ) );
  REQUIRE_FALSE( R::canTransition( U::Sigma0, U::Beta0 ) );
  REQUIRE_FALSE( R::canTransition( U::Gamma0, U::Sigma0 ) );
  REQUIRE_FALSE( R::canTransition( U::Gamma0, U::DigitalNumber ) );
  // Cross-family: SAR never masquerades as optical physics.
  REQUIRE_FALSE( R::canTransition( U::Sigma0, U::ToaReflectance ) );
  REQUIRE_FALSE( R::canTransition( U::Radiance, U::Gamma0 ) );
}

TEST_CASE( "Derived radiometric states are fail-closed", "[radiometric][vocabulary][contract]" )
{
  using R = RadiometricState;
  REQUIRE_FALSE( R::canTransition( U::SarPairMetric, U::Sigma0 ) );
  REQUIRE_FALSE( R::canTransition( U::SarPairMetric, U::DigitalNumber ) );
  REQUIRE_FALSE( R::canTransition( U::SarTexture, U::Gamma0 ) );
  REQUIRE_FALSE( R::canTransition( U::SpectralIndices, U::BoaReflectance ) );
  // Identity stays lawful for every state.
  REQUIRE( R::canTransition( U::SarPairMetric, U::SarPairMetric ) );
  REQUIRE( R::canTransition( U::Sigma0, U::Sigma0 ) );
}

TEST_CASE( "Radiometric unitToString round-trips the whole vocabulary",
           "[radiometric][vocabulary][contract]" )
{
  const U all[] = {
    U::DigitalNumber, U::Radiance, U::ToaReflectance, U::BoaReflectance,
    U::BrightnessTemperature, U::Sigma0, U::Gamma0, U::Beta0,
    U::SarPairMetric, U::SarTexture, U::SpectralIndices,
  };
  for ( const U unit : all )
  {
    const QString token = RadiometricState::unitToString( unit );
    INFO( "unit int=" << static_cast<int>( unit ) << " token=" << token.toStdString() );
    REQUIRE_FALSE( token.isEmpty() );
    REQUIRE( RadiometricState::stringToUnit( token ) == unit );
  }
}

TEST_CASE( "Radiometric token equivalence: spelling variants match, unknowns stay closed",
           "[radiometric][vocabulary][contract]" )
{
  REQUIRE( V::tokensEquivalent( QStringLiteral( "toa_reflectance" ),
                                QStringLiteral( "TOA_REFLECTANCE" ) ) );
  REQUIRE( V::tokensEquivalent( QStringLiteral( "surface_reflectance" ),
                                QStringLiteral( "boa_reflectance" ) ) );
  REQUIRE( V::tokensEquivalent( QStringLiteral( "gamma0" ),
                                QStringLiteral( "GAMMA0" ) ) );
  REQUIRE( V::tokensEquivalent( QStringLiteral( " digital_number " ),
                                QStringLiteral( "dn" ) ) );

  REQUIRE_FALSE( V::tokensEquivalent( QStringLiteral( "sigma0" ),
                                      QStringLiteral( "gamma0" ) ) );
  REQUIRE_FALSE( V::tokensEquivalent( QStringLiteral( "radiance" ),
                                      QStringLiteral( "banana" ) ) );
  REQUIRE_FALSE( V::tokensEquivalent( QStringLiteral( "banana" ),
                                      QStringLiteral( "banana2" ) ) );
  // The master trap: "REFLECTANCE" is NOT a known unit (it degraded to DN).
  REQUIRE_FALSE( V::isKnownToken( QStringLiteral( "REFLECTANCE" ) ) );
  REQUIRE_FALSE( V::isKnownToken( QString() ) );
}
