// src/operators/runtime/eo_preflight.cpp
#include "eo_preflight.h"

#include "operators/framework/rs_operator_error.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <algorithm>
#include <cmath>

namespace sicnu::operators::runtime {

using sicnu::operators::ErrorCode;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorError;

namespace {

std::string describeWindow( const sicnu::operators::ModelWavelengthWindowNm &w )
{
  return "[" + std::to_string( w.minNm ) + ", " + std::to_string( w.maxNm ) + "] nm";
}

bool gsdOutsideRecommended( const ModelInfo &model, double gsd, std::string *why )
{
  if ( model.minResolutionMeters >= 0.0 && gsd < model.minResolutionMeters )
  {
    *why = "input GSD " + std::to_string( gsd ) + " m is finer than the model's recommended "
             "minimum " + std::to_string( model.minResolutionMeters ) + " m";
    return true;
  }
  if ( model.maxResolutionMeters >= 0.0 && gsd > model.maxResolutionMeters )
  {
    *why = "input GSD " + std::to_string( gsd ) + " m is coarser than the model's recommended "
             "maximum " + std::to_string( model.maxResolutionMeters ) + " m";
    return true;
  }
  return false;
}

} // namespace

Json::Value EoPreflightReport::toJson() const
{
  Json::Value out( Json::objectValue );
  out["applicable"] = applicable;
  out["calibration_verified"] = calibrationVerified;
  Json::Value checksJson( Json::arrayValue );
  for ( const std::string &c : checks )
    checksJson.append( c );
  out["checks"] = checksJson;
  Json::Value advisoriesJson( Json::arrayValue );
  for ( const std::string &a : advisories )
    advisoriesJson.append( a );
  out["advisories"] = advisoriesJson;
  return out;
}

EoPreflightReport enforceEoPreflight( const ModelInfo &model, const std::string &rasterPath )
{
  EoPreflightReport report;
  if ( !model.eo.declared )
    return report;
  report.applicable = true;

  const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( rasterPath );
  if ( meta.isNull() )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "eo preflight could not read raster metadata: " + rasterPath );

  // 1) Calibration requirement — enforced => fail-closed verification.
  if ( !model.eo.calibrationState.empty() && model.eo.calibrationState != "any" )
  {
    if ( model.eo.calibrationEnforced )
    {
      if ( meta.radiometricState.empty() )
        throw RSOperatorError(
          ErrorCode::InvalidInputData,
          "model requires radiometric state '" + model.eo.calibrationState
            + "' (eo.calibration.enforced) but the input carries no SICNU_RADIOMETRIC_STATE "
            + "declaration — run the calibration/atmospheric operator first, or drop the "
            + "enforcement in the manifest. Unverified radiometric domains are never fed to "
            + "the model.",
          [&] {
            Json::Value d;
            d["required_state"] = model.eo.calibrationState;
            d["input_path"] = rasterPath;
            return d;
          }() );
      if ( meta.radiometricState != model.eo.calibrationState )
        throw RSOperatorError(
          ErrorCode::InvalidInputData,
          "model requires radiometric state '" + model.eo.calibrationState
            + "' (eo.calibration.enforced) but the input declares '" + meta.radiometricState
            + "' — recalibrate the input or use a matching model; a radiometric domain shift "
            + "is never silently tolerated",
          [&] {
            Json::Value d;
            d["required_state"] = model.eo.calibrationState;
            d["input_state"] = meta.radiometricState;
            d["input_path"] = rasterPath;
            return d;
          }() );
      report.calibrationVerified = true;
      report.checks.push_back( "calibration: input declares '" + meta.radiometricState
                               + "' (required '" + model.eo.calibrationState + "') — verified" );
    }
    else
    {
      // Advisory requirement: log the mismatch, never refuse.
      if ( meta.radiometricState.empty() )
        report.advisories.push_back( "calibration: model prefers '" + model.eo.calibrationState
                                     + "' but the input declares no radiometric state" );
      else if ( meta.radiometricState != model.eo.calibrationState )
        report.advisories.push_back( "calibration: model prefers '" + model.eo.calibrationState
                                     + "' but the input declares '" + meta.radiometricState + "'" );
      else
        report.checks.push_back( "calibration: input declares '" + meta.radiometricState
                                 + "' (preferred) — matched" );
    }
  }

  // 2) Per-band-role wavelength windows — refuse only when both sides declare.
  if ( !model.eo.wavelengthsNm.empty() && !meta.bands.empty() )
  {
    for ( const ModelInputContract &feed : model.inputs )
    {
      for ( std::size_t roleIdx = 0; roleIdx < feed.bandRoles.size(); ++roleIdx )
      {
        const std::string role = [&] {
          std::string r = feed.bandRoles[roleIdx];
          std::transform( r.begin(), r.end(), r.begin(),
                          []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
          return r;
        }();
        const auto windowIt = model.eo.wavelengthsNm.find( role );
        if ( windowIt == model.eo.wavelengthsNm.end() )
          continue;
        const std::size_t bandIdx = roleIdx; // band_roles[i] maps to input band i+1
        if ( bandIdx >= meta.bands.size() )
        {
          report.advisories.push_back( "wavelength: role '" + role + "' maps to band "
                                       + std::to_string( bandIdx + 1 ) + " but the input has only "
                                       + std::to_string( meta.bands.size() ) + " bands" );
          continue;
        }
        const sicnu::geo::BandInfo &band = meta.bands[bandIdx];
        if ( !band.hasWavelength || band.wavelengthNm <= 0.0 )
        {
          report.advisories.push_back( "wavelength: role '" + role
                                       + "' window " + describeWindow( windowIt->second )
                                       + " unchecked — input band " + std::to_string( bandIdx + 1 )
                                       + " declares no center wavelength" );
          continue;
        }
        if ( band.wavelengthNm < windowIt->second.minNm
             || band.wavelengthNm > windowIt->second.maxNm )
          throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "input band " + std::to_string( bandIdx + 1 ) + " (role '" + role + "') centers at "
              + std::to_string( band.wavelengthNm ) + " nm, outside the model's declared "
              "sensitivity window " + describeWindow( windowIt->second )
              + " — the model would misread this band silently; use a sensor whose band matches "
              "the manifest's eo.wavelengths_nm",
            [&] {
              Json::Value d;
              d["role"] = role;
              d["band"] = static_cast<Json::Int64>( bandIdx + 1 );
              d["input_wavelength_nm"] = band.wavelengthNm;
              d["window_min_nm"] = windowIt->second.minNm;
              d["window_max_nm"] = windowIt->second.maxNm;
              return d;
            }() );
        report.checks.push_back( "wavelength: role '" + role + "' at "
                                 + std::to_string( band.wavelengthNm ) + " nm within "
                                 + describeWindow( windowIt->second ) );
      }
    }
  }

  // 3) CRS-family grid assumption — enforced vocabulary ("" / "any" skip).
  if ( !model.eo.crsFamily.empty() && model.eo.crsFamily != "any" )
  {
    const bool geographicRequired = model.eo.crsFamily == "geographic";
    const bool matched = geographicRequired ? meta.crs.isGeographic : meta.crs.isProjected;
    if ( matched )
      report.checks.push_back( "grid: input CRS family '" + model.eo.crsFamily + "' matched" );
    else
      report.advisories.push_back( "grid: model assumes a " + model.eo.crsFamily
                                   + " CRS; the input CRS does not declare that family" );
  }

  // 4) GSD/resolution range stays the historical advisory fact.
  if ( meta.hasGsd && meta.gsd > 0.0 )
  {
    std::string why;
    if ( gsdOutsideRecommended( model, meta.gsd, &why ) )
      report.advisories.push_back( "resolution: " + why + " (advisory; ranking fact)" );
    else
      report.checks.push_back( "resolution: input GSD " + std::to_string( meta.gsd )
                               + " m within the recommended range" );
  }

  return report;
}

} // namespace sicnu::operators::runtime
