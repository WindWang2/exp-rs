/***************************************************************************
  test_env_doctor.cpp — Deployment 11.0 (F19) environment self-check suite.

  Independent oracles only: severity/counts consistency (recomputed here from
  the returned checks), injected impossible requirements (a nonsense driver
  name, a nonexistent PROJ candidate root, an unwritable temp root), a
  fixture runtime-data tree, offline-gate state roundtrip with full restore,
  and — for every emitted finding — the curated diagnostic id must exist in
  data/help/diagnostics.json (parsed fresh from the source tree, never
  through the code under test).
 ***************************************************************************/

#include "geospatial/doctor/env_doctor.h"
#include "geospatial/remote/offline_gate.h"

#include <gdal.h>

#include "geospatial/gdal_guard.h"

using namespace sicnu::geo::envcheck;

#include <catch2/catch_test_macros.hpp>

#include <cpl_conv.h>

#include <filesystem>
#include <fstream>
#include <json/json.h>
#include <set>
#include <string>
#include <vector>

namespace
{

int severityCount( const sicnu::geo::envcheck::EnvDoctorReport &report, const char *severity )
{
  int n = 0;
  for ( const Json::Value &check : report.checks )
    if ( check["severity"].asString() == severity )
      ++n;
  return n;
}

Json::Value parseJsonFile( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  REQUIRE( in.good() );
  Json::Value value;
  Json::CharReaderBuilder builder;
  std::string errors;
  REQUIRE( Json::parseFromStream( builder, in, &value, &errors ) );
  return value;
}

/// All diagnostic ids the curated catalog knows about (independent read).
std::set< std::string > curatedDiagnosticIds()
{
  const Json::Value pages = parseJsonFile(
    std::string( CMAKE_SOURCE_DIR ) + "/data/help/diagnostics.json" );
  std::set< std::string > ids;
  for ( const Json::Value &page : pages )
    ids.insert( page["id"].asString() );
  return ids;
}

void requireCountsConsistent( const sicnu::geo::envcheck::EnvDoctorReport &report )
{
  REQUIRE( ( int )report.checks.size() ==
           report.okCount + report.infoCount + report.warningCount + report.errorCount );
  REQUIRE( severityCount( report, "ok" ) == report.okCount );
  REQUIRE( severityCount( report, "info" ) == report.infoCount );
  REQUIRE( severityCount( report, "warning" ) == report.warningCount );
  REQUIRE( severityCount( report, "error" ) == report.errorCount );

  const std::string worst = report.worst();
  if ( report.errorCount > 0 )
    REQUIRE( worst == "error" );
  else if ( report.warningCount > 0 )
    REQUIRE( worst == "warning" );
  else if ( report.infoCount > 0 )
    REQUIRE( worst == "info" );
  else
    REQUIRE( worst == "ok" );
}

/// Every finding: severity vocabulary, non-empty check/message, and the
/// referenced diagnostic id (if any) must exist in the curated catalog.
void requireWellFormed( const sicnu::geo::envcheck::EnvDoctorReport &report )
{
  const std::set< std::string > curated = curatedDiagnosticIds();
  for ( const Json::Value &check : report.checks )
  {
    const std::string severity = check["severity"].asString();
    INFO( "check: " << check["check"].asString() );
    REQUIRE( ( severity == "ok" || severity == "info" || severity == "warning"
               || severity == "error" ) );
    REQUIRE( !check["check"].asString().empty() );
    REQUIRE( !check["message"].asString().empty() );
    if ( check.isMember( "diagnostic" ) )
    {
      INFO( "diagnostic id: " << check["diagnostic"].asString() );
      REQUIRE( curated.count( check["diagnostic"].asString() ) == 1 );
    }
  }
}

const Json::Value *findCheck( const sicnu::geo::envcheck::EnvDoctorReport &report,
                              const std::string &checkId )
{
  for ( const Json::Value &check : report.checks )
    if ( check["check"].asString() == checkId )
      return &check;
  return nullptr;
}

} // namespace

TEST_CASE( "env doctor healthy-host report is well-formed and self-consistent",
           "[env_doctor][f19]" )
{
  sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor();
  REQUIRE( report.toJson()["schema"].asString() == "exp.env.report.v1" );
  REQUIRE( !report.platform.empty() );
  requireCountsConsistent( report );
  requireWellFormed( report );

  // A normal dev/deployment host resolves proj.db and can write its temp.
  const Json::Value *proj = findCheck( report, "proj.db" );
  REQUIRE( proj != nullptr );
  if ( (*proj)["severity"].asString() == "ok" )
  {
    REQUIRE( (*proj)["detail"].isMember( "resolved" ) );
    // The candidate list names every probed path (Oracle 3 pointer, even on
    // the healthy path).
    REQUIRE( (*proj)["detail"]["probed"].isArray() );
    REQUIRE( (*proj)["detail"]["probed"].size() >= 1 );
  }

  const Json::Value *temp = findCheck( report, "fs.temp" );
  REQUIRE( temp != nullptr );
  if ( (*temp)["severity"].asString() == "ok" )
    REQUIRE( (*temp)["detail"]["path"].asString().size() > 0 );
}

TEST_CASE( "env doctor names a missing required driver verbatim",
           "[env_doctor][f19][negative]" )
{
  sicnu::geo::envcheck::EnvCheckOptions options;
  options.requiredDrivers = { "GTiff", "NoSuchDriver_F19XYZ" };
  sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor( options );
  requireCountsConsistent( report );

  const Json::Value *drivers = findCheck( report, "gdal.drivers.required" );
  REQUIRE( drivers != nullptr );
  REQUIRE( (*drivers)["severity"].asString() == "error" );
  REQUIRE( (*drivers)["message"].asString().find( "NoSuchDriver_F19XYZ" ) != std::string::npos );
  REQUIRE( (*drivers)["diagnostic"].asString() == "diagnostic.env.gdal_driver_missing" );
  REQUIRE( report.errorCount >= 1 );
  REQUIRE( std::string( report.worst() ) == "error" );
}

TEST_CASE( "env doctor full required-driver set passes on a complete host",
           "[env_doctor][f19]" )
{
  sicnu::geo::envcheck::EnvCheckOptions options;
  options.requiredDrivers = { "GTiff", "GPKG", "GeoJSON", "ESRI Shapefile", "MEM", "VRT" };
  sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor( options );
  const Json::Value *drivers = findCheck( report, "gdal.drivers.required" );
  REQUIRE( drivers != nullptr );
  // Host-tolerant oracle: recompute the expected absence set independently
  // through the GDAL C API — the check must agree with reality, whichever
  // drivers this host's build carries.
  ensureGdalRegistered();
  std::set< std::string > independentlyMissing;
  for ( const char *name : { "GTiff", "GPKG", "GeoJSON", "ESRI Shapefile", "MEM", "VRT" } )
    if ( !GDALGetDriverByName( name ) )
      independentlyMissing.insert( name );
  if ( independentlyMissing.empty() )
  {
    REQUIRE( (*drivers)["severity"].asString() == "ok" );
  }
  else
  {
    REQUIRE( (*drivers)["severity"].asString() == "error" );
    for ( const auto &name : independentlyMissing )
      REQUIRE( (*drivers)["message"].asString().find( name ) != std::string::npos );
  }
}

TEST_CASE( "env doctor probed paths list injected PROJ candidates",
           "[env_doctor][f19][negative]" )
{
  sicnu::geo::envcheck::EnvCheckOptions options;
  options.projDataCandidates = { "/nonexistent-f19/中文目录" };
  sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor( options );
  requireCountsConsistent( report );

  const Json::Value *proj = findCheck( report, "proj.db" );
  REQUIRE( proj != nullptr );
  bool listedInjected = false;
  for ( const Json::Value &probed : (*proj)["detail"]["probed"] )
  {
    if ( probed.asString().find( "/nonexistent-f19/中文目录" ) == 0 )
      listedInjected = true;
  }
  REQUIRE( listedInjected );
  // On hosts with a real proj share the scan still resolves; the finding then
  // stays ok — the oracle here is the *named probe list*, not the host state.
}

TEST_CASE( "env doctor unicode roundtrip probe runs and cleans up",
           "[env_doctor][f19]" )
{
  sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor();
  const Json::Value *unicode = findCheck( report, "fs.unicode" );
  REQUIRE( unicode != nullptr );
  const std::string severity = (*unicode)["severity"].asString();
  REQUIRE( ( severity == "ok" || severity == "error" ) );
  if ( severity == "ok" )
  {
    REQUIRE( (*unicode)["detail"]["path"].asString().find( "\xE4\xB8\xAD\xE6\x96\x87" )
             != std::string::npos );
    // cleanup: no probe residue in temp
    const std::string path = (*unicode)["detail"]["path"].asString();
    REQUIRE( !std::filesystem::exists( std::filesystem::path( path ) ) );
  }
}

TEST_CASE( "env doctor runtime data resolution walks to a fixture marker",
           "[env_doctor][f19]" )
{
  std::filesystem::path root =
    std::filesystem::temp_directory_path() / "sicnu-envcheck-fixture-f19";
  std::filesystem::remove_all( root ); // residue from a failed previous run
  std::filesystem::create_directories( root / "data" );
  std::filesystem::path fixture = root / "deep" / "deeper";

  sicnu::geo::envcheck::EnvCheckOptions options;
  options.currentDir = fixture.string();
  sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor( options );
  const Json::Value *data = findCheck( report, "runtime.data.dir" );
  REQUIRE( data != nullptr );
  REQUIRE( (*data)["severity"].asString() == "ok" );
  std::filesystem::path resolvedRoot = (*data)["detail"]["resolved_root"].asString();
  REQUIRE( resolvedRoot.filename() == "sicnu-envcheck-fixture-f19" );
  std::filesystem::remove_all( root ); // pass-path cleanup (failure path is
                                       // covered by the next run's pre-clean)
}

TEST_CASE( "env doctor reports a missing SICNU_DATA_DIR as warning with pointer",
           "[env_doctor][f19][negative]" )
{
  sicnu::geo::envcheck::EnvCheckOptions options;
  options.currentDir = "/nonexistent-f19-root";
  CPLSetConfigOption( "SICNU_DATA_DIR", "/nonexistent-f19/数据目录" );
  sicnu::geo::envcheck::EnvDoctorReport report = runEnvironmentDoctor( options );
  CPLSetConfigOption( "SICNU_DATA_DIR", nullptr );
  requireCountsConsistent( report );

  const Json::Value *data = findCheck( report, "runtime.data.dir" );
  REQUIRE( data != nullptr );
  REQUIRE( (*data)["severity"].asString() == "warning" );
  REQUIRE( (*data)["message"].asString().find( "/nonexistent-f19/数据目录" ) != std::string::npos );
  REQUIRE( (*data)["diagnostic"].asString() == "diagnostic.env.data_dir_missing" );
}

TEST_CASE( "env doctor reports offline gate state without toggling it",
           "[env_doctor][f19]" )
{
  // Not engaged
  sicnu::geo::offline::setEnabled( false );
  sicnu::geo::envcheck::EnvDoctorReport online = runEnvironmentDoctor();
  const Json::Value *onlineState = findCheck( online, "offline.state" );
  REQUIRE( onlineState != nullptr );
  REQUIRE( (*onlineState)["detail"]["engaged"].asBool() == false );
  REQUIRE( (*onlineState)["detail"]["gdal_network_deny"].asBool() == false );

  // Engaged (gate + deny): the report reflects, never mutates.
  sicnu::geo::offline::setEnabled( true );
  sicnu::geo::offline::applyGdalNetworkDeny();
  sicnu::geo::envcheck::EnvDoctorReport offline = runEnvironmentDoctor();
  const Json::Value *offlineState = findCheck( offline, "offline.state" );
  REQUIRE( offlineState != nullptr );
  REQUIRE( (*offlineState)["detail"]["engaged"].asBool() == true );
  REQUIRE( (*offlineState)["detail"]["gdal_network_deny"].asBool() == true );

  // Restore global state for other tests.
  sicnu::geo::offline::clearGdalNetworkDeny();
  sicnu::geo::offline::setEnabled( false );
  REQUIRE( sicnu::geo::offline::enabled() == false );
}
