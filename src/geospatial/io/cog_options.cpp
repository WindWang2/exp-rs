/***************************************************************************
  geospatial/io/cog_options.cpp
  Geospatial I/O, COG & Interchange 11.0 — explicit COG production options
  on top of the preset authority.
 ***************************************************************************/

#include "geospatial/io/cog_options.h"

#include <algorithm>

namespace sicnu::geo::io
{

namespace
{

bool isPowerOfTwo( int value )
{
  return value > 0 && ( value & ( value - 1 ) ) == 0;
}

/// Replaces "KEY=VALUE" for an existing KEY in place; appends when absent.
/// The COG driver's first-match lookup makes in-place replacement the ONLY
/// override semantics that work.
void replaceOrAppend( std::vector<std::string> &options, const std::string &key,
                      const std::string &value, std::vector<std::string> *replacedKeys = nullptr )
{
  const std::string needle = key + "=";
  for ( std::string &option : options )
  {
    if ( option.rfind( needle, 0 ) == 0 )
    {
      option = needle + value;
      if ( replacedKeys )
        replacedKeys->push_back( key );
      return;
    }
  }
  options.push_back( needle + value );
}

bool hasKey( const std::vector<std::string> &options, const std::string &key )
{
  const std::string needle = key + "=";
  for ( const std::string &option : options )
  {
    if ( option.rfind( needle, 0 ) == 0 )
      return true;
  }
  return false;
}

Json::Value optionEntry( const std::string &key, const std::string &value, const char *source,
                         const char *reason )
{
  Json::Value entry;
  entry["key"] = key;
  entry["value"] = value;
  entry["source"] = source;
  entry["reason"] = reason;
  return entry;
}

} // namespace

Json::Value CogProductionPlan::toJson() const
{
  Json::Value json;
  Json::Value optionsArray( Json::arrayValue );
  for ( const std::string &option : creationOptions )
    optionsArray.append( option );
  json["creation_options"] = optionsArray;
  Json::Value warningsArray( Json::arrayValue );
  for ( const std::string &warning : warnings )
    warningsArray.append( warning );
  json["warnings"] = warningsArray;
  json["explanation"] = explanation;
  return json;
}

CogProductionPlan planCogProduction( const CogProductionOptions &settings )
{
  // Preset authority first (also enforces the dtype fidelity policy).
  const sicnu::geo::CogPresetResult presetResult =
    sicnu::geo::cogPresetOptions( settings.preset, settings.expectedDtypeName );

  CogProductionPlan plan;
  plan.creationOptions = presetResult.creationOptions;
  for ( const std::string &warning : presetResult.warnings )
    plan.warnings.push_back( warning );

  Json::Value optionsArray( Json::arrayValue );

  // 1) Blocksize override (validated before any GDAL call).
  if ( settings.blocksize != 0 )
  {
    if ( settings.blocksize < 128 || settings.blocksize > 4096 || !isPowerOfTwo( settings.blocksize ) )
    {
      Json::Value details;
      details["blocksize"] = settings.blocksize;
      details["allowed"] = "power of two, 128..4096";
      throw GeoError( ErrorCode::InvalidArgument, "COG blocksize outside the allowed range", details );
    }
    replaceOrAppend( plan.creationOptions, "BLOCKSIZE", std::to_string( settings.blocksize ) );
    optionsArray.append( optionEntry( "BLOCKSIZE", std::to_string( settings.blocksize ), "override",
                                      "explicit caller blocksize; must stay a power of two for tiled reads" ) );
  }

  // 2) Overview policy: COG driver chooses levels itself; explicit custom
  //    levels are built AFTER publish by the caller (in-place buildOverviews
  //    is the platform's one sanctioned in-place op).
  if ( !settings.buildOverviews )
  {
    replaceOrAppend( plan.creationOptions, "OVERVIEWS", "NONE" );
    optionsArray.append( optionEntry( "OVERVIEWS", "NONE", "override",
                                      "caller declined overviews inside the COG" ) );
  }
  else
  {
    optionsArray.append( optionEntry( "OVERVIEWS", "AUTO", "preset",
                                      "driver-chosen overview stack, tiled per the COG spec" ) );
  }

  // 3) Determinism: pin worker count (and DEFLATE level where DEFLATE).
  if ( settings.deterministic )
  {
    if ( settings.deflateLevel < 1 || settings.deflateLevel > 9 )
    {
      Json::Value details;
      details["deflate_level"] = settings.deflateLevel;
      throw GeoError( ErrorCode::InvalidArgument, "DEFLATE level outside 1..9", details );
    }
    replaceOrAppend( plan.creationOptions, "NUM_THREADS", "1" );
    optionsArray.append( optionEntry( "NUM_THREADS", "1", "override",
                                      "deterministic mode: single-threaded compression gives a stable byte layout" ) );
    if ( hasKey( plan.creationOptions, "COMPRESS" ) )
    {
      const bool deflate = std::any_of( plan.creationOptions.begin(), plan.creationOptions.end(),
                                        []( const std::string &option ) { return option == "COMPRESS=DEFLATE"; } );
      if ( deflate )
      {
        replaceOrAppend( plan.creationOptions, "LEVEL", std::to_string( settings.deflateLevel ) );
        optionsArray.append( optionEntry( "LEVEL", std::to_string( settings.deflateLevel ), "override",
                                          "deterministic mode: pinned DEFLATE level" ) );
      }
      else
      {
        plan.warnings.push_back(
          "deterministic=true with a non-DEFLATE compressor: NUM_THREADS is pinned, but other "
          "compressor settings may still vary the byte layout" );
      }
    }
  }

  // 4) Caller extras: replace-or-append with a warning per replaced key.
  for ( const std::string &extra : settings.extraCreationOptions )
  {
    const std::size_t eq = extra.find( '=' );
    if ( eq == std::string::npos || eq == 0 )
    {
      Json::Value details;
      details["option"] = extra;
      throw GeoError( ErrorCode::InvalidArgument, "creation option must be KEY=VALUE", details );
    }
    const std::string key = extra.substr( 0, eq );
    const std::string value = extra.substr( eq + 1 );
    std::vector<std::string> replaced;
    replaceOrAppend( plan.creationOptions, key, value, &replaced );
    if ( !replaced.empty() )
    {
      plan.warnings.push_back( "creation option '" + key + "' overrides the preset value" );
      optionsArray.append( optionEntry( key, value, "override", "caller creation option replaced the preset value" ) );
    }
    else
    {
      optionsArray.append( optionEntry( key, value, "caller", "additional driver creation option" ) );
    }
  }

  Json::Value determinism;
  determinism["pinned"] = settings.deterministic;
  determinism["scope"] = "byte-identical output for identical input within one GDAL/libtiff build; "
                         "no cross-platform byte guarantee";
  plan.explanation["determinism"] = determinism;
  plan.explanation["options"] = optionsArray;
  plan.explanation["preset"] = sicnu::geo::cogPresetName( settings.preset );
  return plan;
}

} // namespace sicnu::geo::io
