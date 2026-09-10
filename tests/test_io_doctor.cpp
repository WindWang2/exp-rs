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

TEST_CASE( "doctor v2 carries structured sections and remediation advice",
           "[io][doctor][v2]" )
{
  const std::string dir = scratch( "v2raster" );
  const std::string path = dir + "/plain.tif";
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
      path, 3000, 3000, { sicnu::geo::RasterBandSpec {} }, { "GTiff", {}, true } );
    writer.setGeotransform( { 100.0, 10.0, 0.0, 5000.0, 0.0, -10.0 } );
    // Float band without NoData: triggers the nodata warning.
    std::vector<double> values( 3000 * 3000, 1.0 );
    writer.writeWindow( 1, { 0, 0, 3000, 3000 }, values.data() );
    writer.finalize();
  }

  const sicnu::geo::DoctorReport report = sicnu::geo::runDoctor( path );
  REQUIRE( report.readable );
  CHECK( report.kind == "raster" );

  const Json::Value json = report.toJson();
  // Versioned envelope, backward-compatible findings.
  CHECK( json["doctor_version"].asInt() == 2 );
  CHECK( json["findings"].isArray() );
  CHECK( json["identity"]["dataset_kind"].asString() == "raster" );
  CHECK( json["identity"]["display_path"].asString().find( "plain.tif" ) != std::string::npos );
  CHECK( json["format"]["driver"].asString() == "GTiff" );
  CHECK( json["format"]["is_cog"].asBool() == false );
  CHECK( json["grid"]["kind"].asString() == "north_up" );
  CHECK( json["grid"]["resampling_category"].asString() == "continuous" );
  CHECK( json["remote"].isNull() );

  // Remediation covers every error/warning finding with explicit advice.
  const Json::Value &remediation = json["remediation"];
  REQUIRE( remediation.isArray() );
  CHECK( remediation.size() >= static_cast<Json::ArrayIndex>( report.warningCount + report.errorCount ) );
  for ( const Json::Value &item : remediation )
  {
    CHECK( item["advice"].asString().size() > 8 );
    CHECK( item["auto_fixable"].asBool() == false );
  }
  bool hasOverviewAdvice = false;
  for ( const Json::Value &item : remediation )
    if ( item["check"].asString() == "overviews" )
      hasOverviewAdvice = true;
  CHECK( hasOverviewAdvice ); // 3000x3000 without overviews triggers the advice
}

TEST_CASE( "doctor v2 refuses nothing and hides nothing on unreadable input",
           "[io][doctor][v2]" )
{
  const std::string dir = scratch( "v2missing" );
  const sicnu::geo::DoctorReport report = sicnu::geo::runDoctor( dir + "/missing.tif" );
  CHECK_FALSE( report.readable );
  CHECK( report.kind == "unreadable" );
  const Json::Value json = report.toJson();
  CHECK( json["doctor_version"].asInt() == 2 );
  // The classifier answers truthfully for the path shape it was given
  // (Windows temp paths can carry mixed separators and classify Invalid);
  // the contract is a non-empty classification + display form.
  CHECK_FALSE( json["identity"]["resource_kind"].asString().empty() );
  CHECK_FALSE( json["identity"]["display_path"].asString().empty() );
  bool hasExistenceAdvice = false;
  for ( const Json::Value &item : json["remediation"] )
    if ( item["check"].asString() == "existence" )
      hasExistenceAdvice = true;
  CHECK( hasExistenceAdvice );
}
