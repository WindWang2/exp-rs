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
#include <cmath>
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

// Schema 2.0 (ADR 0159): v1 stays readable with its historical rules; v2
// files get strict per-field validation. Every supported version is listed
// here — a file with any other version is a typed refusal (fail-closed gate).
const std::set<int> kSupportedSchemaVersions = { 1, 2 };
constexpr double kMidpointToleranceNm = 0.5; ///< midpoint/centre rounding slack
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
  if ( sensorKey.rfind( "cbers", 0 ) == 0 )
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

// ─── Schema 2.0 strict rules ────────────────────────────────────────────────
// Physical quantities must be finite and positive; roles must come from the
// ADR 0065 vocabulary (mirrors src/data/band_role.h; this Qt-free loader
// keeps its own literal set — the registry drift test pins the two together
// through the shared on-disk strings); band ids are unique per entry;
// wavelength values agree with the declared spectral range (published centre
// when present, otherwise the documented range midpoint within rounding
// slack). Every rule violation is a typed GeoError naming the entry, the
// band and the offending value — never a silent default.

const std::set<std::string> &bandRoleVocabulary()
{
  static const std::set<std::string> kVocabulary = {
    "coastal", "blue", "green", "red", "red_edge", "nir", "narrow_nir",
    "swir1", "swir2", "cirrus", "panchromatic", "thermal", "qa",
    "scene_classification", "unknown",
  };
  return kVocabulary;
}

const std::set<std::string> &modalityVocabulary()
{
  static const std::set<std::string> kModalities = { "optical", "sar", "thermal", "hyperspectral" };
  return kModalities;
}

bool positivePhysical( const Json::Value &value, double &out )
{
  if ( !value.isNumeric() || !value.isDouble() )
    return false;
  const double number = value.asDouble();
  if ( !std::isfinite( number ) || number <= 0.0 )
    return false;
  out = number;
  return true;
}

/// Parses "0.45-0.52" into a nanometre interval. False on any other shape
/// (verbatim passthrough fields are not guessed).
bool parseSpectralRangeUm( const std::string &text, double &lowNm, double &highNm )
{
  const std::size_t dash = text.find( '-' );
  if ( dash == std::string::npos || dash == 0 || dash + 1 >= text.size() )
    return false;
  try
  {
    std::size_t consumed = 0;
    const double lowUm = std::stod( text.substr( 0, dash ), &consumed );
    if ( consumed != dash )
      return false;
    const std::string highText = text.substr( dash + 1 );
    const double highUm = std::stod( highText, &consumed );
    if ( consumed != highText.size() )
      return false;
    if ( !std::isfinite( lowUm ) || !std::isfinite( highUm ) || highUm <= lowUm )
      return false;
    lowNm = lowUm * 1000.0;
    highNm = highUm * 1000.0;
    return true;
  }
  catch ( const std::exception & )
  {
    return false;
  }
}

/// v2 strict validation of one parsed band. Throws GeoError naming the
/// entry/band/rule on the first violation.
void validateStrictBandV2( const std::string &sensorKey, const SensorBandProfile &band,
                           const Json::Value &bandJson, std::set<std::string> &seenBandIds )
{
  const std::string where = sensorKey + "/" + band.band;
  // Strict JSON typing: a present-but-non-numeric physical field is a named
  // refusal — the parse layer would otherwise treat it as silently absent.
  for ( const char *field : { "wavelength_nm", "center_wavelength_nm", "fwhm_nm", "gsd_m" } )
  {
    if ( bandJson.isMember( field ) && !bandJson[ field ].isNumeric() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + " declares \"" + field +
                        "\" with a non-numeric JSON type" );
  }
  const std::string idLower = lowerAscii( band.band );
  if ( !seenBandIds.insert( idLower ).second )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + sensorKey + " declares duplicate band id \"" +
                      band.band + "\" (case-insensitive)" );

  if ( !bandRoleVocabulary().count( band.role ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + " declares role \"" + band.role +
                      "\" outside the ADR 0065 vocabulary" );

  auto checkPositive = [ & ] ( const char *field, bool has, double value ) {
    if ( has && ( !std::isfinite( value ) || value <= 0.0 ) )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + " declares " + field + " = " +
                        std::to_string( value ) + "; physical quantities must be finite and > 0" );
  };
  checkPositive( "wavelength_nm", band.hasWavelengthNm, band.wavelengthNm );
  checkPositive( "center_wavelength_nm", band.hasCenterWavelengthNm, band.centerWavelengthNm );
  checkPositive( "fwhm_nm", band.hasFwhmNm, band.fwhmNm );
  checkPositive( "gsd_m", band.hasGsdM, band.gsdM );

  // Spectral-range agreement: the declared midpoint (or centre) must sit on
  // the verbatim range. This is the drift gate that keeps "range width is
  // never a FWHM" and midpoint-vs-centre semantics honest.
  const std::string rangeText = bandJson.get( "spectral_range_um", Json::Value() ).asString();
  if ( !rangeText.empty() )
  {
    double lowNm = 0.0;
    double highNm = 0.0;
    if ( !parseSpectralRangeUm( rangeText, lowNm, highNm ) )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + " declares unparseable spectral_range_um \"" +
                        rangeText + "\" (expected \"0.45-0.52\")" );
    if ( band.hasCenterWavelengthNm &&
         ( band.centerWavelengthNm < lowNm || band.centerWavelengthNm > highNm ) )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + ": center_wavelength_nm " +
                        std::to_string( band.centerWavelengthNm ) +
                        " nm lies outside the declared spectral range " + rangeText + " um" );
    if ( band.hasFwhmNm && band.fwhmNm > ( highNm - lowNm ) )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + ": fwhm_nm " + std::to_string( band.fwhmNm ) +
                        " nm exceeds the declared range width — a range width is never a FWHM" );
    if ( band.hasWavelengthNm )
    {
      const double reference = band.hasCenterWavelengthNm ? band.centerWavelengthNm
                                                          : ( lowNm + highNm ) / 2.0;
      if ( std::fabs( band.wavelengthNm - reference ) > kMidpointToleranceNm )
        throw GeoError( ErrorCode::InvalidArgument,
                        "Sensor profile entry " + where + ": wavelength_nm " +
                          std::to_string( band.wavelengthNm ) + " nm disagrees with the " +
                          ( band.hasCenterWavelengthNm ? std::string( "declared centre " )
                                                       : std::string( "range midpoint " ) ) +
                          std::to_string( reference ) + " nm (tolerance " +
                          std::to_string( kMidpointToleranceNm ) + " nm)" );
    }
  }
}

/// v2 strict validation + band_axis parsing for one entry. Throws GeoError
/// naming the entry/rule on the first violation.
void validateStrictEntryV2( const std::string &sensorKey, const std::string &familyFile,
                            const Json::Value &entry, SensorProfileRecord &record )
{
  const std::string where = sensorKey + " in " + familyFile;

  // Strict JSON typing first: a wrongly-typed field is a named refusal, not
  // a silently-absent value (the v2 contract the schema document promises).
  for ( const char *field : { "satellite", "instrument", "sensor_mode", "calibration_rule" } )
  {
    if ( entry.isMember( field ) && !entry[ field ].isString() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + " declares \"" + field +
                        "\" with a non-string JSON type" );
    const std::string value = entry.get( field, Json::Value() ).asString();
    if ( value.empty() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + " declares an empty \"" + field + "\"" );
  }
  if ( !entry.isMember( "modality" ) || !entry[ "modality" ].isString() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where +
                      " must declare a string \"modality\" (schema 2.0 has no default)" );
  if ( entry.isMember( "gsd_m" ) )
  {
    double gsd = 0.0;
    if ( !positivePhysical( entry[ "gsd_m" ], gsd ) )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where +
                        " declares gsd_m with a non-numeric or non-positive value" );
  }
  for ( const char *field : { "pan_variant", "ms_variant" } )
  {
    if ( entry.isMember( field ) && !entry[ field ].isString() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + " declares \"" + field +
                        "\" with a non-string JSON type" );
  }

  const std::string modality = record.modality;
  if ( !modalityVocabulary().count( modality ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + " declares modality \"" + modality +
                      "\" outside the documented vocabulary (optical/sar/thermal/hyperspectral)" );
  if ( record.hasGsdM && ( !std::isfinite( record.gsdM ) || record.gsdM <= 0.0 ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + " declares gsd_m = " +
                      std::to_string( record.gsdM ) + "; physical quantities must be finite and > 0" );
  for ( const std::string &constituent : record.constituents )
  {
    if ( constituent.empty() )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + " declares an empty constituent" );
  }
  if ( !record.panVariant.empty() && lowerAscii( record.panVariant ) == lowerAscii( sensorKey ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + ": pan_variant points at itself" );
  if ( !record.msVariant.empty() && lowerAscii( record.msVariant ) == lowerAscii( sensorKey ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + ": ms_variant points at itself" );

  std::set<std::string> seenBandIds;
  for ( std::size_t i = 0; i < record.bands.size() && i < static_cast<std::size_t>( entry["bands"].size() ); ++i )
    validateStrictBandV2( sensorKey, record.bands[i], entry["bands"][static_cast<Json::ArrayIndex>( i )],
                          seenBandIds );

  // band_axis (v2, optional): extent/ordering/bad-band flags about the
  // fully-written-out bands array. Never a runtime band generator.
  const Json::Value axis = entry["band_axis"];
  if ( axis.isNull() )
    return;
  if ( !axis.isObject() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + " declares a non-object band_axis" );
  const Json::Value count = axis["count"];
  // jsoncpp reports any real within int range as isIntegral(); a fractional
  // count must not silently truncate.
  if ( !count.isIntegral() || count.asInt() <= 0 ||
       ( count.isDouble() && count.asDouble() != static_cast<double>( count.asInt() ) ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + " declares band_axis.count that is not a positive integer" );
  if ( count.asInt() != static_cast<int>( record.bands.size() ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + ": band_axis.count " +
                      std::to_string( count.asInt() ) + " != declared bands " +
                      std::to_string( record.bands.size() ) );
  record.hasBandAxis = true;
  record.bandAxisCount = count.asInt();
  record.bandAxisOrdering = axis.get( "ordering", Json::Value() ).asString();
  const Json::Value badBands = axis["bad_bands"];
  if ( badBands.isNull() )
    return;
  if ( !badBands.isArray() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + where + " declares a non-array band_axis.bad_bands" );
  for ( const Json::Value &bad : badBands )
  {
    const std::string badId = bad.asString();
    int index = -1;
    for ( std::size_t i = 0; i < record.bands.size(); ++i )
    {
      if ( lowerAscii( record.bands[i].band ) == lowerAscii( badId ) )
      {
        index = static_cast<int>( i );
        break;
      }
    }
    if ( index < 0 )
      throw GeoError( ErrorCode::InvalidArgument,
                      "Sensor profile entry " + where + ": band_axis.bad_bands names \"" + badId +
                        "\" which is not in the declared band layout" );
    record.badBandIndices.push_back( index );
  }
}

SensorProfileRecord parseSensorEntryImpl( const std::string &sensorKey,
                                          const std::string &familyFile,
                                          int fileVersion, const std::string &source,
                                          const Json::Value &entry )
{
  if ( !entry.isObject() )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + sensorKey + " in " + familyFile + " is not an object" );

  // Forward compatibility: keys outside the schema are ignored but named.
  static const std::set<std::string> kKnownSensorKeys = {
    "satellite", "instrument", "sensor_mode", "modality", "gsd_m", "pan_variant",
    "ms_variant", "constituents", "calibration_rule", "qa_vocabulary", "bands",
    "band_axis", // v2
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
    // Contract enforcement: an unmappable band must say why (ADR 0157 D-07).
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

  if ( fileVersion >= 2 )
    validateStrictEntryV2( sensorKey, familyFile, entry, record );
  return record;
}

/// Fail-closed boundary: a wrongly-typed JSON field must surface as the
/// loader's typed GeoError (so the validator reports it as a finding),
/// never as a jsoncpp LogicError escaping the report-not-throw contract.
SensorProfileRecord parseSensorEntry( const std::string &sensorKey, const std::string &familyFile,
                                      int fileVersion, const std::string &source,
                                      const Json::Value &entry )
{
  try
  {
    return parseSensorEntryImpl( sensorKey, familyFile, fileVersion, source, entry );
  }
  catch ( const Json::Exception &error )
  {
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile entry " + sensorKey + " in " + familyFile +
                      " violates the JSON field contract: " + error.what() );
  }
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
  if ( !kSupportedSchemaVersions.count( version.asInt() ) )
    throw GeoError( ErrorCode::InvalidArgument,
                    "Sensor profile registry " + familyFile + " has version " +
                      std::to_string( version.asInt() ) + "; this build understands versions 1–" +
                      std::to_string( *kSupportedSchemaVersions.rbegin() ) );

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

std::vector<SensorProfileValidationIssue> validateSensorProfiles()
{
  std::vector<SensorProfileValidationIssue> issues;
  auto addIssue = [ & ] ( const std::string &file, const std::string &sensorKey,
                          const std::string &band, const std::string &message ) {
    issues.push_back( { file, sensorKey, band, message } );
  };

  const std::string dir = sensorProfileDir();
  if ( dir.empty() )
  {
    addIssue( "", "", "", "sensor profile registry directory not found" );
    return issues;
  }

  // Pass 1 — per-file, per-entry validation under the file's declared
  // version rules (the same code paths the loader enforces, reported
  // instead of thrown so one bad entry never hides the others).
  std::map<std::string, std::vector<std::pair<std::string, const Json::Value *>>> entriesByKey;
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

    const Json::Value *json = nullptr;
    try
    {
      json = &familyJson( name );
    }
    catch ( const GeoError &error )
    {
      addIssue( name, "", "", error.what() );
      continue;
    }
    const Json::Value version = ( *json )["version"];
    if ( !version.isIntegral() )
    {
      addIssue( name, "", "", "does not declare an integer version" );
      continue;
    }
    if ( !kSupportedSchemaVersions.count( version.asInt() ) )
    {
      addIssue( name, "", "",
                "version " + std::to_string( version.asInt() ) + " is not a supported schema version" );
      continue;
    }
    if ( version.asInt() >= 2 && json->get( "source", Json::Value() ).asString().empty() )
      addIssue( name, "", "", "v2 files must carry a non-empty \"source\" provenance note" );

    const Json::Value &sensors = ( *json )["sensors"];
    if ( !sensors.isObject() || sensors.empty() )
    {
      addIssue( name, "", "", "no non-empty \"sensors\" object" );
      continue;
    }
    for ( const std::string &key : sensors.getMemberNames() )
    {
      auto &slot = entriesByKey[key];
      slot.emplace_back( name, &sensors[key] );
      if ( slot.size() > 1 )
        addIssue( name, key, "", "sensor key redeclared in " + slot.front().first );
      try
      {
        parseSensorEntry( key, name, version.asInt(),
                          json->get( "source", Json::Value() ).asString(), sensors[key] );
      }
      catch ( const GeoError &error )
      {
        addIssue( name, key, "", error.what() );
      }
    }
  }

  // Pass 2 — registry-wide cross-references: pan/ms sibling links must
  // resolve to declared keys (any file). A dangling link would make the
  // runtime pan/MS shape resolution silently lose its refinement path.
  for ( const auto &entry : entriesByKey )
  {
    if ( entry.second.size() != 1 )
      continue; // duplicates already reported; skip to avoid cascades
    const std::string &file = entry.second.front().first;
    const Json::Value &json = *entry.second.front().second;
    for ( const char *variant : { "pan_variant", "ms_variant" } )
    {
      if ( !json.isMember( variant ) || !json[ variant ].isString() )
        continue; // wrongly-typed fields are already reported by pass 1
      const std::string target = json[ variant ].asString();
      if ( target.empty() )
        continue;
      if ( !entriesByKey.count( target ) )
        addIssue( file, entry.first, "",
                  std::string( variant ) + " \"" + target + "\" does not resolve to a declared sensor key" );
    }
  }
  return issues;
}

} // namespace sicnu::geo
