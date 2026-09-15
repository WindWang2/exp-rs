// test_sensor_schema.cpp — CN sensor registry schema 2.0 (ADR 0159):
// strict per-field validation, band_axis contracts, validator + registry
// drift gate, cross-file reference integrity, v1 back-compat.
//
// Fixtures are minimal handwritten registry files in unique temp roots
// (the loader caches by full path, so roots are never reused). The drift
// gate pins the COMMITTED registry to zero validator findings.

#include <catch2/catch_test_macros.hpp>

#include "geospatial/products/sensor_profile.h"

#include <algorithm>

using namespace sicnu::geo;
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined( _WIN32 )
#include <cstdlib>
static void setDataDirEnv( const std::string &value ) { _putenv_s( "SICNU_DATA_DIR", value.c_str() ); }
static void clearDataDirEnv() { _putenv_s( "SICNU_DATA_DIR", "" ); }
#else
#include <cstdlib>
static void setDataDirEnv( const std::string &value ) { setenv( "SICNU_DATA_DIR", value.c_str(), 1 ); }
static void clearDataDirEnv() { unsetenv( "SICNU_DATA_DIR" ); }
#endif

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

namespace
{

namespace fs = std::filesystem;

/// Unique temp data root per call: the loader caches parsed registry files
/// by full path, so every fixture gets its own tree. The returned path is
/// the DATA ROOT (SICNU_DATA_DIR semantics): the fixture registry lives at
/// <root>/products/sensor_profiles.
std::string makeDataRoot()
{
  static std::atomic<unsigned> counter{ 0 };
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path profiles = fs::temp_directory_path() /
                            ( "sicnu_sensor_schema_" + std::to_string( now ) + "_" +
                              std::to_string( counter++ ) ) / "data" / "products" /
                            "sensor_profiles";
  fs::create_directories( profiles );
  return profiles.parent_path().parent_path().generic_string();
}

/// @p dataRoot follows the SICNU_DATA_DIR semantics: the registry file lands
/// at <dataRoot>/products/sensor_profiles/<fileName>.
void writeRegistry( const std::string &dataRoot, const std::string &fileName,
                    const std::string &text )
{
  const fs::path target =
    fs::u8path( dataRoot ) / "products" / "sensor_profiles" / fs::u8path( fileName );
  std::ofstream out( target, std::ios::binary );
  REQUIRE( out.is_open() );
  out << text;
}

const SensorProfileValidationIssue *findingFor( const std::vector<SensorProfileValidationIssue> &issues,
                                                const std::string &needle )
{
  for ( const SensorProfileValidationIssue &issue : issues )
  {
    if ( issue.message.find( needle ) != std::string::npos )
      return &issue;
  }
  return nullptr;
}

/// Minimal v2 registry file; @p entryBody is the whole sensor entry object.
std::string v2Registry( const std::string &entryBody )
{
  return "{\n  \"version\": 2,\n"
         "  \"source\": \"test fixture (ADR 0159 schema tests)\",\n"
         "  \"sensors\": { \"gf_test\": " +
         entryBody + "}\n}\n";
}

} // namespace

TEST_CASE( "sensor_schema: the committed registry passes the validator (drift gate)",
           "[cn][registry][drift]" )
{
  setDataDirEnv( std::string( CMAKE_SOURCE_DIR ) + "/data" );
  const std::vector<SensorProfileValidationIssue> issues = validateSensorProfiles();
  for ( const SensorProfileValidationIssue &issue : issues )
    FAIL( issue.file + "/" + issue.sensorKey + ( issue.band.empty() ? "" : "/" + issue.band ) +
          ": " + issue.message );
  REQUIRE( issues.empty() );
  clearDataDirEnv();
}

TEST_CASE( "sensor_schema: v2 entries enforce strict field rules", "[cn][registry][v2]" )
{
  struct Case
  {
    const char *name;
    std::string entry;
    std::string expectedFinding;
  };
  const Case cases[] = {
    // Required non-empty identity / calibration semantics.
    { "empty satellite",
      R"json({ "satellite": "", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "radiance = DN * gain + bias",
           "bands": [ { "band": "B1", "role": "blue" } ] })json",
      "empty \"satellite\"" },
    { "empty calibration_rule",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "",
           "bands": [ { "band": "B1", "role": "blue" } ] })json",
      "empty \"calibration_rule\"" },
    // Closed modality vocabulary.
    { "modality outside vocabulary",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "hyper_spectral",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue" } ] })json",
      "modality" },
    // Physical quantities finite and positive.
    { "negative gsd",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "gsd_m": -4.0, "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue" } ] })json",
      "gsd_m" },
    { "negative band gsd",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue", "gsd_m": -1.0 } ] })json",
      "gsd_m" },
    // Closed role vocabulary.
    { "role outside vocabulary",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "greenish" } ] })json",
      "ADR 0065 vocabulary" },
    // Band-id uniqueness (case-insensitive — the lookup is case-insensitive,
    // so duplicates would be ambiguous).
    { "duplicate band id",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue" }, { "band": "b1", "role": "green" } ] })json",
      "duplicate band id" },
    // Range agreement rules.
    { "unparseable spectral range",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue", "spectral_range_um": "0.45..0.52" } ] })json",
      "unparseable spectral_range_um" },
    { "centre outside range",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue", "spectral_range_um": "0.45-0.52",
                        "center_wavelength_nm": 999.0 } ] })json",
      "outside the declared spectral range" },
    { "fwhm exceeds range width",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue", "spectral_range_um": "0.45-0.52",
                        "fwhm_nm": 200.0 } ] })json",
      "never a FWHM" },
    { "wavelength disagrees with midpoint",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue", "spectral_range_um": "0.45-0.52",
                        "wavelength_nm": 600.0 } ] })json",
      "range midpoint" },
    { "wavelength disagrees with centre",
      R"json({ "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
           "calibration_rule": "r = DN * g + b",
           "bands": [ { "band": "B1", "role": "blue", "spectral_range_um": "0.45-0.52",
                        "center_wavelength_nm": 485.0, "wavelength_nm": 520.0 } ] })json",
      "declared centre" },
  };

  for ( const Case &testCase : cases )
  {
    DYNAMIC_SECTION( testCase.name )
    {
      const std::string root = makeDataRoot();
      writeRegistry( root, "gaofen.json", v2Registry( testCase.entry ) );
      setDataDirEnv( root );

      // Loader: typed refusal naming the rule.
      bool threw = false;
      std::string thrownMessage;
      try
      {
        ( void )loadSensorProfile( "gf_test" );
      }
      catch ( const GeoError &catchError )
      {
        threw = true;
        thrownMessage = catchError.what();
      }
      REQUIRE( threw );
      INFO( "error: " << thrownMessage );
      REQUIRE( thrownMessage.find( testCase.expectedFinding ) != std::string::npos );

      // Validator: the same rule surfaces as a named finding.
      const std::vector<SensorProfileValidationIssue> issues = validateSensorProfiles();
      REQUIRE( !issues.empty() );
      INFO( "issues: " << issues.front().file << "/" << issues.front().sensorKey << ": "
                       << issues.front().message );
      REQUIRE( findingFor( issues, testCase.expectedFinding ) != nullptr );
      clearDataDirEnv();
    }
  }
}

TEST_CASE( "sensor_schema: band_axis declares extent, ordering and bad bands",
           "[cn][registry][v2][hyperspectral]" )
{
  // Happy path: extent matches the fully-written-out bands, bad_bands ids
  // exist and map to 0-based indices.
  {
    const std::string root = makeDataRoot();
    writeRegistry( root, "gaofen.json", v2Registry( R"json({
      "satellite": "GF5", "instrument": "AHSI", "sensor_mode": "HSI", "modality": "hyperspectral",
      "calibration_rule": "radiance = DN * gain + offset (declared coefficients)",
      "bands": [
        { "band": "B1", "role": "coastal", "spectral_range_um": "0.40-0.41", "wavelength_nm": 405.0 },
        { "band": "B2", "role": "unknown", "role_reason": "fixture band", "spectral_range_um": "0.41-0.42", "wavelength_nm": 415.0 },
        { "band": "B3", "role": "blue", "spectral_range_um": "0.42-0.43", "wavelength_nm": 425.0 },
        { "band": "B4", "role": "blue", "spectral_range_um": "0.43-0.44", "wavelength_nm": 435.0 }
      ],
      "band_axis": { "count": 4, "ordering": "B1..B4 ascending wavelength", "bad_bands": [ "B3" ] }
    })json" ) );
    setDataDirEnv( root );
    const SensorProfileRecord record = loadSensorProfile( "gf_test" );
    REQUIRE( record.hasBandAxis );
    REQUIRE( record.bandAxisCount == 4 );
    REQUIRE( record.badBandIndices.size() == 1 );
    REQUIRE( record.badBandIndices.front() == 2 );
    REQUIRE( record.bandAxisOrdering == "B1..B4 ascending wavelength" );
    REQUIRE( validateSensorProfiles().empty() );
    clearDataDirEnv();
  }

  // count must equal the declared band array length.
  {
    const std::string root = makeDataRoot();
    writeRegistry( root, "gaofen.json", v2Registry( R"json({
      "satellite": "GF5", "instrument": "AHSI", "sensor_mode": "HSI", "modality": "hyperspectral",
      "calibration_rule": "r",
      "bands": [ { "band": "B1", "role": "blue" } ],
      "band_axis": { "count": 330 }
    })json" ) );
    setDataDirEnv( root );
    bool threw = false;
    try
    {
      ( void )loadSensorProfile( "gf_test" );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      REQUIRE( std::string( error.what() ).find( "band_axis.count" ) != std::string::npos );
    }
    REQUIRE( threw );
    clearDataDirEnv();
  }

  // bad_bands must reference declared band ids.
  {
    const std::string root = makeDataRoot();
    writeRegistry( root, "gaofen.json", v2Registry( R"json({
      "satellite": "GF5", "instrument": "AHSI", "sensor_mode": "HSI", "modality": "hyperspectral",
      "calibration_rule": "r",
      "bands": [ { "band": "B1", "role": "blue" } ],
      "band_axis": { "count": 1, "bad_bands": [ "B330" ] }
    })json" ) );
    setDataDirEnv( root );
    bool threw = false;
    try
    {
      ( void )loadSensorProfile( "gf_test" );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      REQUIRE( std::string( error.what() ).find( "bad_bands" ) != std::string::npos );
    }
    REQUIRE( threw );
    clearDataDirEnv();
  }
}

TEST_CASE( "sensor_schema: v1 files keep the historical rules (additive strictness)",
           "[cn][registry][v1]" )
{
  // A minimal v1 entry that predates the strict fields: loads under v1.
  const std::string v1Entry = R"json({
    "satellite": "GF1",
    "bands": [ { "band": "B1", "role": "blue", "wavelength_nm": 470.0 } ]
  })json";
  {
    const std::string root = makeDataRoot();
    writeRegistry( root, "gaofen.json",
                   "{\n  \"version\": 1,\n  \"sensors\": { \"gf_test\": " + v1Entry + "}\n}\n" );
    setDataDirEnv( root );
    const SensorProfileRecord record = loadSensorProfile( "gf_test" );
    REQUIRE( record.bands.size() == 1 );
    REQUIRE( record.schemaVersion == 1 );
    REQUIRE_FALSE( record.hasBandAxis );
    clearDataDirEnv();
  }
  // The identical content under version 2 fails strict validation.
  {
    const std::string root = makeDataRoot();
    writeRegistry( root, "gaofen.json",
                   "{\n  \"version\": 2,\n  \"source\": \"test\",\n  \"sensors\": { \"gf_test\": " +
                     v1Entry + "}\n}\n" );
    setDataDirEnv( root );
    bool threw = false;
    std::string thrownMessage;
    try
    {
      ( void )loadSensorProfile( "gf_test" );
    }
    catch ( const GeoError &catchError )
    {
      threw = true;
      thrownMessage = catchError.what();
    }
    REQUIRE( threw );
    INFO( "error: " << thrownMessage );
    REQUIRE( thrownMessage.find( "empty" ) != std::string::npos );
    clearDataDirEnv();
  }
}

TEST_CASE( "sensor_schema: cross-file sibling references are validated",
           "[cn][registry][v2][crossref]" )
{
  // pan_variant pointing at a key no file declares: validator finding, but
  // the entry itself still loads (the runtime pan/MS refinement catches the
  // missing profile itself and callers fail closed there).
  {
    const std::string root = makeDataRoot();
    writeRegistry( root, "gaofen.json", v2Registry( R"json({
      "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
      "calibration_rule": "r = DN * g + b", "pan_variant": "gf_test_pan_missing",
      "bands": [ { "band": "B1", "role": "blue" } ]
    })json" ) );
    setDataDirEnv( root );
    const SensorProfileRecord record = loadSensorProfile( "gf_test" );
    REQUIRE( record.panVariant == "gf_test_pan_missing" );
    const std::vector<SensorProfileValidationIssue> issues = validateSensorProfiles();
    const SensorProfileValidationIssue *finding =
      findingFor( issues, "does not resolve to a declared sensor key" );
    REQUIRE( finding != nullptr );
    REQUIRE( finding->sensorKey == "gf_test" );
    clearDataDirEnv();
  }

  // A self-referencing sibling link is refused at load time (shape
  // resolution could never terminate usefully).
  {
    const std::string root = makeDataRoot();
    writeRegistry( root, "gaofen.json", v2Registry( R"json({
      "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
      "calibration_rule": "r = DN * g + b", "pan_variant": "gf_test",
      "bands": [ { "band": "B1", "role": "blue" } ]
    })json" ) );
    setDataDirEnv( root );
    bool threw = false;
    try
    {
      ( void )loadSensorProfile( "gf_test" );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      REQUIRE( std::string( error.what() ).find( "points at itself" ) != std::string::npos );
    }
    REQUIRE( threw );
    clearDataDirEnv();
  }

  // Duplicate sensor keys across files are a validator finding.
  {
    const std::string root = makeDataRoot();
    const std::string entry = R"json({
      "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
      "calibration_rule": "r = DN * g + b",
      "bands": [ { "band": "B1", "role": "blue" } ]
    })json";
    writeRegistry( root, "gaofen.json", v2Registry( entry ) );
    writeRegistry( root, "hj.json",
                   "{\n  \"version\": 2,\n  \"source\": \"test\",\n  \"sensors\": { \"gf_test\": " +
                     entry + "}\n}\n" );
    setDataDirEnv( root );
    const std::vector<SensorProfileValidationIssue> issues = validateSensorProfiles();
    const SensorProfileValidationIssue *finding = findingFor( issues, "redeclared" );
    REQUIRE( finding != nullptr );
    clearDataDirEnv();
  }
}

TEST_CASE( "sensor_schema: unknown keys stay forward-compatible on v2",
           "[cn][registry][v2][forwardcompat]" )
{
  const std::string root = makeDataRoot();
  writeRegistry( root, "gaofen.json", v2Registry( R"json({
    "satellite": "GF1", "instrument": "PMS", "sensor_mode": "PMS", "modality": "optical",
    "calibration_rule": "r = DN * g + b",
    "hypothetical_future_field": 42,
    "bands": [ { "band": "B1", "role": "blue", "future_band_field": "x" } ]
  })json" ) );
  setDataDirEnv( root );
  const SensorProfileRecord record = loadSensorProfile( "gf_test" );
  REQUIRE( record.unknownKeys.size() == 2 );
  REQUIRE( std::find( record.unknownKeys.begin(), record.unknownKeys.end(),
                      "hypothetical_future_field" ) != record.unknownKeys.end() );
  REQUIRE( std::find( record.unknownKeys.begin(), record.unknownKeys.end(),
                      "gf_test/B1/future_band_field" ) != record.unknownKeys.end() );
  // Forward compatibility is explicit but not a drift finding for foreign
  // fixtures — only committed data is pinned to zero findings.
  clearDataDirEnv();
}

TEST_CASE( "sensor_schema: future registry versions stay refused", "[cn][registry][gate]" )
{
  const std::string root = makeDataRoot();
  writeRegistry( root, "gaofen.json",
                 R"json({"version": 3, "sensors": {"gf_test": {"bands": []}}})json" );
  setDataDirEnv( root );
  bool threw = false;
  try
  {
    ( void )loadSensorProfile( "gf_test" );
  }
  catch ( const GeoError &error )
  {
    threw = true;
    REQUIRE( std::string( error.what() ).find( "version" ) != std::string::npos );
  }
  REQUIRE( threw );
  clearDataDirEnv();
}
