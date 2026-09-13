/***************************************************************************
  geospatial/products/cn_product_metadata.cpp
  Geospatial I/O Foundation 4.0 — Chinese satellite product metadata.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  CRESDA-family L1A sidecar XML (GF-1/2/6, ZY-3, HJ-1A/1B CCD) is parsed with
  a small path-aware expat scanner over a whitelist of tags (no Qt, no regex
  guesswork). Sidecars are found beside the image or inside the product
  directory; nothing is ever fetched from the network.

  The band→role vocabulary is data-driven from data/products/band_roles/*.json
  (ADR 0146): the loader is fail-closed, a missing table is a structured
  error — never a silent "unknown".
 ***************************************************************************/

#include "geospatial/products/cn_product_metadata.h"

#include "geospatial/common.h"

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>
#include <expat.h>

namespace sicnu::geo
{

namespace
{

namespace fs = std::filesystem;

std::string upperAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
  return text;
}

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

std::string trimText( const std::string &text )
{
  std::size_t begin = 0;
  std::size_t end = text.size();
  while ( begin < end && std::isspace( static_cast<unsigned char>( text[begin] ) ) )
    ++begin;
  while ( end > begin && std::isspace( static_cast<unsigned char>( text[end - 1] ) ) )
    --end;
  return text.substr( begin, end - begin );
}

std::string baseNameOf( const std::string &path )
{
  const std::size_t slash = path.find_last_of( "/\\" );
  return slash == std::string::npos ? path : path.substr( slash + 1 );
}

std::string parentOf( const std::string &path )
{
  const std::size_t slash = path.find_last_of( "/\\" );
  return slash == std::string::npos ? std::string() : path.substr( 0, slash );
}

std::string stemOf( const std::string &fileName )
{
  const std::size_t dot = fileName.rfind( '.' );
  return dot == std::string::npos ? fileName : fileName.substr( 0, dot );
}

std::string extensionOfLower( const std::string &fileName )
{
  const std::size_t dot = fileName.rfind( '.' );
  return dot == std::string::npos ? std::string() : lowerAscii( fileName.substr( dot ) );
}

bool fileExistsLocal( const std::string &path )
{
  std::error_code ec;
  return fs::exists( fs::u8path( path ), ec );
}

bool isDirectoryLocal( const std::string &path )
{
  std::error_code ec;
  return fs::is_directory( fs::u8path( path ), ec );
}

std::vector<std::string> listDirectoryBounded( const std::string &dir, int maxEntries = 512 )
{
  std::vector<std::string> names;
  std::error_code ec;
  fs::directory_iterator it( fs::u8path( dir ), ec );
  if ( ec )
    return names;
  for ( fs::directory_iterator end; it != end && static_cast<int>( names.size() ) < maxEntries;
        it.increment( ec ) )
  {
    if ( ec )
      break;
    const std::u8string u8 = it->path().generic_u8string();
    names.emplace_back( reinterpret_cast<const char *>( u8.data() ), u8.size() );
  }
  return names;
}

bool readFileText( const std::string &path, std::string &out )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in.is_open() )
    return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

bool parseDouble( const std::string &text, double &value )
{
  try
  {
    std::size_t consumed = 0;
    const double parsed = std::stod( trimText( text ), &consumed );
    if ( consumed == 0 || !std::isfinite( parsed ) )
      return false;
    value = parsed;
    return true;
  }
  catch ( const std::exception & )
  {
    return false;
  }
}

// ─── Path-aware XML scanner ─────────────────────────────────────────────────

/// Watches lowercase tag paths. A pattern without "::" matches any depth by
/// last segment; a pattern with "::" matches the suffix of the element path
/// (e.g. "sunposgeodetic::elevation" matches <SunPosGeodetic><Elevation> at
/// any depth but not an unrelated <Elevation> elsewhere).
struct XmlPathScan
{
  std::vector<std::string> watched;
  std::map<std::string, std::string> values;       // pattern → first text
  std::map<std::string, std::vector<std::string>> listValues; // pattern → all texts
  std::vector<std::string> pathStack;
  std::string currentPattern;
  std::ostringstream buffer;
};

std::string joinPathSegments( const std::vector<std::string> &segments )
{
  std::string joined;
  for ( const std::string &segment : segments )
  {
    if ( !joined.empty() )
      joined += "::";
    joined += segment;
  }
  return joined;
}

bool patternMatches( const std::string &pattern, const std::vector<std::string> &pathStack )
{
  const std::size_t sep = pattern.rfind( "::" );
  if ( sep == std::string::npos )
    return !pathStack.empty() && pathStack.back() == pattern;
  const std::string patternTail = pattern.substr( sep + 2 );
  if ( pathStack.empty() || pathStack.back() != patternTail )
    return false;
  // The parent part must also appear as a path suffix.
  std::vector<std::string> parents( pathStack.begin(), pathStack.end() - 1 );
  const std::string parentPattern = pattern.substr( 0, sep );
  return patternMatches( parentPattern, parents );
}

void xmlPathStart( void *user, const XML_Char *element, const XML_Char ** )
{
  auto *scan = static_cast<XmlPathScan *>( user );
  scan->pathStack.push_back( lowerAscii( element ? element : "" ) );
  scan->buffer.str( std::string() );
  scan->buffer.clear();
  scan->currentPattern.clear();
  for ( const std::string &pattern : scan->watched )
  {
    if ( patternMatches( pattern, scan->pathStack ) )
    {
      scan->currentPattern = pattern;
      break;
    }
  }
}

void xmlPathEnd( void *user, const XML_Char * )
{
  auto *scan = static_cast<XmlPathScan *>( user );
  if ( !scan->currentPattern.empty() )
  {
    const std::string text = trimText( scan->buffer.str() );
    if ( !text.empty() )
    {
      if ( !scan->values.count( scan->currentPattern ) )
        scan->values[scan->currentPattern] = text;
      scan->listValues[scan->currentPattern].push_back( text );
    }
  }
  scan->currentPattern.clear();
  if ( !scan->pathStack.empty() )
    scan->pathStack.pop_back();
}

void xmlPathChars( void *user, const XML_Char *data, int length )
{
  auto *scan = static_cast<XmlPathScan *>( user );
  scan->buffer.write( data, length );
}

XmlPathScan scanXmlPaths( const std::string &path, std::vector<std::string> watchedPatterns )
{
  std::string text;
  if ( !readFileText( path, text ) )
    throw GeoError( ErrorCode::OpenFailed, "Cannot read product metadata: " + path );

  XmlPathScan scan;
  scan.watched = std::move( watchedPatterns );
  XML_Parser parser = XML_ParserCreate( nullptr );
  if ( !parser )
    throw GeoError( ErrorCode::OpenFailed, "Cannot allocate XML parser for " + path );
  XML_SetUserData( parser, &scan );
  XML_SetElementHandler( parser, &xmlPathStart, &xmlPathEnd );
  XML_SetCharacterDataHandler( parser, &xmlPathChars );
  if ( XML_Parse( parser, text.data(), static_cast<int>( text.size() ), 1 ) == XML_STATUS_ERROR )
  {
    const XML_LChar *error = XML_ErrorString( XML_GetErrorCode( parser ) );
    XML_ParserFree( parser );
    throw GeoError( ErrorCode::InvalidArgument,
                    std::string( "Malformed product XML: " ) +
                      ( error ? reinterpret_cast<const char *>( error ) : "parse error" ) );
  }
  XML_ParserFree( parser );
  return scan;
}

std::string scanText( const XmlPathScan &scan, const char *pattern )
{
  const auto it = scan.values.find( pattern );
  return it == scan.values.end() ? std::string() : it->second;
}

// ─── Identity ───────────────────────────────────────────────────────────────

struct FamilyPattern
{
  const char *prefix;      // filename prefix (uppercased path contains)
  const char *satellite;
  const char *sensorMode;
  const char *sensorKey;
  const char *kindName;
};

const FamilyPattern kSupportedPatterns[] = {
  { "GF1_PMS", "GF1", "PMS", "gf1_pms", "gaofen_product" },
  { "GF1_WFV", "GF1", "WFV", "gf1_wfv", "gaofen_product" },
  { "GF2_PMS", "GF2", "PMS", "gf2_pms", "gaofen_product" },
  { "GF6_PMS", "GF6", "PMS", "gf6_pms", "gaofen_product" },
  { "GF6_WFV", "GF6", "WFV", "gf6_wfv", "gaofen_product" },
  { "ZY3_TLC", "ZY3", "TLC", "zy3_pan", "zy3_product" },
  { "ZY3_NAD", "ZY3", "NAD", "zy3_nad_ms", "zy3_product" },
  { "ZY3_FWD", "ZY3", "FWD", "zy3_pan", "zy3_product" },
  { "ZY3_BWD", "ZY3", "BWD", "zy3_pan", "zy3_product" },
  { "HJ1A-CCD", "HJ1A", "CCD", "hj_ccd", "hj_ccd_product" },
  { "HJ1B-CCD", "HJ1B", "CCD", "hj_ccd", "hj_ccd_product" },
};

/// Reasons for CN-family names we deliberately refuse (DECISIONS D-01:
/// fixed support set, diagnosable refusal, no best-effort guessing). Tokens
/// are checked against the file name and against separator-anchored path
/// fragments so a random "AGF3..." directory never trips them.
bool unsupportedFamilyReason( const std::string &upper, std::string &reason )
{
  const std::string base = baseNameOf( upper );
  auto pathHas = [ &upper, &base ] ( const char *token ) {
    if ( base.rfind( token, 0 ) == 0 )
      return true;
    return upper.find( token ) != std::string::npos;
  };

  if ( pathHas( "GF3_" ) || pathHas( "GF3-" ) )
  {
    reason = "Gaofen-3 (GF-3) is a SAR mission; only GF-1/2/6 PMS/WFV optical "
             "products are adapted";
    return true;
  }
  if ( pathHas( "GF4_" ) || pathHas( "GF4-" ) )
  {
    reason = "Gaofen-4 (GF-4) geostationary products are not adapted";
    return true;
  }
  if ( pathHas( "GF5_" ) || pathHas( "GF5-" ) )
  {
    reason = "Gaofen-5 (GF-5) hyperspectral/AHSI products are not adapted";
    return true;
  }
  if ( pathHas( "GF7_" ) || pathHas( "GF7-" ) )
  {
    reason = "Gaofen-7 (GF-7) stereo mapping products are not adapted";
    return true;
  }
  if ( base.rfind( "ZY1", 0 ) == 0 || base.rfind( "ZY5", 0 ) == 0 ||
       upper.find( "ZY1_" ) != std::string::npos || upper.find( "ZY5_" ) != std::string::npos )
  {
    reason = "only ZY-3 TLC/NAD/FWD/BWD products are adapted";
    return true;
  }
  if ( base.rfind( "HJ2", 0 ) == 0 || upper.find( "HJ2A-" ) != std::string::npos ||
       upper.find( "HJ2B-" ) != std::string::npos )
  {
    reason = "Huanjing-2 (HJ-2) SAR products are not adapted; only HJ-1A/1B CCD";
    return true;
  }
  if ( ( base.rfind( "HJ1A", 0 ) == 0 || base.rfind( "HJ1B", 0 ) == 0 ) &&
       base.find( "-IRS" ) != std::string::npos )
  {
    reason = "HJ-1A/1B IRS infrared camera products are not adapted; only CCD";
    return true;
  }
  if ( base.rfind( "CBERS", 0 ) == 0 || upper.find( "CBERS_" ) != std::string::npos )
  {
    reason = "CBERS products are not adapted";
    return true;
  }
  return false;
}

/// Band-order table key for ZY-3 NAD: the same sensor id carries both a
/// panchromatic and a multispectral product; the sidecar's declared band
/// count decides (1 band → pan, ≥2 → multispectral). Never guessed from
/// band numbers — from the declared band inventory.
std::string zy3NadKey( const ProductMetadata &metadata )
{
  if ( metadata.declaredBandIds.size() == 1 )
    return "zy3_pan";
  return "zy3_nad_ms";
}

std::string extraValue( const ProductMetadata &metadata, const char *key )
{
  for ( const auto &entry : metadata.extra )
  {
    if ( entry.first == key )
      return entry.second;
  }
  return std::string();
}

// ─── CRESDA sidecar parsing ─────────────────────────────────────────────────

bool looksLikeCresdaXml( const XmlPathScan &scan )
{
  return !scanText( scan, "satelliteid" ).empty() || !scanText( scan, "productid" ).empty();
}

/// Normalizes a declared receive date/time into ISO-8601. Accepts
/// "2017-08-23" + "11:56:47(.ms)" or a full ISO timestamp in one tag.
std::string normalizeAcquisitionTime( const std::string &date, const std::string &time )
{
  const std::string isoDate = trimText( date );
  std::string isoTime = trimText( time );
  if ( isoDate.empty() && isoTime.empty() )
    return std::string();
  if ( isoDate.empty() )
    return isoTime; // full ISO declared in the time tag
  if ( isoTime.empty() )
    return isoDate;
  if ( isoTime.find( 'T' ) != std::string::npos )
    return isoDate + "T" + isoTime.substr( isoTime.find( 'T' ) + 1 );
  return isoDate + "T" + isoTime;
}

ProductMetadata parseCresdaXml( const std::string &xmlPath, const CnProductIdentity &identity )
{
  const XmlPathScan scan = scanXmlPaths( xmlPath, {
                                                    "productid",
                                                    "satelliteid",
                                                    "sensorid",
                                                    "modeid",
                                                    "productlevel",
                                                    "receivetimedate",
                                                    "receivedate",
                                                    "receivetime",
                                                    "stopdate",
                                                    "stoptime",
                                                    "width",
                                                    "height",
                                                    "pixelsizex",
                                                    "pixelsizey",
                                                    "groundresolution",
                                                    "resolution",
                                                    "cloudpercent",
                                                    "cloudcoveragepercentage",
                                                    "orbitid",
                                                    "bandid",
                                                    "sunposgeodetic::azimuth",
                                                    "sunposgeodetic::elevation",
                                                    "sunelevation",
                                                    "sunzenithangle",
                                                    "sunazimuth",
                                                    "gainval",
                                                    "offsetval",
                                                    "bandcalibration::gain",
                                                    "bandcalibration::offset",
                                                    "bandcalibration::bias",
                                                  } );

  // Guard the fabrication path (mirrors the Sentinel-2 adapter): a random
  // XML must never be stamped with CN optical semantics.
  if ( !looksLikeCresdaXml( scan ) )
  {
    Json::Value details;
    details["xml"] = xmlPath;
    throw GeoError( ErrorCode::UnsupportedProduct,
                    "Sidecar does not declare a CRESDA product (no SatelliteID/ProductID)", details );
  }

  ProductMetadata product;
  product.productId = scanText( scan, "productid" );
  product.platform = upperAscii( scanText( scan, "satelliteid" ) );
  if ( product.platform.empty() )
    product.platform = identity.satellite;
  product.sensor = scanText( scan, "sensorid" );
  product.sensorMode = scanText( scan, "sensorid" );
  if ( product.sensorMode.empty() )
    product.sensorMode = identity.sensorMode;
  const std::string modeId = upperAscii( scanText( scan, "modeid" ) );
  product.modality = "optical";
  // CRESDA L1A products deliver digital numbers; TOA reflectance is computed
  // downstream from the (published or declared) calibration coefficients.
  product.radiometricState = "digital_number";

  product.acquisitionTime = normalizeAcquisitionTime( scanText( scan, "receivedate" ),
                                                      scanText( scan, "receivetime" ) );
  if ( product.acquisitionTime.empty() )
    product.acquisitionTime = normalizeAcquisitionTime( scanText( scan, "receivetimedate" ), std::string() );

  // Processing level: declared tag wins, otherwise the L1A token in the
  // product id (the CRESDA distribution naming) — never a default.
  product.processingLevel = scanText( scan, "productlevel" );
  if ( product.processingLevel.empty() )
  {
    const std::string upperId = upperAscii( product.productId );
    if ( upperId.find( "L1A" ) != std::string::npos )
      product.processingLevel = "L1A";
  }

  const std::string cloud = scanText( scan, "cloudpercent" ).empty()
                              ? scanText( scan, "cloudcoveragepercentage" )
                              : scanText( scan, "cloudpercent" );
  if ( !cloud.empty() )
  {
    double parsed = 0.0;
    if ( parseDouble( cloud, parsed ) )
    {
      product.cloudCover = parsed;
      product.hasCloudCover = true;
    }
  }

  for ( const char *sizeTag : { "pixelsizex", "groundresolution", "resolution" } )
  {
    const std::string sizeText = scanText( scan, sizeTag );
    double parsed = 0.0;
    if ( !sizeText.empty() && parseDouble( sizeText, parsed ) && parsed > 0.0 )
    {
      product.resolutionMeters = parsed;
      product.hasResolution = true;
      break;
    }
  }

  const std::string sunElevation = scanText( scan, "sunposgeodetic::elevation" ).empty()
                                     ? scanText( scan, "sunelevation" )
                                     : scanText( scan, "sunposgeodetic::elevation" );
  if ( !sunElevation.empty() )
  {
    double parsed = 0.0;
    if ( parseDouble( sunElevation, parsed ) )
    {
      product.sunElevationDeg = parsed;
      product.hasSunElevation = true;
    }
  }
  const std::string sunAzimuth = scanText( scan, "sunposgeodetic::azimuth" ).empty()
                                   ? scanText( scan, "sunazimuth" )
                                   : scanText( scan, "sunposgeodetic::azimuth" );
  if ( !sunAzimuth.empty() )
  {
    double parsed = 0.0;
    if ( parseDouble( sunAzimuth, parsed ) )
    {
      product.sunAzimuthDeg = parsed;
      product.hasSunAzimuth = true;
    }
  }
  ( void )scanText( scan, "sunzenithangle" ); // watched for diagnostics only

  product.orbitId = scanText( scan, "orbitid" );

  // Declared band inventory, in sidecar order (drives band-role lookup and
  // the band index inside the multi-band TIFF).
  for ( const std::string &band : scan.listValues["bandid"] )
    product.declaredBandIds.push_back( band );

  // Declared calibration: flat comma lists (GainVal/OffsetVal) or per-band
  // elements (BandCalibration::Gain/Offset/Bias). Whatever the sidecar
  // declares is carried verbatim; nothing is defaulted (DECISIONS D-06).
  auto parseCalibrationList = [] ( const std::string &text, std::vector<double> &out ) {
    std::istringstream stream( text );
    std::string item;
    while ( std::getline( stream, item, ',' ) )
    {
      double parsed = 0.0;
      if ( parseDouble( item, parsed ) )
        out.push_back( parsed );
      else
        out.push_back( std::numeric_limits<double>::quiet_NaN() );
    }
  };

  std::vector<double> gains;
  std::vector<double> offsets;
  const std::string gainValText = scanText( scan, "gainval" );
  const std::string offsetValText = scanText( scan, "offsetval" );
  bool hasFlatGain = false;
  bool hasFlatOffset = false;
  if ( !gainValText.empty() )
  {
    parseCalibrationList( gainValText, gains );
    hasFlatGain = !gains.empty();
  }
  if ( !offsetValText.empty() )
  {
    parseCalibrationList( offsetValText, offsets );
    hasFlatOffset = !offsets.empty();
  }
  const std::vector<std::string> &gainList = scan.listValues["bandcalibration::gain"];
  const std::vector<std::string> &offsetList = scan.listValues["bandcalibration::offset"];
  const std::vector<std::string> &biasList = scan.listValues["bandcalibration::bias"];

  const std::size_t bandCount = product.declaredBandIds.empty() ? 0 : product.declaredBandIds.size();
  const bool anyCalibration = hasFlatGain || hasFlatOffset || !gainList.empty() ||
                              !offsetList.empty() || !biasList.empty();
  if ( anyCalibration && bandCount == 0 )
  {
    Json::Value details;
    details["xml"] = xmlPath;
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sidecar declares calibration coefficients but no BandID inventory", details );
  }
  for ( std::size_t i = 0; i < bandCount; ++i )
  {
    BandCalibration calibration;
    calibration.band = product.declaredBandIds[i];
    const auto readFrom = [] ( const std::vector<double> &values, std::size_t index,
                               const std::vector<std::string> &textValues, bool &flag, double &out ) {
      if ( index < values.size() && !std::isnan( values[index] ) )
      {
        out = values[index];
        flag = true;
        return;
      }
      if ( index < textValues.size() )
      {
        double parsed = 0.0;
        if ( parseDouble( textValues[index], parsed ) )
        {
          out = parsed;
          flag = true;
        }
      }
    };
    readFrom( gains, i, gainList, calibration.hasGain, calibration.gain );
    readFrom( offsets, i, offsetList, calibration.hasBias, calibration.bias );
    if ( !calibration.hasBias && !biasList.empty() )
      readFrom( {}, i, biasList, calibration.hasBias, calibration.bias );
    if ( calibration.hasGain || calibration.hasBias )
      product.bandCalibration.push_back( calibration );
  }

  // Bounded passthrough of declared values that downstream consumers may
  // want verbatim (mirrors the Landsat adapter's reporting policy).
  product.extra.emplace_back( "parsed_sidecar", baseNameOf( xmlPath ) );
  if ( !modeId.empty() && product.extra.size() < 16 )
    product.extra.emplace_back( "mode_id", modeId );
  const std::string stopTime = normalizeAcquisitionTime( scanText( scan, "stopdate" ),
                                                         scanText( scan, "stoptime" ) );
  if ( !stopTime.empty() && product.extra.size() < 16 )
    product.extra.emplace_back( "stop_time", stopTime );
  return product;
}

// ─── Band-role table loader ─────────────────────────────────────────────────

/// Resolves data/products/band_roles the way processing/framework resolves
/// data/ paths, Qt-free (ADR 0146).
std::string resolveBandRoleDir()
{
  const char *envDataDir = std::getenv( "SICNU_DATA_DIR" );
  if ( envDataDir && *envDataDir )
  {
    const std::string candidate = std::string( envDataDir ) + "/products/band_roles";
    if ( isDirectoryLocal( candidate ) )
      return candidate;
  }

  auto hasMarker = [] ( const std::string &dir ) {
    return isDirectoryLocal( dir + "/data/products/band_roles" );
  };
  auto walkUp = [] ( std::string dir ) -> std::string {
    for ( int hops = 0; hops < 8 && !dir.empty(); ++hops )
    {
      std::error_code ec;
      if ( hasMarker( dir ) )
        return dir + "/data/products/band_roles";
      const std::string parent = parentOf( dir );
      if ( parent == dir )
        break;
      dir = parent;
      ( void )ec;
    }
    return std::string();
  };

  // Executable directory (in-tree builds/tests, Linux).
  std::error_code exeEc;
  const fs::path exe = fs::canonical( "/proc/self/exe", exeEc );
  if ( !exeEc )
  {
    const std::string fromExe = walkUp( exe.parent_path().generic_string() );
    if ( !fromExe.empty() )
      return fromExe;
  }

  // Current working directory.
  {
    std::error_code cwdEc;
    const std::string cwd = fs::current_path( cwdEc ).generic_string();
    if ( !cwdEc )
    {
      const std::string fromCwd = walkUp( cwd );
      if ( !fromCwd.empty() )
        return fromCwd;
    }
  }

#if defined( SICNU_SOURCE_DIR )
  {
    const std::string fromSource = std::string( SICNU_SOURCE_DIR ) + "/data/products/band_roles";
    if ( isDirectoryLocal( fromSource ) )
      return fromSource;
  }
#endif
  return std::string();
}

std::string bandRoleFamilyFile( const std::string &sensorKey )
{
  if ( sensorKey.rfind( "gf", 0 ) == 0 )
    return "gaofen.json";
  if ( sensorKey.rfind( "zy3", 0 ) == 0 )
    return "zy3.json";
  if ( sensorKey.rfind( "hj", 0 ) == 0 )
    return "hj.json";
  return std::string();
}

const Json::Value &familyJson( const std::string &familyFile )
{
  static std::map<std::string, Json::Value> cache;
  static std::mutex cacheMutex;
  std::lock_guard<std::mutex> lock( cacheMutex );
  auto it = cache.find( familyFile );
  if ( it != cache.end() )
    return it->second;

  const std::string dir = resolveBandRoleDir();
  if ( dir.empty() )
    throw GeoError( ErrorCode::OpenFailed,
                    "Band-role table directory not found: data/products/band_roles "
                    "(set SICNU_DATA_DIR to the platform data root)" );
  const std::string path = dir + "/" + familyFile;
  std::string text;
  if ( !readFileText( path, text ) )
    throw GeoError( ErrorCode::OpenFailed, "Band-role table not found: " + path );
  Json::Value json;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream stream( text );
  if ( !Json::parseFromStream( builder, stream, &json, &errors ) )
    throw GeoError( ErrorCode::InvalidArgument, "Unparseable band-role table " + path + ": " + errors );
  it = cache.emplace( familyFile, std::move( json ) ).first;
  return it->second;
}

CnBandRoleTable loadBandRoleTable( const std::string &sensorKey )
{
  const std::string familyFile = bandRoleFamilyFile( sensorKey );
  if ( familyFile.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "No band-role table family for sensor key: " + sensorKey );
  const Json::Value &json = familyJson( familyFile );
  const Json::Value &sensors = json["sensors"];
  if ( !sensors.isObject() || !sensors.isMember( sensorKey ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Band-role table " + familyFile + " does not declare sensor key: " + sensorKey );
  const Json::Value &entry = sensors[sensorKey];

  CnBandRoleTable table;
  table.sensorKey = sensorKey;
  table.satellite = entry.get( "satellite", Json::Value() ).asString();
  table.sensorMode = entry.get( "sensor_mode", Json::Value() ).asString();
  table.source = json.get( "source", Json::Value() ).asString();
  const Json::Value &bands = entry["bands"];
  if ( !bands.isArray() || bands.empty() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Band-role table entry " + sensorKey + " declares no bands" );
  for ( const Json::Value &band : bands )
  {
    CnBandSpec spec;
    spec.band = band.get( "band", Json::Value() ).asString();
    spec.role = band.get( "role", Json::Value( "unknown" ) ).asString();
    spec.roleReason = band.get( "role_reason", Json::Value() ).asString();
    const Json::Value wavelength = band["wavelength_nm"];
    if ( wavelength.isNumeric() )
    {
      spec.hasWavelength = true;
      spec.wavelengthNm = wavelength.asDouble();
    }
    // Contract enforcement: an unmappable band must say why (D-07).
    if ( spec.role == "unknown" && spec.roleReason.empty() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Band-role table entry " + sensorKey + "/" + spec.band +
                        " has role \"unknown\" without a role_reason" );
    table.bands.push_back( std::move( spec ) );
  }
  return table;
}

} // namespace

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

CnProductIdentity cnIdentifyProduct( const std::string &path )
{
  CnProductIdentity identity;
  const std::string upper = upperAscii( path );

  for ( const FamilyPattern &pattern : kSupportedPatterns )
  {
    if ( upper.find( pattern.prefix ) != std::string::npos )
    {
      identity.recognized = true;
      identity.supported = true;
      identity.satellite = pattern.satellite;
      identity.sensorMode = pattern.sensorMode;
      identity.sensorKey = pattern.sensorKey;
      identity.kindName = pattern.kindName;
      // WFV camera number stays part of the mode ("WFV2"), the table key
      // carries the layout only.
      return identity;
    }
  }

  identity.recognized = unsupportedFamilyReason( upper, identity.reason );
  if ( !identity.recognized )
    identity.reason.clear();
  return identity;
}

std::string cnBandRoleTableDir()
{
  return resolveBandRoleDir();
}

CnBandRoleTable cnBandRoleTable( const std::string &sensorKey )
{
  return loadBandRoleTable( sensorKey );
}

std::string cnLocateSidecarXml( const std::string &path )
{
  if ( isDirectoryLocal( path ) )
  {
    // Prefer the multispectral sidecar (the teaching default for PMS
    // directories); otherwise the first *.xml sibling.
    std::string first;
    for ( const std::string &entry : listDirectoryBounded( path ) )
    {
      const std::string name = baseNameOf( entry );
      if ( extensionOfLower( name ) != ".xml" )
        continue;
      const std::string upper = upperAscii( name );
      if ( upper.find( "MSS" ) != std::string::npos || upper.find( "WFV" ) != std::string::npos ||
           upper.find( "MSC" ) != std::string::npos )
        return entry;
      if ( first.empty() )
        first = entry;
    }
    return first;
  }

  const std::string extension = extensionOfLower( baseNameOf( path ) );
  if ( extension == ".xml" )
    return path;
  const std::string stem = stemOf( baseNameOf( path ) );
  const std::string parent = parentOf( path );
  const std::string sibling = parent + "/" + stem + ".xml";
  if ( fileExistsLocal( sibling ) )
    return sibling;
  return std::string();
}

std::string cnLocateImageTiff( const std::string &path, const std::string &sidecarPath )
{
  auto firstTiffIn = [] ( const std::string &dir ) {
    for ( const std::string &entry : listDirectoryBounded( dir ) )
    {
      const std::string extension = extensionOfLower( baseNameOf( entry ) );
      if ( extension == ".tif" || extension == ".tiff" )
        return entry;
    }
    return std::string();
  };

  if ( isDirectoryLocal( path ) )
  {
    // Same-stem pairing first (MSS1.xml → MSS1.tiff): a PMS directory holds
    // two image/sidecar pairs and directory enumeration order is not
    // deterministic.
    if ( !sidecarPath.empty() )
    {
      const std::string sidecarName = baseNameOf( sidecarPath );
      if ( extensionOfLower( sidecarName ) == ".xml" )
      {
        const std::string stem = stemOf( sidecarName );
        for ( const char *ext : { ".tiff", ".tif" } )
        {
          const std::string candidate = path + "/" + stem + ext;
          if ( fileExistsLocal( candidate ) )
            return candidate;
        }
      }
    }
    return firstTiffIn( path );
  }

  const std::string fileName = baseNameOf( path );
  const std::string extension = extensionOfLower( fileName );
  if ( extension == ".tif" || extension == ".tiff" )
    return path;
  const std::string stem = stemOf( fileName );
  const std::string parent = parentOf( path );
  for ( const char *ext : { ".tiff", ".tif" } )
  {
    const std::string sibling = parent + "/" + stem + ext;
    if ( fileExistsLocal( sibling ) )
      return sibling;
  }
  return std::string();
}

std::string cnSensorKey( const CnProductIdentity &identity, const ProductMetadata &metadata )
{
  if ( identity.sensorKey == "zy3_nad_ms" )
    return zy3NadKey( metadata );
  // PMS cameras deliver two products with the same BandID letters: the
  // multispectral stack (B1..B4) and the panchromatic band (B1). The sidecar
  // ModeID decides; a single declared band without a ModeID is the pan
  // product by inventory shape (same resolution as zy3NadKey — shape-driven,
  // not band-number guessing).
  if ( identity.sensorKey.size() > 3 && identity.sensorKey.compare( identity.sensorKey.size() - 3, 3, "pms" ) == 0 )
  {
    const std::string modeId = upperAscii( extraValue( metadata, "mode_id" ) );
    if ( modeId == "PAN" || metadata.declaredBandIds.size() == 1 )
      return identity.sensorKey + "_pan";
  }
  return identity.sensorKey;
}

ProductMetadata readCnProductMetadata( const std::string &path, const CnProductIdentity &identity )
{
  if ( !identity.supported )
  {
    Json::Value details;
    details["path"] = path;
    details["reason"] = identity.reason.empty() ? Json::Value( "unsupported CN product" )
                                                : Json::Value( identity.reason );
    throw GeoError( ErrorCode::UnsupportedProduct,
                    "Unsupported Chinese satellite product: " +
                      ( identity.reason.empty() ? std::string( "unrecognized product name" )
                                                : identity.reason ),
                    details );
  }

  const std::string xmlPath = cnLocateSidecarXml( path );
  if ( xmlPath.empty() )
  {
    Json::Value details;
    details["path"] = path;
    throw GeoError( ErrorCode::OpenFailed,
                    "No L1A sidecar XML found beside/inside the CN product path", details );
  }
  return parseCresdaXml( xmlPath, identity );
}

} // namespace sicnu::geo
