/***************************************************************************
  tests/test_io_param_guard.cpp — io boundary path guards (11.0).
  Known-answer + negative tests for checkSourcePath/checkTargetPath on the
  ResourceUri authority. The redaction oracle is the display() contract:
  credentials must never survive into a log/error surface.
 ***************************************************************************/

#include "geospatial/io/param_guard.h"
#include "geospatial/common.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

using sicnu::geo::ErrorCode;
using sicnu::geo::GeoError;
using sicnu::geo::io::checkSourcePath;
using sicnu::geo::io::checkTargetPath;

namespace
{

std::string scratch()
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_param_guard";
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

} // namespace

TEST_CASE( "checkSourcePath refuses empty and passes local paths through unchanged", "[io][param_guard]" )
{
  REQUIRE_THROWS_AS( checkSourcePath( "" ), GeoError );

  const std::string dir = scratch();
  const std::string file = dir + "/数据_é.tif";
  { std::ofstream out( file, std::ios::binary ); out << "x"; }

  const sicnu::geo::io::CheckedPath checked = checkSourcePath( file );
  CHECK( checked.raw == file );
  CHECK( checked.isLocalPayload );
  CHECK( !checked.isRemote );
  // The raw form opens GDAL; the display form is for humans. Unicode must
  // survive both (no code-page re-encoding anywhere on this path).
  CHECK( checked.display.find( "数据_é" ) != std::string::npos );
}

TEST_CASE( "checkSourcePath display form redacts credentials", "[io][param_guard][redaction]" )
{
  const sicnu::geo::io::CheckedPath checked = checkSourcePath( "https://user:secret123@example.com/eo/scene.tif" );
  CHECK( checked.isRemote );
  CHECK( checked.display.find( "secret123" ) == std::string::npos );
  // Identity keeps the bytes (never displayed), display masks them.
  const std::string canonical = checked.canonical;
  CHECK( canonical.find( "example.com" ) != std::string::npos );
}

TEST_CASE( "checkTargetPath refuses remote and read-only projection kinds", "[io][param_guard][negative]" )
{
  try
  {
    checkTargetPath( "https://example.com/out.tif" );
    FAIL( "remote target must be refused" );
  }
  catch ( const GeoError &error )
  {
    CHECK( error.code() == ErrorCode::Unsupported );
    CHECK( error.details()["reason"].asString() == "remote_write_offline_policy" );
  }

  try
  {
    checkTargetPath( "vrt://something" );
    FAIL( "virtual dataset target must be refused" );
  }
  catch ( const GeoError &error )
  {
    CHECK( error.code() == ErrorCode::Unsupported );
    CHECK( error.details()["reason"].asString() == "read_only_projection_kind" );
  }

  REQUIRE_THROWS_AS( checkTargetPath( "" ), GeoError );
}

TEST_CASE( "checkTargetPath accepts local and /vsimem targets", "[io][param_guard]" )
{
  const std::string dir = scratch();
  const sicnu::geo::io::CheckedPath local = checkTargetPath( dir + "/out.tif" );
  CHECK( local.isLocalPayload );

  const sicnu::geo::io::CheckedPath mem = checkTargetPath( "/vsimem/out.tif" );
  CHECK( !mem.isRemote ); // memory target: staged pipeline applies, no network
}

TEST_CASE( "checkTargetPath refuses targets in missing directories", "[io][param_guard][negative]" )
{
  const std::string dir = scratch();
  const std::string missingParent = dir + "/不存在目录/out.tif";
  try
  {
    checkTargetPath( missingParent );
    FAIL( "target in a missing directory must be refused" );
  }
  catch ( const GeoError &error )
  {
    CHECK( error.code() == ErrorCode::InvalidArgument );
    CHECK( error.details()["reason"].asString() == "target_directory_missing" );
  }
}
