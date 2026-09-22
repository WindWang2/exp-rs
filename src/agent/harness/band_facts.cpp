// src/agent/harness/band_facts.cpp
#include "band_facts.h"

#include <algorithm>
#include <cctype>

namespace sicnu::agent::harness::facts {

std::string lowered( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

BandFacts bandFacts( const Json::Value &understanding )
{
  BandFacts facts;
  // band_count is a summary field some understanding docs carry alongside
  // the band arrays; count the arrays and fall back to the summary only
  // when neither array is present (otherwise the count double-counts).
  const bool hasBandArray =
    ( understanding.isMember( "bands" ) && understanding["bands"].isArray() ) ||
    ( understanding.isMember( "band_roles" ) && understanding["band_roles"].isArray() );
  facts.bandCount = hasBandArray ? 0 : understanding.get( "band_count", 0 ).asInt();
  auto window = []( const Json::Value &band, double &wavelengthNm ) {
    if ( band.isMember( "wavelength" ) && band["wavelength"].isNumeric() )
    {
      wavelengthNm = band["wavelength"].asDouble();
      const std::string units = lowered( band.get( "wavelengthUnits", "nm" ).asString() );
      if ( units == "µm" || units == "um" )
        wavelengthNm *= 1000.0;
    }
  };
  auto inWindow = []( double wavelengthNm, double low, double high ) {
    return wavelengthNm >= low && wavelengthNm <= high;
  };
  if ( understanding.isMember( "bands" ) && understanding["bands"].isArray() )
  {
    for ( const auto &band : understanding["bands"] )
    {
      ++facts.bandCount;
      const std::string role = lowered( band.get( "role", "" ).asString() );
      if ( !role.empty() )
        facts.roles.push_back( role );
      double wavelengthNm = -1;
      window( band, wavelengthNm );
      const bool isNirRole = role == "nir";
      const bool isRedRole = role == "red";
      if ( isNirRole || inWindow( wavelengthNm, 750.0, 1100.0 ) )
      {
        facts.hasNir = true;
        facts.nirByWavelength = isNirRole ? facts.nirByWavelength : true;
      }
      if ( isRedRole || ( wavelengthNm >= 600.0 && wavelengthNm < 700.0 ) )
      {
        facts.hasRed = true;
        facts.redByWavelength = isRedRole ? facts.redByWavelength : true;
      }
      if ( role == "green" || inWindow( wavelengthNm, 500.0, 600.0 ) )
        facts.hasGreen = true;
      if ( role == "blue" || inWindow( wavelengthNm, 430.0, 520.0 ) )
        facts.hasBlue = true;
      if ( role == "swir" || role == "swir1" || role == "swir2" ||
           inWindow( wavelengthNm, 1550.0, 1750.0 ) || inWindow( wavelengthNm, 2080.0, 2350.0 ) )
        facts.hasSwir = true;
      if ( role == "red_edge" || role == "rededge" || inWindow( wavelengthNm, 700.0, 745.0 ) )
        facts.hasRedEdge = true;
    }
  }
  else if ( understanding.isMember( "band_roles" ) && understanding["band_roles"].isArray() )
  {
    for ( const auto &role : understanding["band_roles"] )
    {
      ++facts.bandCount;
      const std::string r = lowered( role.asString() );
      if ( !r.empty() )
      {
        facts.roles.push_back( r );
        if ( r == "nir" )
          facts.hasNir = true;
        if ( r == "red" )
          facts.hasRed = true;
        if ( r == "green" )
          facts.hasGreen = true;
        if ( r == "blue" )
          facts.hasBlue = true;
        if ( r == "swir" || r == "swir1" || r == "swir2" )
          facts.hasSwir = true;
        if ( r == "red_edge" || r == "rededge" )
          facts.hasRedEdge = true;
      }
    }
  }
  return facts;
}

std::string crsOf( const Json::Value &understanding )
{
  const Json::Value &crs = understanding.get( "crs", Json::Value() );
  if ( crs.isString() )
    return crs.asString();
  if ( crs.isObject() )
    return crs.get( "authid", "" ).asString();
  return "";
}

// Closed geographic-authid list, shared with the workflow requires_projected
// check (workflow_analysis.cpp) so both layers agree on which CRS carry
// degree-based pixel sizes. Unknown/WKT-only CRS are deliberately not
// recognized: their units are unknown, never silently assumed to be meters.
bool isGeographicAuthid( const std::string &crs )
{
  static const char *const kGeographicAuthids[] = {
    "epsg:4326", "epsg:4269", "epsg:4258", "epsg:4610", "crs:84", "ogc:crs84",
  };
  const std::string authid = lowered( crs );
  for ( const char *geo : kGeographicAuthids )
    if ( authid == geo )
      return true;
  return false;
}

GridFacts gridFacts( const Json::Value &understanding )
{
  GridFacts facts;
  facts.crs = crsOf( understanding );
  // Shape tolerance (mirrors the GridShapeFacts review A-4 convention in
  // workflow_analysis.cpp): the inspect tools emit `size` {width,height} /
  // `pixel_size` {x,y} OBJECTS (raster_inspect_tool.cpp) while declared
  // facts use [w,h]/[x,y] ARRAYS. Both shapes are the same physical fact;
  // parsing only one of them silently zeroed every grid check on
  // inspect-derived inputs. Non-numeric members stay unknown (0) — checks
  // skip, they never fake a verdict.
  const auto readXy = []( const Json::Value &value, double &first, double &second ) {
    if ( value.isArray() && value.size() == 2 && value[0].isNumeric()
         && value[1].isNumeric() )
    {
      first = value[0].asDouble();
      second = value[1].asDouble();
      return true;
    }
    if ( value.isObject() && value["x"].isNumeric() && value["y"].isNumeric() )
    {
      first = value["x"].asDouble();
      second = value["y"].asDouble();
      return true;
    }
    return false;
  };
  const auto readWh = []( const Json::Value &value, Json::Int &first, Json::Int &second ) {
    if ( value.isArray() && value.size() == 2 && value[0].isNumeric()
         && value[1].isNumeric() )
    {
      first = value[0].asInt64();
      second = value[1].asInt64();
      return true;
    }
    if ( value.isObject() && value["width"].isNumeric() && value["height"].isNumeric() )
    {
      first = value["width"].asInt64();
      second = value["height"].asInt64();
      return true;
    }
    return false;
  };
  if ( understanding.isMember( "pixel_size" ) )
    readXy( understanding["pixel_size"], facts.pixelSizeX, facts.pixelSizeY );
  readWh( understanding.get( "size", Json::Value() ), facts.width, facts.height );
  return facts;
}

std::string radiometricState( const Json::Value &understanding )
{
  return lowered( understanding.get( "radiometric_state", "" ).asString() );
}

std::string modalityOf( const Json::Value &understanding )
{
  return lowered( understanding.get( "modality", "" ).asString() );
}

SarFacts sarFacts( const Json::Value &understanding )
{
  SarFacts facts;
  const BandFacts bands = bandFacts( understanding );
  for ( const std::string &role : bands.roles )
  {
    if ( role == "hh" || role == "vv" || role == "hv" || role == "vh" )
    {
      facts.polarization = role;
      break;
    }
  }
  const std::string state = radiometricState( understanding );
  for ( const char *domain : { "sigma0", "gamma0", "beta0", "dn" } )
  {
    if ( state.find( domain ) != std::string::npos )
    {
      facts.calibrationDeclared = true;
      facts.calibration = domain;
      break;
    }
  }
  return facts;
}

std::string acquisitionTime( const Json::Value &understanding )
{
  for ( const char *key : { "acquisition_time", "SICNU_ACQUISITION_DATE" } )
  {
    if ( understanding.isMember( key ) && understanding[key].isString() )
      return understanding[key].asString();
  }
  return std::string();
}

} // namespace sicnu::agent::harness::facts
