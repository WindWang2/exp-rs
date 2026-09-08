/***************************************************************************
  tests/test_io_doctor.cpp — Data Doctor suite (Phase 10): read-only,
  structured diagnostics over rasters, vectors and broken sources.
 ***************************************************************************/

#include "geospatial/doctor/data_doctor.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_doctor" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}
} // namespace

TEST_CASE( "doctor reports structured findings for a healthy raster", "[io][doctor]" )
{
  const std::string target = ( fs::path( scratch( "healthy" ) ) / "healthy.tif" ).string();
  sicnu::geo::RasterBandSpec spec;
  spec.hasNoData = true;
  spec.noDataValue = 0.0;
  spec.role = "Red";
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 64, 64, { spec }, {} );
  writer.setGeotransform( { 116.0, 0.01, 0.0, 31.0, 0.0, -0.01 } );
  writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
  sicnu::geo::RasterWindow full;
  full.width = 64;
  full.height = 64;
  std::vector<double> values( 64 * 64, 5.0 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  const sicnu::geo::DoctorReport report = sicnu::geo::runDoctor( target );
  CHECK( report.readable );
  CHECK( report.kind == "raster" );
  CHECK( report.driver == "GTiff" );
  CHECK( report.errorCount == 0 );

  bool crsOk = false;
  bool nodataOk = false;
  bool geotransformOk = false;
  for ( const Json::Value &finding : report.findings )
  {
    if ( finding["check"].asString() == "crs" && finding["severity"].asString() == "ok" )
      crsOk = true;
    if ( finding["check"].asString() == "nodata" && finding["severity"].asString() == "ok" )
      nodataOk = true;
    if ( finding["check"].asString() == "geotransform" && finding["severity"].asString() == "ok" )
      geotransformOk = true;
  }
  CHECK( crsOk );
  CHECK( nodataOk );
  CHECK( geotransformOk );

  // Strict read-only guarantee: a second run produces identical output and no
  // .aux.xml or PAM sidecars appear from the doctor itself.
  const auto sizeBefore = sicnu::geo::atomic_fs::fileSize( target );
  sicnu::geo::runDoctor( target, sicnu::geo::InspectOptions{ true, false, 64, 64 } );
  CHECK( sicnu::geo::atomic_fs::fileSize( target ) == sizeBefore );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target + ".aux.xml" ) );
}

TEST_CASE( "doctor flags missing CRS, geotransform and nodata as warnings", "[io][doctor][warnings]" )
{
  const std::string target = ( fs::path( scratch( "bare" ) ) / "bare.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 8, 8, { {} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 8;
  full.height = 8;
  std::vector<double> values( 64, 1.0 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  const sicnu::geo::DoctorReport report = sicnu::geo::runDoctor( target );
  CHECK( report.readable );
  CHECK( report.warningCount >= 3 ); // crs + geotransform + nodata
  int warnings = 0;
  for ( const Json::Value &finding : report.findings )
    if ( finding["severity"].asString() == "warning" )
      ++warnings;
  CHECK( warnings == report.warningCount );
}

TEST_CASE( "doctor keeps working on unreadable and malformed sources", "[io][doctor][broken]" )
{
  CHECK_FALSE( sicnu::geo::runDoctor( "" ).readable );

  const std::string missing = ( fs::path( scratch( "broken" ) ) / "missing.tif" ).string();
  const sicnu::geo::DoctorReport missingReport = sicnu::geo::runDoctor( missing );
  CHECK_FALSE( missingReport.readable );
  CHECK( missingReport.kind == "unreadable" );
  CHECK( missingReport.errorCount >= 2 ); // readability + existence
  bool hasExistence = false;
  for ( const Json::Value &finding : missingReport.findings )
    hasExistence = hasExistence || finding["check"].asString() == "existence";
  CHECK( hasExistence );

  const std::string junk = ( fs::path( scratch( "broken" ) ) / "junk.tif" ).string();
  { std::ofstream junkOut( junk ); junkOut << "definitely not a tiff"; }
  const sicnu::geo::DoctorReport junkReport = sicnu::geo::runDoctor( junk );
  CHECK_FALSE( junkReport.readable );
}

TEST_CASE( "runInspect returns canonical metadata for any supported kind", "[io][doctor][inspect]" )
{
  const std::string target = ( fs::path( scratch( "inspect" ) ) / "plain.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, {} );
  writer.finalize();

  const Json::Value inspected = sicnu::geo::runInspect( target );
  CHECK( inspected["kind"].asString() == "raster" );
  CHECK( inspected["width"].asInt() == 4 );
}
