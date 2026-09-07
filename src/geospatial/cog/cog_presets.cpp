/***************************************************************************
  geospatial/cog/cog_presets.cpp
  Geospatial I/O Foundation 4.0 — safe COG creation presets.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/cog/cog_presets.h"

#include <algorithm>
#include <cctype>

namespace sicnu::geo
{
namespace
{

bool isFloatDtype( const std::string &dtype )
{
  std::string lower;
  lower.reserve( dtype.size() );
  for ( const char c : dtype )
    lower.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );
  return lower == "float32" || lower == "float64" || lower == "cfloat32" || lower == "cfloat64";
}

bool isIntegerDtype( const std::string &dtype )
{
  return !isFloatDtype( dtype ) && !dtype.empty();
}

CogPresetResult baseResult( std::initializer_list<const char *> options )
{
  CogPresetResult result;
  for ( const char *option : options )
    result.creationOptions.emplace_back( option );
  // Common policy: BigTIFF when size demands it, overviews for every level,
  // all-thread encoding.
  result.creationOptions.emplace_back( "BIGTIFF=IF_SAFER" );
  result.creationOptions.emplace_back( "OVERVIEWS=AUTO" );
  result.creationOptions.emplace_back( "NUM_THREADS=ALL_CPUS" );
  return result;
}

} // namespace

const char *cogPresetName( CogPreset preset )
{
  switch ( preset )
  {
    case CogPreset::LosslessScientific: return "lossless_scientific";
    case CogPreset::Visualization: return "visualization";
    case CogPreset::Categorical: return "categorical";
    case CogPreset::ContinuousFloat: return "continuous_float";
    case CogPreset::Sar: return "sar";
  }
  return "unknown";
}

Json::Value CogPresetResult::toJson() const
{
  Json::Value json;
  Json::Value options( Json::arrayValue );
  for ( const std::string &option : creationOptions )
    options.append( option );
  json["creation_options"] = options;
  Json::Value warningsJson( Json::arrayValue );
  for ( const std::string &warning : warnings )
    warningsJson.append( warning );
  json["warnings"] = warningsJson;
  return json;
}

CogPresetResult cogPresetOptions( CogPreset preset, const std::string &expectedDtypeName )
{
  switch ( preset )
  {
    case CogPreset::LosslessScientific:
    {
      CogPresetResult result = baseResult( { "COMPRESS=DEFLATE", "LEVEL=6", "TILING_SCHEME=Custom", "BLOCKSIZE=512" } );
      if ( isFloatDtype( expectedDtypeName ) )
        result.creationOptions.emplace_back( "PREDICTOR=3" );
      else
        result.creationOptions.emplace_back( "PREDICTOR=2" );
      result.warnings.emplace_back( "lossless preset: pixels are bit-exact; file size may exceed lossy alternatives" );
      return result;
    }
    case CogPreset::Visualization:
    {
      CogPresetResult result = baseResult( { "COMPRESS=JPEG", "QUALITY=75", "PHOTOMETRIC=YCbCr",
                                             "OVERVIEW_QUALITY=50", "TILING_SCHEME=Custom", "BLOCKSIZE=512" } );
      if ( !isIntegerDtype( expectedDtypeName ) || expectedDtypeName != "Byte" )
        result.warnings.emplace_back( "JPEG compression requires Byte data; the COG driver demotes or refuses "
                                      "other dtypes — scientific products must not use this preset" );
      result.warnings.emplace_back( "LOSSY preset: visualization products only; never for scientific analysis" );
      return result;
    }
    case CogPreset::Categorical:
    {
      if ( isFloatDtype( expectedDtypeName ) )
      {
        Json::Value details;
        details["dtype"] = expectedDtypeName;
        details["preset"] = cogPresetName( preset );
        details["policy"] = "categorical data is integer-coded; float input indicates a semantic mismatch";
        throw GeoError( ErrorCode::FidelityLoss, "Categorical COG preset refused for floating-point data", details );
      }
      CogPresetResult result = baseResult( { "COMPRESS=LZW", "TILING_SCHEME=Custom", "BLOCKSIZE=512" } );
      // No predictor: class codes are not continuous, prediction adds nothing
      // and can interact badly with palette semantics.
      result.warnings.emplace_back( "categorical preset is lossless by policy — lossy compression is rejected "
                                    "for class/QA products" );
      return result;
    }
    case CogPreset::ContinuousFloat:
    {
      if ( !isFloatDtype( expectedDtypeName ) )
      {
        CogPresetResult fallback = baseResult( { "COMPRESS=DEFLATE", "LEVEL=6", "PREDICTOR=2",
                                                 "TILING_SCHEME=Custom", "BLOCKSIZE=512" } );
        fallback.warnings.emplace_back( "continuous_float preset applied to non-float data; switched to the "
                                        "horizontal predictor (lossless, still bit-exact)" );
        return fallback;
      }
      CogPresetResult result = baseResult( { "COMPRESS=DEFLATE", "LEVEL=6", "PREDICTOR=3",
                                             "TILING_SCHEME=Custom", "BLOCKSIZE=512" } );
      result.warnings.emplace_back( "float predictor (3): lossless, best for continuous float grids" );
      return result;
    }
    case CogPreset::Sar:
    {
      CogPresetResult result = baseResult( { "COMPRESS=DEFLATE", "LEVEL=6", "TILING_SCHEME=Custom", "BLOCKSIZE=512" } );
      if ( isFloatDtype( expectedDtypeName ) )
        result.creationOptions.emplace_back( "PREDICTOR=3" );
      result.warnings.emplace_back( "SAR preset stores amplitude/intensity as declared; complex products should "
                                    "convert to amplitude before COG (the COG driver does not carry CInt16)" );
      return result;
    }
  }
  throw GeoError( ErrorCode::InvalidArgument, "Unknown COG preset" );
}

CogPresetResult cogPresetOptionsForJson( const Json::Value &presetSpec, const std::string &expectedDtypeName )
{
  const std::string name = presetSpec.asString();
  if ( name == cogPresetName( CogPreset::LosslessScientific ) )
    return cogPresetOptions( CogPreset::LosslessScientific, expectedDtypeName );
  if ( name == cogPresetName( CogPreset::Visualization ) )
    return cogPresetOptions( CogPreset::Visualization, expectedDtypeName );
  if ( name == cogPresetName( CogPreset::Categorical ) )
    return cogPresetOptions( CogPreset::Categorical, expectedDtypeName );
  if ( name == cogPresetName( CogPreset::ContinuousFloat ) )
    return cogPresetOptions( CogPreset::ContinuousFloat, expectedDtypeName );
  if ( name == cogPresetName( CogPreset::Sar ) )
    return cogPresetOptions( CogPreset::Sar, expectedDtypeName );
  Json::Value details;
  details["preset"] = name;
  throw GeoError( ErrorCode::InvalidArgument, "Unknown COG preset", details );
}

} // namespace sicnu::geo
