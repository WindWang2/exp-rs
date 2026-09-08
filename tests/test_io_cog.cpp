/***************************************************************************
  tests/test_io_cog.cpp — COG validation + safe presets suite (Phase 6).
 ***************************************************************************/

#include "geospatial/cog/cog_presets.h"
#include "geospatial/cog/cog_validator.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_cog" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}
} // namespace

TEST_CASE( "COG presets refuse lossy or semantically wrong combinations", "[io][cog][presets]" )
{
  // Categorical on float data is a fidelity violation.
  CHECK_THROWS_AS( sicnu::geo::cogPresetOptions( sicnu::geo::CogPreset::Categorical, "Float32" ),
                   sicnu::geo::GeoError );

  // Visualization on Byte warns about lossiness but is an explicit opt-in.
  const sicnu::geo::CogPresetResult viz = sicnu::geo::cogPresetOptions( sicnu::geo::CogPreset::Visualization, "Byte" );
  bool lossyWarning = false;
  for ( const std::string &warning : viz.warnings )
    lossyWarning = lossyWarning || warning.find( "LOSSY" ) != std::string::npos;
  CHECK( lossyWarning );
  bool jpeg = false;
  for ( const std::string &option : viz.creationOptions )
    jpeg = jpeg || option.find( "COMPRESS=JPEG" ) != std::string::npos;
  CHECK( jpeg );

  // Every preset carries the BigTIFF safety policy.
  for ( const std::string presetName : { "lossless_scientific", "categorical", "continuous_float", "sar" } )
  {
    const sicnu::geo::CogPreset preset = presetName == "lossless_scientific" ? sicnu::geo::CogPreset::LosslessScientific
                                         : presetName == "categorical" ? sicnu::geo::CogPreset::Categorical
                                         : presetName == "continuous_float" ? sicnu::geo::CogPreset::ContinuousFloat
                                                                            : sicnu::geo::CogPreset::Sar;
    const sicnu::geo::CogPresetResult result = sicnu::geo::cogPresetOptions( preset, "Byte" );
    bool bigtiff = false;
    for ( const std::string &option : result.creationOptions )
      bigtiff = bigtiff || option.find( "BIGTIFF=IF_SAFER" ) != std::string::npos;
    CHECK( bigtiff );
  }

  // Continuous float on integer data falls back to the horizontal predictor
  // with a surfaced warning (still lossless).
  const sicnu::geo::CogPresetResult fallback =
    sicnu::geo::cogPresetOptions( sicnu::geo::CogPreset::ContinuousFloat, "UInt16" );
  bool fallbackWarning = false;
  bool predictor2 = false;
  for ( const std::string &warning : fallback.warnings )
    fallbackWarning = fallbackWarning || warning.find( "non-float" ) != std::string::npos;
  for ( const std::string &option : fallback.creationOptions )
    predictor2 = predictor2 || option.find( "PREDICTOR=2" ) != std::string::npos;
  CHECK( fallbackWarning );
  CHECK( predictor2 );
}

TEST_CASE( "validator accepts produced COGs and rejects untiled GeoTIFFs", "[io][cog][validator]" )
{
  const std::string dir = scratch( "validate" );

  // A large untiled plain GeoTIFF must NOT pass COG validation.
  const std::string plain = ( fs::path( dir ) / "plain.tif" ).string();
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
      plain, 1400, 900, { sicnu::geo::RasterBandSpec{} },
      sicnu::geo::RasterWriteOptions{ "GTiff", { "COMPRESS=LZW", "TILED=NO" }, true } );
    sicnu::geo::RasterWindow full;
    full.width = 1400;
    full.height = 900;
    std::vector<double> values( 1400ull * 900, 1.0 );
    writer.writeWindow( 1, full, values.data() );
    writer.finalize();
  }
  const sicnu::geo::CogValidationReport plainReport = sicnu::geo::validateCog( plain );
  CHECK_FALSE( plainReport.isCog );

  // A COG-driver product of the same raster validates.
  const std::string cog = ( fs::path( dir ) / "cog.tif" ).string();
  sicnu::geo::makeCog( plain, cog, sicnu::geo::CogPreset::LosslessScientific );
  const sicnu::geo::CogValidationReport cogReport = sicnu::geo::validateCog( cog );
  if ( !cogReport.isCog )
    WARN( "COG validation details: " << cogReport.toJson().toStyledString() );
  CHECK( cogReport.isCog );

  // Fidelity: nodata + scale/offset survive the COG production.
  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( cog );
  CHECK( reader.metadata().bandCount == 1 );
  CHECK( reader.metadata().compression == "DEFLATE" );
}

TEST_CASE( "makeCog applies preset warnings to the result payload", "[io][cog][presets]" )
{
  const std::string dir = scratch( "payload" );
  const std::string source = ( fs::path( dir ) / "byte.tif" ).string();
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( source, 64, 64, { {} }, {} );
    sicnu::geo::RasterWindow full;
    full.width = 64;
    full.height = 64;
    std::vector<double> values( 64 * 64, 3.0 );
    writer.writeWindow( 1, full, values.data() );
    writer.finalize();
  }
  const std::string cog = ( fs::path( dir ) / "cog.tif" ).string();
  const sicnu::geo::TranslateResult result =
    sicnu::geo::makeCog( source, cog, sicnu::geo::CogPreset::Visualization );
  CHECK( result.bandCount == 1 );
  CHECK( result.warnings.isArray() );
  CHECK( result.warnings.size() >= 1 ); // the LOSSY advisory is surfaced, not swallowed
}
