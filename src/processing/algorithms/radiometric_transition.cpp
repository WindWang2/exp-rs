// src/processing/algorithms/radiometric_transition.cpp — see
// radiometric_transition.h for the DAG, predicates and provenance contract.
#include "radiometric_transition.h"

#include "satellite_products.h"

#include <cmath>
#include <map>

namespace RadiometricTransition
{

const char *const kStepDnToRadiance = "dn_to_radiance";
const char *const kStepDnToToaReflectance = "dn_to_toa_reflectance";
const char *const kStepRadianceToToaReflectance = "radiance_to_toa_reflectance";
const char *const kStepRadianceToBrightnessTemperature =
    "radiance_to_brightness_temperature";
const char *const kStepToaToSurfaceReflectance = "toa_to_surface_reflectance";

const char *const kMissingRadianceCoefficients = "radiance_coefficients";
const char *const kMissingReflectanceCoefficients = "reflectance_coefficients";
const char *const kMissingSunElevation = "sun_elevation";
const char *const kMissingEsun = "esun";
const char *const kMissingThermalConstants = "thermal_constants";
const char *const kMissingAtmosphericProvider = "atmospheric_provider";

namespace
{
using SatelliteProducts::kRadiometricStateBrightnessTemperature;
using SatelliteProducts::kRadiometricStateDigitalNumber;
using SatelliteProducts::kRadiometricStateRadiance;
using SatelliteProducts::kRadiometricStateSurfaceReflectance;
using SatelliteProducts::kRadiometricStateToaReflectance;

bool finite( double v )
{
    return std::isfinite( v );
}

/// Landsat-style coefficient path presence flags.
bool hasReflectanceCoeffs( const StepInputs &in )
{
    return in.coeffs && in.coeffs->hasReflectance;
}

bool hasRadianceCoeffs( const StepInputs &in )
{
    return in.coeffs && in.coeffs->hasRadiance;
}

/// Generic / Sentinel-2 quantification path: the quantification value must
/// have actually been loaded (non-default scale/offset), mirroring the
/// house parser's insertion rule (a bare default would silently relabel DN).
bool hasQuantification( const StepInputs &in )
{
    if ( !in.coeffs )
        return false;
    if ( !finite( in.coeffs->scale ) || !finite( in.coeffs->offset ) )
        return false;
    return in.coeffs->scale != 1.0 || in.coeffs->offset != 0.0;
}

bool hasUsableSunElevation( const StepInputs &in )
{
    return in.sunElevationKnown && finite( in.sunElevationDeg ) && in.sunElevationDeg > 0.0
           && in.sunElevationDeg <= 90.0;
}

/// Required-input evaluation per edge. Appends the missing tokens for THIS
/// edge; returns true when nothing is missing.
bool edgeSatisfiable( const QString &step, const StepInputs &in, QStringList *missing )
{
    bool ok = true;
    if ( step == kStepDnToRadiance )
    {
        if ( !hasRadianceCoeffs( in ) )
        {
            missing->append( kMissingRadianceCoefficients );
            ok = false;
        }
    }
    else if ( step == kStepDnToToaReflectance )
    {
        if ( in.sensor == SensorType::Landsat )
        {
            if ( !hasReflectanceCoeffs( in ) )
            {
                missing->append( kMissingReflectanceCoefficients );
                ok = false;
            }
            if ( !hasUsableSunElevation( in ) )
            {
                missing->append( kMissingSunElevation );
                ok = false;
            }
        }
        else if ( !hasQuantification( in ) )
        {
            missing->append( kMissingReflectanceCoefficients );
            ok = false;
        }
    }
    else if ( step == kStepRadianceToToaReflectance )
    {
        if ( !( finite( in.esun ) && in.esun > 0.0 ) )
        {
            missing->append( kMissingEsun );
            ok = false;
        }
        if ( !hasUsableSunElevation( in ) )
        {
            missing->append( kMissingSunElevation );
            ok = false;
        }
    }
    else if ( step == kStepRadianceToBrightnessTemperature )
    {
        if ( !in.coeffs || !finite( in.coeffs->k1 ) || !finite( in.coeffs->k2 )
             || in.coeffs->k1 <= 0.0 || in.coeffs->k2 <= 0.0 )
        {
            missing->append( kMissingThermalConstants );
            ok = false;
        }
    }
    else if ( step == kStepToaToSurfaceReflectance )
    {
        if ( in.atmosphericProvider.isEmpty() )
        {
            missing->append( kMissingAtmosphericProvider );
            ok = false;
        }
    }
    else
    {
        Q_ASSERT( false && "unknown step token" );
        ok = false;
    }
    return ok;
}

/// Provenance entry for one planned edge (only fields the step consumes).
Json::Value edgeProvenance( const QString &step, const StepInputs &in )
{
    Json::Value e( Json::objectValue );
    e["step"] = step.toStdString();
    if ( step == kStepDnToRadiance && in.coeffs )
    {
        e["formula"] = "L = gain*DN + bias";
        e["radiance_gain"] = in.coeffs->radianceGain;
        e["radiance_bias"] = in.coeffs->radianceBias;
    }
    else if ( step == kStepDnToToaReflectance )
    {
        if ( in.sensor == SensorType::Landsat && in.coeffs )
        {
            e["formula"] = "rho = (reflMult*DN + reflAdd)/sin(sunElevation)";
            e["reflectance_mult"] = in.coeffs->reflMult;
            e["reflectance_add"] = in.coeffs->reflAdd;
        }
        else
        {
            e["formula"] = "rho = (DN + offset)/scale";
            e["scale"] = in.coeffs ? in.coeffs->scale : 0.0;
            e["offset"] = in.coeffs ? in.coeffs->offset : 0.0;
        }
    }
    else if ( step == kStepRadianceToToaReflectance )
    {
        e["formula"] = "rho = pi*L*d^2/(ESUN*cos(sunZenith))";
        e["esun"] = in.esun;
    }
    else if ( step == kStepRadianceToBrightnessTemperature && in.coeffs )
    {
        e["formula"] = "T = K2/ln(K1/L + 1)";
        e["k1"] = in.coeffs->k1;
        e["k2"] = in.coeffs->k2;
    }
    else if ( step == kStepToaToSurfaceReflectance )
    {
        e["formula"] = "atmospheric_correction";
        e["provider"] = in.atmosphericProvider.toStdString();
    }
    if ( step == kStepDnToToaReflectance || step == kStepRadianceToToaReflectance )
    {
        e["sun_elevation_deg"] = hasUsableSunElevation( in ) ? in.sunElevationDeg : 0.0;
        e["sun_elevation_source"] = in.sunElevationFromMetadata ? "metadata" : "computed";
    }
    return e;
}
} // namespace

bool isKnownState( const QString &state )
{
    return state == kRadiometricStateDigitalNumber || state == kRadiometricStateRadiance
           || state == kRadiometricStateToaReflectance
           || state == kRadiometricStateSurfaceReflectance
           || state == kRadiometricStateBrightnessTemperature;
}

bool isLawfulEdge( const QString &from, const QString &to )
{
    if ( !isKnownState( from ) || !isKnownState( to ) )
        return false;
    if ( from == to )
        return true;
    if ( from == kRadiometricStateDigitalNumber )
        return to == kRadiometricStateRadiance || to == kRadiometricStateToaReflectance;
    if ( from == kRadiometricStateRadiance )
        return to == kRadiometricStateToaReflectance
               || to == kRadiometricStateBrightnessTemperature;
    if ( from == kRadiometricStateToaReflectance )
        return to == kRadiometricStateSurfaceReflectance;
    return false; // surface_reflectance / brightness_temperature are terminal
}

Plan plan( const QString &fromState, const QString &toState, const StepInputs &inputs )
{
    Plan p;
    if ( !isKnownState( fromState ) || !isKnownState( toState ) )
    {
        p.explanation = QStringLiteral( "radiometric_transition: unknown radiometric state "
                                       "(vocabulary: digital_number, radiance, "
                                       "toa_reflectance, surface_reflectance, "
                                       "brightness_temperature)" );
        return p;
    }

    if ( fromState == toState )
    {
        p.lawful = true;
        p.satisfiable = true;
        p.explanation = QStringLiteral( "identity: data already in state '%1'" ).arg( fromState );
        p.provenance["schema"] = "exp_rs_radiometric_provenance/1";
        p.provenance["from"] = fromState.toStdString();
        p.provenance["to"] = toState.toStdString();
        p.provenance["lawful"] = true;
        p.provenance["satisfiable"] = true;
        p.provenance["steps"] = Json::Value( Json::arrayValue );
        p.provenance["numeric_scale_before"] = inputs.numericScale;
        p.provenance["numeric_scale_after"] = inputs.numericScale;
        return p;
    }

    // Shortest lawful chains (the DAG makes each reachable pair unique).
    // The table itself decides lawfulness: multi-edge conversions (DN → BT,
    // DN/L → surface) are lawful without a direct edge, so the direct-edge
    // predicate must not gate here.
    QStringList chain;
    if ( fromState == kRadiometricStateDigitalNumber )
    {
        if ( toState == kRadiometricStateRadiance )
            chain = { kStepDnToRadiance };
        else if ( toState == kRadiometricStateToaReflectance )
            chain = { kStepDnToToaReflectance };
        else if ( toState == kRadiometricStateSurfaceReflectance )
            chain = { kStepDnToToaReflectance, kStepToaToSurfaceReflectance };
        else if ( toState == kRadiometricStateBrightnessTemperature )
            chain = { kStepDnToRadiance, kStepRadianceToBrightnessTemperature };
    }
    else if ( fromState == kRadiometricStateRadiance )
    {
        if ( toState == kRadiometricStateToaReflectance )
            chain = { kStepRadianceToToaReflectance };
        else if ( toState == kRadiometricStateBrightnessTemperature )
            chain = { kStepRadianceToBrightnessTemperature };
        else if ( toState == kRadiometricStateSurfaceReflectance )
            chain = { kStepRadianceToToaReflectance, kStepToaToSurfaceReflectance };
    }
    else if ( fromState == kRadiometricStateToaReflectance
              && toState == kRadiometricStateSurfaceReflectance )
    {
        chain = { kStepToaToSurfaceReflectance };
    }
    if ( chain.isEmpty() )
    {
        p.explanation = QStringLiteral( "radiometric_transition: %1 → %2 is not a lawful "
                                       "conversion (radiometric corrections only move forward "
                                       "along DN → radiance → TOA → surface; brightness "
                                       "temperature is terminal)" )
                            .arg( fromState, toState );
        return p;
    }

    p.lawful = true;
    p.steps = chain;

    QStringList missing;
    for ( const QString &step : chain )
        edgeSatisfiable( step, inputs, &missing );
    missing.removeDuplicates();
    p.missing = missing;
    p.satisfiable = missing.isEmpty();

    if ( p.satisfiable )
    {
        p.explanation = QStringLiteral( "%1 → %2 via %3" )
                            .arg( fromState, toState, chain.join( QStringLiteral( ", " ) ) );
    }
    else
    {
        p.explanation = QStringLiteral( "%1 → %2 refused: missing %3" )
                            .arg( fromState, toState, missing.join( QStringLiteral( ", " ) ) );
    }

    Json::Value &prov = p.provenance;
    prov["schema"] = "exp_rs_radiometric_provenance/1";
    prov["from"] = fromState.toStdString();
    prov["to"] = toState.toStdString();
    prov["lawful"] = true;
    prov["satisfiable"] = p.satisfiable;
    if ( !p.satisfiable )
    {
        Json::Value miss( Json::arrayValue );
        for ( const QString &m : missing )
            miss.append( m.toStdString() );
        prov["missing"] = miss;
    }
    Json::Value steps( Json::arrayValue );
    for ( const QString &step : chain )
        steps.append( edgeProvenance( step, inputs ) );
    prov["steps"] = steps;
    prov["numeric_scale_before"] = inputs.numericScale;
    // Conversions emit physical units; the scale is consumed by the first
    // edge (quantification form) or passes through unchanged.
    prov["numeric_scale_after"] = 1.0;
    return p;
}

} // namespace RadiometricTransition
