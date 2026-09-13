/***************************************************************************
  geospatial/products/sensor_profile.cpp
  Sensor/Product Physics Platform 10.0 — sensor profile registry loader.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Qt-free jsoncpp loader over data/products/sensor_profiles/*.json. Per-file
  caching is mutex-guarded; failures are typed GeoErrors (fail-closed), and
  unknown entry keys are ignored but reported on the record (forward
  compatibility is explicit — never silent, never fatal).
 ***************************************************************************/

#include "geospatial/products/sensor_profile.h"

#include "geospatial/common.h"

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace sicnu::geo
{

namespace
{

namespace fs = std::filesystem;

std::string lowerAscii( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  [] ( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

std::string parentOf( const std::string &path )
{
  const std::size_t slash = path.find_last_of( "/\\" );
  return slash == std::string::npos ? std::string() : path.substr( 0, slash );
}

bool isDirectoryLocal( const std::string &path )
{
  std::error_code ec;
  return fs::is_directory( fs::u8path( path ), ec );
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

constexpr int kRegistrySchemaVersion = 1;
constexpr int kMaxWalkUpHops = 8;
constexpr std::size_t kMaxFamilyFiles = 32;

/// Resolves <dataRoot>/<relativeDir> the way the processing layer resolves
/// data/ paths, Qt-free (SICNU_DATA_DIR → walk-up from executable/cwd →
/// compiled source dir).
std::string resolveDataSubdir( const char *relativeDir )
{
  const char *envDataDir = std::getenv( "SICNU_DATA_DIR" );
  if ( envDataDir && *envDataDir )
  {
    const std::string candidate = std::string( envDataDir ) + "/" + relativeDir;
    if ( isDirectoryLocal( candidate ) )
      return candidate;
  }

  const std::string marker = std::string( "/data/" ) + relativeDir;
  auto walkUp = [ &marker ] ( std::string dir ) -> std::string {
    for ( int hops = 0; hops < kMaxWalkUpHops && !dir.empty(); ++hops )
    {
      if ( isDirectoryLocal( dir + marker ) )
        return dir + marker;
      const std::string parent = parentOf( dir );
      if ( parent == dir )
        break;
      dir = parent;
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
    const std::string fromSource = std::string( SICNU_SOURCE_DIR ) + "/data/" + relativeDir;
    if ( isDirectoryLocal( fromSource ) )
      return fromSource;
  }
#endif
  return std::string();
}

std::string familyFileFor( const std::string &sensorKey )
{
  if ( sensorKey.rfind( "gf", 0 ) == 0 )
    return "gaofen.json";
  if ( sensorKey.rfind( "zy3", 0 ) == 0 )
    return "zy3.json";
  if ( sensorKey.rfind( "zy1", 0 ) == 0 )
    return "zy1.json";
  if ( sensorKey.rfind( "hj", 0 ) == 0 )
    return "hj.json";
  if ( sensorKey.rfind( "cb", 0 ) == 0 )
    return "cbers.json";
  return std::string();
}

/// Parsed registry files, cached by FULL PATH: environment overrides
/// (SICNU_DATA_DIR) must win over an earlier in-tree parse of the same
/// file name, so the cache key can never be the file name alone.
const Json::Value &familyJson( const std::string &familyFile )
{
  static std::map<std::string, Json::Value> cache;
  static std::mutex cacheMutex;
  const std::string dir = sensorProfileDir();
  if ( dir.empty() )
    throw GeoError( ErrorCode::OpenFailed,
                    "Sensor profile registry not found: data/products/sensor_profiles "
                    "(set SICNU_DATA_DIR to the platform data root)" );
  const std::string path = dir + "/" + familyFile;
  std::lock_guard<std::mutex> lock( cacheMutex );
  auto it = cache.find( path );
  if ( it != cache.end() )
    return it->second;

  std::string text;
  if ( !readFileText( path, text ) )
    throw GeoError( ErrorCode::OpenFailed, "Sensor profile registry file not found: " + path );
  Json::Value json;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream stream( text );
  if ( !Json::parseFromStream( builder, stream, &json, &errors ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Unparseable sensor profile registry " + path + ": " + errors );
  it = cache.emplace( path, std::move( json ) ).first;
  return it->second;
}

void collectStringList( const Json::Value &value, std::vector<std::string> &out )
{
  if ( !value.isArray() )
    return;
  for ( const Json::Value &item : value )
  {
    if ( item.isString() )
      out.push_back( item.asString() );
  }
}

SensorProfileRecord parseSensorEntry( const std::string &sensorKey, const std::string &familyFile,
                                      int fileVersion, const std::string &source,
                                      const Json::Value &entry )
{
  if ( !entry.isObject() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + sensorKey + " in " + familyFile + " is not an object" );

  // Forward compatibility: keys outside the v1 schema are ignored but named.
  static const std::set<std::string> kKnownSensorKeys = {
    "satellite", "instrument", "sensor_mode", "modality", "gsd_m", "pan_variant",
    "ms_variant", "constituents", "calibration_rule", "qa_vocabulary", "bands",
  };
  static const std::set<std::string> kKnownBandKeys = {
    "band", "role", "role_reason", "wavelength_nm", "center_wavelength_nm", "fwhm_nm",
    "spectral_range_um", "gsd_m", "note",
  };

  SensorProfileRecord record;
  record.sensorKey = sensorKey;
  record.schemaVersion = fileVersion;
  record.source = source;
  record.familyFile = familyFile;
  record.satellite = entry.get( "satellite", Json::Value() ).asString();
  record.instrument = entry.get( "instrument", Json::Value() ).asString();
  record.sensorMode = entry.get( "sensor_mode", Json::Value() ).asString();
  record.modality = entry.get( "modality", Json::Value( "optical" ) ).asString();
  const Json::Value gsd = entry["gsd_m"];
  if ( gsd.isNumeric() )
  {
    record.hasGsdM = true;
    record.gsdM = gsd.asDouble();
  }
  record.panVariant = entry.get( "pan_variant", Json::Value() ).asString();
  record.msVariant = entry.get( "ms_variant", Json::Value() ).asString();
  record.calibrationRule = entry.get( "calibration_rule", Json::Value() ).asString();
  collectStringList( entry["constituents"], record.constituents );
  collectStringList( entry["qa_vocabulary"], record.qaVocabulary );
  for ( const std::string &key : entry.getMemberNames() )
  {
    if ( !kKnownSensorKeys.count( key ) )
      record.unknownKeys.push_back( key );
  }

  const Json::Value &bands = entry["bands"];
  if ( !bands.isArray() || bands.empty() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + sensorKey + " declares no bands" );
  for ( const Json::Value &band : bands )
  {
    if ( !band.isObject() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile band of " + sensorKey + " is not an object" );
    SensorBandProfile spec;
    spec.band = band.get( "band", Json::Value() ).asString();
    if ( spec.band.empty() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + sensorKey + " declares a band without an id" );
    spec.role = band.get( "role", Json::Value( "unknown" ) ).asString();
    spec.roleReason = band.get( "role_reason", Json::Value() ).asString();
    // Contract enforcement: an unmappable band must say why (ADR 0146 D-07).
    if ( spec.role == "unknown" && spec.roleReason.empty() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + sensorKey + "/" + spec.band +
                        " has role \"unknown\" without a role_reason" );
    const Json::Value wavelength = band["wavelength_nm"];
    if ( wavelength.isNumeric() )
    {
      spec.hasWavelengthNm = true;
      spec.wavelengthNm = wavelength.asDouble();
    }
    const Json::Value center = band["center_wavelength_nm"];
    if ( center.isNumeric() )
    {
      spec.hasCenterWavelengthNm = true;
      spec.centerWavelengthNm = center.asDouble();
    }
    const Json::Value fwhm = band["fwhm_nm"];
    if ( fwhm.isNumeric() )
    {
      spec.hasFwhmNm = true;
      spec.fwhmNm = fwhm.asDouble();
    }
    spec.spectralRangeUm = band.get( "spectral_range_um", Json::Value() ).asString();
    const Json::Value bandGsd = band["gsd_m"];
    if ( bandGsd.isNumeric() )
    {
      spec.hasGsdM = true;
      spec.gsdM = bandGsd.asDouble();
    }
    spec.note = band.get( "note", Json::Value() ).asString();
    for ( const std::string &key : band.getMemberNames() )
    {
      if ( !kKnownBandKeys.count( key ) )
        record.unknownKeys.push_back( sensorKey + "/" + spec.band + "/" + key );
    }
    record.bands.push_back( std::move( spec ) );
  }
  return record;
}

} // namespace

const SensorBandProfile *SensorProfileRecord::findBand( const std::string &bandId ) const
{
  const std::string needle = lowerAscii( bandId );
  for ( const SensorBandProfile &band : bands )
  {
    if ( lowerAscii( band.band ) == needle )
      return &band;
  }
  return nullptr;
}

std::string sensorProfileDir()
{
  // Deliberately re-resolved on every call: an env override set after the
  // first lookup must win (matches the band-role loader's historical
  // behavior and the SICNU_DATA_DIR tests).
  return resolveDataSubdir( "products/sensor_profiles" );
}

std::vector<std::string> sensorProfileKeys( std::vector<std::string> *warnings )
{
  std::vector<std::string> keys;
  const std::string dir = sensorProfileDir();
  if ( dir.empty() )
  {
    if ( warnings )
      warnings->push_back( "sensor profile registry directory not found" );
    return keys;
  }
  std::error_code ec;
  int visited = 0;
  for ( fs::directory_iterator it( fs::u8path( dir ), ec ), end;
        !ec && it != end && visited < kMaxFamilyFiles; it.increment( ec ) )
  {
    ++visited;
    std::error_code entryEc;
    if ( !it->is_regular_file( entryEc ) || entryEc )
      continue;
    const std::u8string u8 = it->path().generic_u8string();
    const std::string path( reinterpret_cast<const char *>( u8.data() ), u8.size() );
    const std::string name = path.substr( path.find_last_of( "/\\" ) + 1 );
    if ( name.size() < 6 || name.substr( name.size() - 5 ) != ".json" )
      continue;
    try
    {
      const Json::Value &json = familyJson( name );
      const Json::Value &sensors = json["sensors"];
      if ( !sensors.isObject() )
        throw GeoError( ErrorCode::InvalidArgument, "no \"sensors\" object" );
      for ( const std::string &key : sensors.getMemberNames() )
        keys.push_back( key );
    }
    catch ( const GeoError &error )
    {
      if ( warnings )
        warnings->push_back( name + ": " + error.what() );
    }
  }
  std::sort( keys.begin(), keys.end() );
  return keys;
}

SensorProfileRecord loadSensorProfile( const std::string &sensorKey )
{
  const std::string familyFile = familyFileFor( sensorKey );
  if ( familyFile.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "No sensor profile family for sensor key: " + sensorKey );
  const Json::Value &json = familyJson( familyFile );

  const Json::Value version = json["version"];
  if ( !version.isIntegral() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile registry " + familyFile + " does not declare an integer version" );
  if ( version.asInt() != kRegistrySchemaVersion )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile registry " + familyFile + " has version " +
                      std::to_string( version.asInt() ) + "; this build understands version " +
                      std::to_string( kRegistrySchemaVersion ) );

  const Json::Value &sensors = json["sensors"];
  if ( !sensors.isObject() || !sensors.isMember( sensorKey ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile registry " + familyFile + " does not declare sensor key: " + sensorKey );

  return parseSensorEntry( sensorKey, familyFile, version.asInt(),
                           json.get( "source", Json::Value() ).asString(), sensors[sensorKey] );
}

bool hasSensorProfile( const std::string &sensorKey )
{
  try
  {
    loadSensorProfile( sensorKey );
    return true;
  }
  catch ( const GeoError & )
  {
    return false;
  }
}

} // namespace sicnu::geo
