// test_radiometric_transition.cpp — radiometric-physics-11 work package A.
//
// Oracles: the lawful DAG is checked against an independent adjacency table
// written out in the test (not by calling isLawfulEdge for the negative
// cases' mirror logic); requirement predicates are exercised with explicitly
// constructed coefficient sets whose provenance (which MTL keys were
// present) is stated in comments; provenance JSON is validated structurally
// (schema id, step names, numeric scale round-trip). Negative tests pin the
// typed refusals: unknown states, unlawful pairs, every missing-input token.

#include "processing/algorithms/radiometric_transition.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cmath>

using namespace RadiometricTransition;
using RadiometricCalibration::BandCoefficients;
using RadiometricCalibration::SensorType;

namespace
{
/// Independent statement of the lawful DAG (the five vocabulary states).
bool referenceLawful( const QString &from, const QString &to )
{
    if ( from == to )
        return true;
    if ( from == QLatin1String( "digital_number" ) )
        return to == QLatin1String( "radiance" ) || to == QLatin1String( "toa_reflectance" );
    if ( from == QLatin1String( "radiance" ) )
        return to == QLatin1String( "toa_reflectance" )
               || to == QLatin1String( "brightness_temperature" );
    if ( from == QLatin1String( "toa_reflectance" ) )
        return to == QLatin1String( "surface_reflectance" );
    return false;
}

const char *kStates[] = { "digital_number", "radiance", "toa_reflectance",
                          "surface_reflectance", "brightness_temperature" };

BandCoefficients landsatCoeffs()
{
    // Landsat 8 OLI/TIRS shape: RADIANCE_MULT/ADD, REFLECTANCE_MULT/ADD and
    // thermal K1/K2 all present in the MTL (per-band gates set accordingly).
    BandCoefficients c;
    c.radianceGain = 1.2753e-2;
    c.radianceBias = -6.777;
    c.reflMult = 2.0e-5;
    c.reflAdd = -0.1;
    c.k1 = 774.8853;
    c.k2 = 1321.0789;
    c.hasRadiance = true;
    c.hasReflectance = true;
    return c;
}
} // namespace

TEST_CASE( "isKnownState accepts exactly the ADR 0114 vocabulary", "[transition]" )
{
    for ( const char *s : kStates )
        CHECK( isKnownState( QLatin1String( s ) ) );
    CHECK_FALSE( isKnownState( QStringLiteral( "reflectance" ) ) ); // imprecise label
    CHECK_FALSE( isKnownState( QStringLiteral( "TOA" ) ) );
    CHECK_FALSE( isKnownState( QString() ) );
}

TEST_CASE( "lawful DAG matches the independent adjacency table", "[transition]" )
{
    for ( const char *from : kStates )
        for ( const char *to : kStates )
        {
            INFO( from << " -> " << to );
            CHECK( isLawfulEdge( QLatin1String( from ), QLatin1String( to ) )
                   == referenceLawful( QLatin1String( from ), QLatin1String( to ) ) );
        }
    CHECK_FALSE( isLawfulEdge( QStringLiteral( "reflectance" ), QStringLiteral( "radiance" ) ) );
}

TEST_CASE( "identity plans are lawful and satisfiable with no steps", "[transition]" )
{
    StepInputs in;
    const Plan p = plan( QStringLiteral( "toa_reflectance" ), QStringLiteral( "toa_reflectance" ), in );
    CHECK( p.lawful );
    CHECK( p.satisfiable );
    CHECK( p.steps.isEmpty() );
    CHECK( p.missing.isEmpty() );
    CHECK( p.provenance["schema"].asString() == "exp_rs_radiometric_provenance/1" );
    CHECK( p.provenance["from"].asString() == "toa_reflectance" );
    CHECK( p.provenance["steps"].size() == 0 );
}

TEST_CASE( "DN→radiance requires radiance coefficients", "[transition]" )
{
    StepInputs noCoeffs;
    const Plan refused = plan( QStringLiteral( "digital_number" ),
                               QStringLiteral( "radiance" ), noCoeffs );
    CHECK( refused.lawful );
    CHECK_FALSE( refused.satisfiable );
    REQUIRE( refused.missing.size() == 1 );
    CHECK( refused.missing.first() == QLatin1String( "radiance_coefficients" ) );
    CHECK( refused.provenance["satisfiable"].asBool() == false );
    CHECK( refused.provenance["missing"].size() == 1 );

    StepInputs with = noCoeffs;
    BandCoefficients c = landsatCoeffs();
    with.coeffs = &c;
    const Plan ok = plan( QStringLiteral( "digital_number" ), QStringLiteral( "radiance" ), with );
    CHECK( ok.satisfiable );
    REQUIRE( ok.steps.size() == 1 );
    CHECK( ok.steps.first() == QLatin1String( "dn_to_radiance" ) );
    CHECK( ok.provenance["steps"][0]["radiance_gain"].asDouble()
           == Catch::Approx( c.radianceGain ) );
}

TEST_CASE( "DN→TOA per-sensor requirements", "[transition]" )
{
    // Landsat: REFLECTANCE_MULT/ADD AND a real SUN_ELEVATION must exist —
    // the 90° default would skew 1/sin(θe) by ~1.5× at 42° (house contract).
    StepInputs in;
    in.sensor = SensorType::Landsat;
    BandCoefficients c = landsatCoeffs();

    in.coeffs = &c;
    Plan p = plan( QStringLiteral( "digital_number" ), QStringLiteral( "toa_reflectance" ), in );
    CHECK( p.lawful );
    CHECK_FALSE( p.satisfiable );
    CHECK( p.missing.contains( QLatin1String( "sun_elevation" ) ) );
    CHECK_FALSE( p.missing.contains( QLatin1String( "reflectance_coefficients" ) ) );

    BandCoefficients noRefl = c;
    noRefl.hasReflectance = false;
    in.coeffs = &noRefl;
    in.sunElevationDeg = 42.5;
    in.sunElevationKnown = true;
    in.sunElevationFromMetadata = true;
    p = plan( QStringLiteral( "digital_number" ), QStringLiteral( "toa_reflectance" ), in );
    CHECK_FALSE( p.satisfiable );
    CHECK( p.missing.contains( QLatin1String( "reflectance_coefficients" ) ) );

    // Degenerate sun (below horizon) is refused, not defaulted.
    in.coeffs = &c;
    in.sunElevationDeg = -5.0;
    p = plan( QStringLiteral( "digital_number" ), QStringLiteral( "toa_reflectance" ), in );
    CHECK( p.missing.contains( QLatin1String( "sun_elevation" ) ) );

    // Complete Landsat case records the metadata sun source.
    in.sunElevationDeg = 42.5;
    p = plan( QStringLiteral( "digital_number" ), QStringLiteral( "toa_reflectance" ), in );
    CHECK( p.satisfiable );
    CHECK( p.provenance["steps"][0]["sun_elevation_deg"].asDouble()
           == Catch::Approx( 42.5 ) );
    CHECK( p.provenance["steps"][0]["sun_elevation_source"].asString() == "metadata" );

    // Sentinel-2: quantification path (scale 10000 + offset), no angles needed.
    StepInputs s2;
    s2.sensor = SensorType::Sentinel2;
    BandCoefficients q;
    q.scale = 10000.0;
    q.offset = -1000.0;
    s2.coeffs = &q;
    p = plan( QStringLiteral( "digital_number" ), QStringLiteral( "toa_reflectance" ), s2 );
    CHECK( p.satisfiable );
    CHECK( p.provenance["steps"][0]["scale"].asDouble() == 10000.0 );

    // Generic sensor with untouched defaults = no quantification → refusal
    // (a bare DN relabelled "reflectance" is exactly the failure this blocks).
    StepInputs generic;
    generic.sensor = SensorType::Generic;
    BandCoefficients defaults;
    generic.coeffs = &defaults;
    p = plan( QStringLiteral( "digital_number" ), QStringLiteral( "toa_reflectance" ), generic );
    CHECK_FALSE( p.satisfiable );
    CHECK( p.missing.contains( QLatin1String( "reflectance_coefficients" ) ) );
}

TEST_CASE( "radiance→TOA is the ESUN path and needs geometry", "[transition]" )
{
    StepInputs in;
    in.sensor = SensorType::Landsat;
    in.esun = 1546.57; // L8 B4 ESUN value shape
    in.sunElevationDeg = 60.0;
    in.sunElevationKnown = true;
    in.sunElevationFromMetadata = false; // derived via SolarGeometry
    const Plan p = plan( QStringLiteral( "radiance" ), QStringLiteral( "toa_reflectance" ), in );
    CHECK( p.satisfiable );
    CHECK( p.steps.first() == QLatin1String( "radiance_to_toa_reflectance" ) );
    CHECK( p.provenance["steps"][0]["esun"].asDouble() == 1546.57 );
    CHECK( p.provenance["steps"][0]["sun_elevation_source"].asString() == "computed" );

    StepInputs noEsun = in;
    noEsun.esun = 0.0;
    const Plan refused =
        plan( QStringLiteral( "radiance" ), QStringLiteral( "toa_reflectance" ), noEsun );
    CHECK( refused.missing.contains( QLatin1String( "esun" ) ) );
    CHECK_FALSE( refused.satisfiable );
}

TEST_CASE( "thermal branch requires K1/K2 and skips unlawful shortcuts", "[transition]" )
{
    StepInputs in;
    in.sensor = SensorType::Landsat;
    BandCoefficients c = landsatCoeffs();
    in.coeffs = &c;

    const Plan ok =
        plan( QStringLiteral( "digital_number" ), QStringLiteral( "brightness_temperature" ), in );
    CHECK( ok.lawful );
    CHECK( ok.satisfiable );
    REQUIRE( ok.steps.size() == 2 );
    CHECK( ok.steps[0] == QLatin1String( "dn_to_radiance" ) );
    CHECK( ok.steps[1] == QLatin1String( "radiance_to_brightness_temperature" ) );
    CHECK( ok.provenance["steps"][1]["k1"].asDouble() == Catch::Approx( c.k1 ) );

    BandCoefficients noK = c;
    noK.k1 = 0.0;
    in.coeffs = &noK;
    const Plan refused =
        plan( QStringLiteral( "radiance" ), QStringLiteral( "brightness_temperature" ), in );
    CHECK( refused.missing.contains( QLatin1String( "thermal_constants" ) ) );

    // Category errors: reflectance can never become temperature, and
    // surface reflectance / brightness temperature are terminal states.
    for ( const auto &pair : { std::make_pair( QStringLiteral( "toa_reflectance" ),
                                               QStringLiteral( "brightness_temperature" ) ),
                               std::make_pair( QStringLiteral( "surface_reflectance" ),
                                               QStringLiteral( "toa_reflectance" ) ),
                               std::make_pair( QStringLiteral( "brightness_temperature" ),
                                               QStringLiteral( "radiance" ) ),
                               std::make_pair( QStringLiteral( "digital_number" ),
                                               QStringLiteral( "surface_reflectance" ) ),
                               std::make_pair( QStringLiteral( "radiance" ),
                                               QStringLiteral( "surface_reflectance" ) ) } )
    {
        in.coeffs = &c;
        const Plan p = plan( pair.first, pair.second, in );
        INFO( pair.first << " -> " << pair.second << ": " << p.explanation );
        CHECK_FALSE( p.lawful );
        CHECK( p.steps.isEmpty() );
        CHECK( p.missing.isEmpty() ); // unlawful ≠ missing inputs
        CHECK( p.provenance.isNull() );
    }
}

TEST_CASE( "TOA→surface requires a named atmospheric provider", "[transition]" )
{
    StepInputs in;
    in.sensor = SensorType::Landsat;
    BandCoefficients c = landsatCoeffs();
    in.coeffs = &c;
    in.sunElevationDeg = 40.0;
    in.sunElevationKnown = true;
    in.sunElevationFromMetadata = true;

    const Plan refused =
        plan( QStringLiteral( "toa_reflectance" ), QStringLiteral( "surface_reflectance" ), in );
    CHECK( refused.lawful );
    CHECK_FALSE( refused.satisfiable );
    CHECK( refused.missing == QStringList{ QLatin1String( "atmospheric_provider" ) } );

    in.atmosphericProvider = QStringLiteral( "dos1" );
    const Plan ok =
        plan( QStringLiteral( "toa_reflectance" ), QStringLiteral( "surface_reflectance" ), in );
    CHECK( ok.satisfiable );
    CHECK( ok.provenance["steps"][0]["provider"].asString() == "dos1" );

    // Full chain DN→SR: two edges, provenance lists both, scale round-trips.
    const Plan chain =
        plan( QStringLiteral( "digital_number" ), QStringLiteral( "surface_reflectance" ), in );
    REQUIRE( chain.steps.size() == 2 );
    CHECK( chain.satisfiable );
    CHECK( chain.provenance["steps"].size() == 2 );
    CHECK( chain.provenance["numeric_scale_before"].asDouble() == 1.0 );
    CHECK( chain.provenance["numeric_scale_after"].asDouble() == 1.0 );
}

TEST_CASE( "multi-edge refusals union every missing input", "[transition]" )
{
    StepInputs in; // nothing provided at all
    in.sensor = SensorType::Landsat;
    const Plan p =
        plan( QStringLiteral( "digital_number" ), QStringLiteral( "surface_reflectance" ), in );
    CHECK( p.lawful );
    CHECK_FALSE( p.satisfiable );
    // DN→TOA needs reflectance coeffs + sun elevation; TOA→SR needs provider.
    CHECK( p.missing.contains( QLatin1String( "reflectance_coefficients" ) ) );
    CHECK( p.missing.contains( QLatin1String( "sun_elevation" ) ) );
    CHECK( p.missing.contains( QLatin1String( "atmospheric_provider" ) ) );
    CHECK( p.missing.size() == 3 );
    CHECK( p.explanation.contains( QLatin1String( "refused" ) ) );
}
