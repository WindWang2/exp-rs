/***************************************************************************
  tests/test_io_fidelity.cpp — Phase 13: metadata fidelity policy suite.
  NoData/NaN/mask/scale/offset/unit/color-table round-trips; silent
  float→byte and silent CRS guessing are forbidden.
 ***************************************************************************/

#include "geospatial/convert/raster_convert.h"
#include "geospatial/cog/cog_presets.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/doctor/data_doctor.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_fidelity" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}
} // namespace

TEST_CASE( "scale/offset/unit round-trip through GTiff conversions", "[io][fidelity][scaling]" )
{
  const std::string dir = scratch( "scaling" );
  const std::string source = ( fs::path( dir ) / "src.tif" ).string();
  sicnu::geo::RasterBandSpec spec;
  spec.dtype = "Float32";
  spec.hasNoData = true;
  spec.noDataValue = -9999.0;
  spec.hasScale = true;
  spec.scale = 0.001;
  spec.hasOffset = true;
  spec.offset = 273.15;
  spec.unit = "kelvin";
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( source, 16, 16, { spec }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 16;
  full.height = 16;
  std::vector<double> values( 256 );
  for ( std::size_t i = 0; i < values.size(); ++i )
    values[i] = ( i % 9 == 0 ) ? -9999.0 : static_cast<double>( i ) * 10.0;
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  // DEFLATE GTiff → GTiff keeps the calibration vocabulary exactly.
  const std::string target = ( fs::path( dir ) / "out.tif" ).string();
  sicnu::geo::TranslateOptions options;
  options.creationOptions = { "COMPRESS=DEFLATE" };
  sicnu::geo::translateRaster( source, target, options );

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const sicnu::geo::BandInfo &band = reader.metadata().bands.at( 0 );
  CHECK( band.hasScale );
  CHECK( band.scale == Approx( 0.001 ) );
  CHECK( band.hasOffset );
  CHECK( band.offset == Approx( 273.15 ) );
  CHECK( band.unit == "kelvin" );
  CHECK( band.hasNoData );
  CHECK( band.noDataValue == Approx( -9999.0 ) );

  // Stored values unchanged; physical values derived only explicitly.
  const std::vector<double> stored = reader.readWindow( { 1 }, full );
  CHECK( stored[0] == Approx( -9999.0 ) );
  CHECK( sicnu::geo::RasterReader::applyScaleOffset( band, stored[1] ) == Approx( 10.0 * 0.001 + 273.15 ) );

  // PNG (visualization) drops scale/offset: an Accessible-only profile — the
  // doctor must WARN, and conversion claims must not promise fidelity there.
  const std::string png = ( fs::path( dir ) / "viz.png" ).string();
  sicnu::geo::TranslateOptions toPng;
  toPng.outputFormat = "PNG";
  try
  {
    sicnu::geo::translateRaster( source, png, toPng );
    // PNG drops the fidelity carriers (no CRS/scale/offset): the doctor must
    // surface at least one degradation warning for the converted product.
    const sicnu::geo::DoctorReport report = sicnu::geo::runDoctor( png );
    CHECK( report.readable );
    CHECK( report.warningCount >= 1 );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    // Float32→PNG refusing outright is also acceptable fail-closed behavior.
    SUCCEED( "PNG conversion of float data refused (fail-closed)" );
  }
}

TEST_CASE( "color table survives when the target dtype supports it", "[io][fidelity][palette]" )
{
  const std::string dir = scratch( "palette" );
  const std::string source = ( fs::path( dir ) / "classes.tif" ).string();

  // Author a Byte class raster with a palette through raw GDAL, then convert.
  {
    sicnu::geo::ensureGdalRegistered();
    sicnu::geo::QuietCplErrors quiet;
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, source.c_str(), 4, 4, 1, GDT_Byte, nullptr );
    REQUIRE( dataset );
    GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
    GDALColorTableH table = GDALCreateColorTable( GPI_RGB );
    GDALColorEntry c0 = { 0, 0, 0, 0 };
    GDALColorEntry c1 = { 200, 30, 30, 255 };
    GDALColorEntry c2 = { 30, 200, 90, 255 };
    GDALSetColorEntry( table, 0, &c0 );
    GDALSetColorEntry( table, 1, &c1 );
    GDALSetColorEntry( table, 2, &c2 );
    GDALSetRasterColorTable( band, table );
    GDALDestroyColorTable( table );
    unsigned char pixels[16] = { 0, 1, 2, 1, 2, 2, 1, 0, 1, 2, 1, 2, 0, 1, 2, 2 };
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 4, 4, pixels, 4, 4, GDT_Byte, 0, 0 ) == CE_None );
    GDALClose( dataset );
  }

  // Byte→Byte conversion keeps the palette.
  const std::string target = ( fs::path( dir ) / "classes_copy.tif" ).string();
  sicnu::geo::TranslateOptions options;
  options.creationOptions = { "COMPRESS=LZW" };
  sicnu::geo::translateRaster( source, target, options );

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  CHECK( reader.metadata().bands.at( 0 ).hasColorTable );
  CHECK( reader.metadata().bands.at( 0 ).colorTableEntryCount >= 3 );
}

TEST_CASE( "float→byte narrowing never happens implicitly", "[io][fidelity][narrowing]" )
{
  // The write contract stores exactly what the caller provides; a Float32
  // source converted without -ot stays Float32. No helper in the foundation
  // performs dtype demotion, and the Categorical preset refuses float data.
  CHECK_THROWS_AS( sicnu::geo::cogPresetOptions( sicnu::geo::CogPreset::Categorical, "Float64" ),
                   sicnu::geo::GeoError );

  const std::string dir = scratch( "narrowing" );
  const std::string source = ( fs::path( dir ) / "f.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( source, 4, 4, { {} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 4;
  full.height = 4;
  std::vector<double> values( 16, 0.5 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  const std::string target = ( fs::path( dir ) / "f_copy.tif" ).string();
  sicnu::geo::TranslateOptions defaultOptions;
  sicnu::geo::translateRaster( source, target, defaultOptions );
  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  CHECK( reader.metadata().bands.at( 0 ).dtype == "Float32" ); // no silent demotion
  const std::vector<double> stored = reader.readWindow( { 1 }, full );
  CHECK( stored[0] == Approx( 0.5 ) );
}

TEST_CASE( "NaN fidelity: NaN nodata is preserved through lossless conversion", "[io][fidelity][nan]" )
{
  const std::string dir = scratch( "nan" );
  const std::string source = ( fs::path( dir ) / "nan.tif" ).string();
  sicnu::geo::RasterBandSpec spec;
  spec.dtype = "Float32";
  spec.hasNoData = true;
  spec.noDataIsNaN = true;
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( source, 4, 4, { spec }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 4;
  full.height = 4;
  std::vector<double> values( 16, 1.0 );
  values[7] = std::numeric_limits<double>::quiet_NaN();
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  const std::string target = ( fs::path( dir ) / "nan_copy.tif" ).string();
      sicnu::geo::TranslateOptions lossless;
    lossless.creationOptions = { "COMPRESS=DEFLATE" };
    sicnu::geo::translateRaster( source, target, lossless );
  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const sicnu::geo::BandInfo &band = reader.metadata().bands.at( 0 );
  CHECK( band.hasNoData );
  CHECK( band.noDataIsNaN );
  const std::vector<double> stored = reader.readWindow( { 1 }, full );
  CHECK( std::isnan( stored[7] ) );
  const std::vector<std::uint8_t> mask = reader.readMask( full, { 1 } );
  CHECK( mask[7] == 0 );
}
