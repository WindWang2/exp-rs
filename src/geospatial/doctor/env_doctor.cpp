/***************************************************************************
  geospatial/doctor/env_doctor.cpp
  Deployment 11.0 (F19) — local runtime environment self-check.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/doctor/env_doctor.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/remote/offline_gate.h"

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_spatialref.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sicnu::geo::envcheck
{
namespace
{

std::string envOrEmpty( const char *name )
{
  if ( const char *v = CPLGetConfigOption( name, nullptr ) )
  {
    if ( *v )
      return std::string( v );
  }
  if ( const char *e = std::getenv( name ) )
    return std::string( e );
  return std::string();
}

bool dirExists( const std::string &path )
{
  std::error_code ec;
  return std::filesystem::is_directory( path, ec );
}

std::string joinStrings( const std::vector<std::string> &parts, const std::string &sep )
{
  std::string out;
  for ( std::size_t i = 0; i < parts.size(); ++i )
  {
    if ( i )
      out += sep;
    out += parts[i];
  }
  return out;
}

std::string currentProcessId()
{
#ifdef _WIN32
  return std::to_string( static_cast<long long>( GetCurrentProcessId() ) );
#else
  return std::to_string( static_cast<long long>( ::getpid() ) );
#endif
}

/// Writes a unique scratch file under @p dir and deletes it. Returns empty on
/// success, else a failure description. Every failure path still removes the
/// file it created (failure cleanup).
std::string probeWritable( const std::filesystem::path &dir, std::string &probedPath )
{
  std::error_code ec;
  if ( !std::filesystem::create_directories( dir, ec ) && ec )
    return "cannot create directory: " + ec.message();
  const std::filesystem::path file =
    dir / ( "sicnu-envcheck-" + currentProcessId() + ".tmp" );
  probedPath = file.string();
  if ( std::filesystem::exists( file, ec ) )
  {
    std::filesystem::remove( file, ec );
    ec.clear();
  }
  std::FILE *f = std::fopen( probedPath.c_str(), "w+b" );
  if ( !f )
    return "cannot create file: " + probedPath;
  const char *payload = "sicnu";
  if ( std::fwrite( payload, 1, 5, f ) != 5 )
  {
    std::fclose( f );
    std::remove( probedPath.c_str() );
    return "cannot write file: " + probedPath;
  }
  std::fclose( f );
  {
    std::ifstream in( probedPath, std::ios::binary );
    char buf[6] = { 0 };
    in.read( buf, 5 );
    if ( !in || std::string( buf, buf + 5 ) != payload )
    {
      std::remove( probedPath.c_str() );
      return "write-then-read mismatch: " + probedPath;
    }
  }
  if ( std::remove( probedPath.c_str() ) != 0 )
    return "cannot delete probe file: " + probedPath;
  return std::string();
}

void addSeverity( EnvDoctorReport &report, const char *severity )
{
  if ( std::strcmp( severity, "error" ) == 0 )
    ++report.errorCount;
  else if ( std::strcmp( severity, "warning" ) == 0 )
    ++report.warningCount;
  else if ( std::strcmp( severity, "info" ) == 0 )
    ++report.infoCount;
  else
    ++report.okCount;
}

void emitFinding( EnvDoctorReport &report, const char *severity, const char *checkId,
           const std::string &message, const Json::Value &detail = Json::Value(),
           const char *diagnostic = "" )
{
  EnvFinding finding;
  finding.check = checkId;
  finding.severity = severity;
  finding.message = message;
  finding.detail = detail;
  finding.diagnostic = diagnostic;
  addSeverity( report, severity );
  report.checks.append( finding.toJson() );
}

std::string platformName()
{
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#elif defined(__unix__)
  return "linux";
#else
  return "unknown";
#endif
}

// ---------------------------------------------------------------- GDAL probes

const char *kRequiredDrivers[] = { "GTiff", "GPKG", "GeoJSON", "ESRI Shapefile", "MEM", "VRT" };

void checkGdal( EnvDoctorReport &report, const EnvCheckOptions &options )
{
  ensureGdalRegistered();
  // GDALVersionInfo("RELEASE_NAME") is not reliable across builds (observed
  // returning garbage on GDAL 3.13); the "--version" string is the stable
  // surface. Release = the token right after the leading "GDAL ".
  const char *versionFull = GDALVersionInfo( "--version" );
  const std::string full( versionFull ? versionFull : "" );
  std::string release;
  if ( full.rfind( "GDAL ", 0 ) == 0 )
  {
    const std::string rest = full.substr( 5 );
    const std::string::size_type space = rest.find( ' ' );
    release = space == std::string::npos ? rest : rest.substr( 0, space );
  }
  Json::Value detail;
  detail["release"] = release;
  detail["full"] = full;
  emitFinding( report, "ok", "gdal.version", "GDAL " + ( release.empty() ? full : release ),
               detail );

  const int registered = GDALGetDriverCount();
  if ( registered <= 0 )
  {
    emitFinding( report, "error", "gdal.drivers", "GDAL driver registry is empty",
          Json::Value(), "diagnostic.env.gdal_drivers_empty" );
    return;
  }
  Json::Value countDetail;
  countDetail["registered"] = registered;
  emitFinding( report, "ok", "gdal.drivers",
        std::to_string( registered ) + " GDAL drivers registered", countDetail );

  // Required-for-the-labs driver set: every absence is named (Oracle 3 —
  // diagnosis points at the specific missing driver). C API on purpose: no
  // gdal_priv.h surface in the Qt-free layer.
  std::vector< std::string > required;
  if ( options.requiredDrivers.empty() )
  {
    for ( const char *name : kRequiredDrivers )
      required.emplace_back( name );
  }
  else
  {
    required = options.requiredDrivers;
  }
  std::vector< std::string > missing;
  for ( const auto &name : required )
  {
    if ( !GDALGetDriverByName( name.c_str() ) )
      missing.push_back( name );
  }
  if ( missing.empty() )
  {
    emitFinding( report, "ok", "gdal.drivers.required", "required GDAL drivers present" );
  }
  else
  {
    Json::Value md;
    Json::Value list( Json::arrayValue );
    for ( const auto &m : missing )
      list.append( m );
    md["missing"] = list;
    emitFinding( report, "error", "gdal.drivers.required",
          "missing required GDAL driver(s): " + joinStrings( missing, ", " ),
          md, "diagnostic.env.gdal_driver_missing" );
  }

  const std::string gdalData = envOrEmpty( "GDAL_DATA" );
  Json::Value dd;
  dd["path"] = gdalData;
  if ( gdalData.empty() )
  {
    // GDAL >= 3 auto-locates its data directory relative to the shared
    // library; absence of the env var is only an info when drivers register.
    emitFinding( report, "info", "gdal.data_dir",
          "GDAL_DATA not set (GDAL auto-location in effect)" );
  }
  else if ( !dirExists( gdalData ) )
  {
    emitFinding( report, "error", "gdal.data_dir", "GDAL_DATA set but missing: " + gdalData,
          dd, "diagnostic.env.gdal_data_missing" );
  }
  else
  {
    emitFinding( report, "ok", "gdal.data_dir", "GDAL_DATA resolved: " + gdalData, dd );
  }
}

// ----------------------------------------------------------------- PROJ probes

void checkProj( EnvDoctorReport &report, const EnvCheckOptions &options )
{
  // Candidate scan names every probed path so a missing proj.db is a pointer,
  // not a guess (Oracle 3).
  std::vector< std::string > candidates = options.projDataCandidates;
  for ( const char *name : { "PROJ_DATA", "PROJ_LIB" } )
  {
    const std::string v = envOrEmpty( name );
    if ( !v.empty() )
      candidates.push_back( v );
  }
  const std::string gdalData = envOrEmpty( "GDAL_DATA" );
  if ( !gdalData.empty() )
  {
    const std::string stem = gdalData.substr( 0, gdalData.find_last_of( "/\\" ) );
    if ( !stem.empty() )
      candidates.push_back( stem + "/proj" );
  }
  candidates.push_back( "/usr/share/proj" );
  candidates.push_back( "/usr/local/share/proj" );
  candidates.push_back( "/usr/share/QGIS/share/proj" );

  std::vector< std::string > probedPaths;
  for ( const auto &c : candidates )
    probedPaths.push_back( c + "/proj.db" );

  std::string projDb;
  for ( const auto &candidate : probedPaths )
  {
    std::error_code ec;
    if ( std::filesystem::exists( candidate, ec ) )
    {
      projDb = candidate;
      break;
    }
  }
  Json::Value pd;
  Json::Value probed( Json::arrayValue );
  for ( const auto &p : probedPaths )
    probed.append( p );
  pd["probed"] = probed;
  if ( projDb.empty() )
  {
    emitFinding( report, "error", "proj.db",
          "proj.db not found (probed " + std::to_string( probedPaths.size() )
            + " candidates: " + joinStrings( probedPaths, ", " ) + ")",
          pd, "diagnostic.env.proj_db_missing" );
    // The operational probe below still runs: it records what GDAL/OSR sees.
  }
  else
  {
    pd["resolved"] = projDb;
    emitFinding( report, "ok", "proj.db", "proj.db resolved: " + projDb, pd );
  }

  // Operational CRS resolve: this is what grading actually needs (EPSG
  // authorities). Fails when proj.db is missing, unreadable or corrupt. CPL
  // noise is silenced — the finding is the honest report, not a raw traceback.
  OGRSpatialReference srs;
  OGRErr err;
  {
    QuietCplErrors quiet;
    err = srs.importFromEPSG( 4326 );
  }
  if ( err != OGRERR_NONE )
  {
    emitFinding( report, "error", "proj.crs.resolve",
          "EPSG:4326 import failed (PROJ database unusable in this process)",
          Json::Value(), "diagnostic.env.proj_db_unusable" );
    return;
  }
  char *wkt = nullptr;
  bool exported = false;
  {
    QuietCplErrors quiet;
    exported = srs.exportToWkt( &wkt ) == OGRERR_NONE && wkt != nullptr;
  }
  if ( exported )
    CPLFree( wkt );
  if ( exported )
    emitFinding( report, "ok", "proj.crs.resolve", "EPSG:4326 roundtrip through PROJ OK" );
  else
    emitFinding( report, "error", "proj.crs.resolve",
          "EPSG:4326 imported but WKT export failed", Json::Value(),
          "diagnostic.env.proj_db_unusable" );
}

// --------------------------------------------------------- runtime data probes

/// Mirrors processing::resolveRuntimeDataPath's directory-walk (exe dir, then
/// cwd, walking up to a "data" or CMakeLists.txt marker) without pulling Qt
/// into this layer. Reports what the resolution would find for the well-known
/// marker directory "data".
void checkRuntimeData( EnvDoctorReport &report, const EnvCheckOptions &options )
{
  const std::string envDataDir = envOrEmpty( "SICNU_DATA_DIR" );
  std::vector< std::string > roots;
  if ( !options.applicationDir.empty() )
    roots.push_back( options.applicationDir );
  if ( !options.currentDir.empty() )
    roots.push_back( options.currentDir );
  else
  {
    std::error_code ec;
    const std::string cwd = std::filesystem::current_path( ec ).string();
    if ( !ec )
      roots.push_back( cwd );
  }

  auto walkToMarker = []( const std::string &start, std::string &found ) {
    std::error_code ec;
    std::filesystem::path dir( start );
    for ( int i = 0; i < 32; ++i )
    {
      if ( std::filesystem::is_directory( dir / "data", ec )
           || std::filesystem::exists( dir / "CMakeLists.txt", ec ) )
      {
        found = dir.string();
        return true;
      }
      if ( !dir.has_parent_path() || dir.parent_path() == dir )
        break;
      dir = dir.parent_path();
    }
    return false;
  };

  Json::Value rd;
  if ( !envDataDir.empty() )
  {
    rd["SICNU_DATA_DIR"] = envDataDir;
    if ( dirExists( envDataDir ) )
    {
      rd["source"] = "env";
      emitFinding( report, "ok", "runtime.data.dir",
            "SICNU_DATA_DIR resolved: " + envDataDir, rd );
    }
    else
    {
      emitFinding( report, "warning", "runtime.data.dir",
            "SICNU_DATA_DIR is set but missing: " + envDataDir, rd,
            "diagnostic.env.data_dir_missing" );
    }
    return;
  }
  for ( const auto &root : roots )
  {
    std::string markerRoot;
    if ( walkToMarker( root, markerRoot ) )
    {
      rd["resolved_root"] = markerRoot;
      rd["source"] = "marker-walk from " + root;
      emitFinding( report, "ok", "runtime.data.dir",
            "runtime data located (" + rd["source"].asString() + ")", rd );
      return;
    }
  }
  emitFinding( report, "warning", "runtime.data.dir",
        "runtime data tree not located (set SICNU_DATA_DIR; probed exe/cwd walks)",
        rd, "diagnostic.env.data_dir_unresolved" );
}

// ------------------------------------------------------------ scratch FS probes

void checkFilesystem( EnvDoctorReport &report )
{
  std::error_code ec;
  const std::filesystem::path temp = std::filesystem::temp_directory_path( ec );
  if ( ec )
  {
    emitFinding( report, "error", "fs.temp", "system temp directory cannot be resolved",
          Json::Value(), "diagnostic.env.temp_unresolved" );
    return;
  }
  std::string probed;
  const std::string failure = probeWritable( temp, probed );
  Json::Value td;
  td["path"] = temp.string();
  if ( failure.empty() )
  {
    emitFinding( report, "ok", "fs.temp", "temp directory writable", td );
  }
  else
  {
    td["probe"] = probed;
    td["error"] = failure;
    emitFinding( report, "error", "fs.temp", "temp directory not writable: " + failure,
          td, "diagnostic.env.temp_not_writable" );
  }

  // Unicode path roundtrip: classroom machines and Chinese student data hit
  // this constantly (package C). Probed under temp, cleaned up on all paths.
#ifdef _WIN32
  const std::filesystem::path unicodeDir = temp / L"中文目录_诊断";
#else
  const std::filesystem::path unicodeDir = temp / "\xE4\xB8\xAD\xE6\x96\x87\xE7\x9B\xAE\xE5\xBD\x95_\xE8\xAF\x8A\xE6\x96\xAD";
#endif
  const std::string unicodeFailure = probeWritable( unicodeDir, probed );
  Json::Value ud;
  ud["path"] = unicodeDir.string();
  if ( unicodeFailure.empty() )
  {
    emitFinding( report, "ok", "fs.unicode", "unicode path roundtrip OK", ud );
  }
  else
  {
    ud["error"] = unicodeFailure;
    emitFinding( report, "error", "fs.unicode", "unicode path roundtrip failed: " + unicodeFailure,
          ud, "diagnostic.env.unicode_path_failed" );
  }
  std::filesystem::remove_all( unicodeDir, ec );
}

// --------------------------------------------------------------- offline probes

void checkOfflineState( EnvDoctorReport &report )
{
  Json::Value od;
  const bool engaged = offline::enabled();
  const bool fromEnv = offline::enabledFromEnv();
  od["engaged"] = engaged;
  od["source"] = fromEnv ? std::string( "SICNU_OFFLINE env" )
                         : std::string( engaged ? "flag" : "none" );
  const char *deny = CPLGetConfigOption( "CPL_VSIL_CURL_ALLOWED_EXTENSIONS", nullptr );
  od["gdal_network_deny"] = deny != nullptr;
  if ( engaged )
    emitFinding( report, "info", "offline.state", "offline gate engaged (remote opens refused)", od );
  else
    emitFinding( report, "info", "offline.state", "offline gate not engaged (online mode)", od );
}

} // namespace

Json::Value EnvFinding::toJson() const
{
  Json::Value v;
  v["check"] = check;
  v["severity"] = severity;
  v["message"] = message;
  if ( !detail.isNull() && !detail.empty() )
    v["detail"] = detail;
  if ( !diagnostic.empty() )
    v["diagnostic"] = diagnostic;
  return v;
}

Json::Value EnvDoctorReport::toJson() const
{
  Json::Value v;
  v["schema"] = "exp.env.report.v1";
  Json::Value host;
  host["platform"] = platform;
  v["host"] = host;
  v["checks"] = checks;
  Json::Value counts;
  counts["ok"] = okCount;
  counts["info"] = infoCount;
  counts["warning"] = warningCount;
  counts["error"] = errorCount;
  v["counts"] = counts;
  return v;
}

const char *EnvDoctorReport::worst() const
{
  if ( errorCount > 0 )
    return "error";
  if ( warningCount > 0 )
    return "warning";
  if ( infoCount > 0 )
    return "info";
  return "ok";
}

EnvDoctorReport runEnvironmentDoctor( const EnvCheckOptions &options )
{
  EnvDoctorReport report;
  report.platform = platformName();
  report.checks = Json::Value( Json::arrayValue );
  checkGdal( report, options );
  checkProj( report, options );
  checkRuntimeData( report, options );
  checkFilesystem( report );
  checkOfflineState( report );
  return report;
}

} // namespace sicnu::geo::envcheck
