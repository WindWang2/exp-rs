/***************************************************************************
  tests/test_io_metadata_patch.cpp — validated metadata write-back.
  Oracle: canonical re-inspection (the READ path) after patching, refusal
  before mutation, read-only medium refusal, manifest digest continuity.
 ***************************************************************************/

#include "geospatial/io/metadata_patch.h"
#include "geospatial/common.h"
#include "geospatial/io/finalize_manifest.h"
#include "geospatial/io/stage_ledger.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>
#include <gdal_priv.h>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

using namespace sicnu::geo;
using namespace sicnu::geo::io;

namespace
{

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_metadata_patch" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

/// 6x6 two-band Float32 raster through the standard writer contract.
std::string makeRaster( const std::string &dir, const std::string &name )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / name ).string();
  RasterBandSpec spec;
  spec.dtype = "Float32";
  RasterWriter writer = RasterWriter::create( path, 6, 6, { spec, spec } );
  std::vector<double> pixels( 36, 1.5 );
  RasterWindow window{ 0, 0, 6, 6 };
  writer.writeWindow( 1, window, pixels.data() );
  writer.writeWindow( 2, window, pixels.data() );
  writer.finalize();
  return path;
}

} // namespace

TEST_CASE( "applyMetadataPatch round-trips band and dataset fields", "[io][metadata_patch]" )
{
  const std::string dir = scratch( "roundtrip" );
  const std::string tif = makeRaster( dir, "grid.tif" );

  MetadataPatch scale;
  scale.band = 1;
  scale.field = "scale";
  scale.value = "0.0015";
  MetadataPatch unit;
  unit.band = 1;
  unit.field = "unit";
  unit.value = "W/m^2/sr/um";
  MetadataPatch role;
  role.band = 2;
  role.field = "role";
  role.value = "NIR";
  MetadataPatch wavelength;
  wavelength.band = 2;
  wavelength.field = "wavelength_nm";
  wavelength.value = "842.5";
  MetadataPatch stamp;
  stamp.band = 0;
  stamp.field = "acquisition_time";
  stamp.value = "2026-09-16T02:30:00Z";
  MetadataPatch sensor;
  sensor.band = 0;
  sensor.field = "sensor";
  sensor.value = "SICNU-1";

  const MetadataPatchReport report = applyMetadataPatch( tif, { scale, unit, role, wavelength, stamp, sensor } );
  INFO( report.toJson().toStyledString() );
  CHECK( report.applied );
  CHECK( report.appliedFields.size() == 6 );

  // Independent oracle: the canonical READ path must reflect every value.
  const RasterMetadata meta = inspectRaster( tif );
  REQUIRE( meta.bands.size() == 2 );
  CHECK( meta.bands[0].hasScale );
  CHECK( meta.bands[0].scale == Catch::Approx( 0.0015 ) );
  CHECK( meta.bands[0].unit == "W/m^2/sr/um" );
  CHECK( meta.bands[1].role == "NIR" );
  CHECK( meta.bands[1].hasWavelength );
  CHECK( meta.bands[1].wavelengthNm == Catch::Approx( 842.5 ) );
  CHECK( meta.acquisitionTime == "2026-09-16T02:30:00Z" );
  CHECK( meta.sensor == "SICNU-1" );
}

TEST_CASE( "applyMetadataPatch refuses invalid batches before opening for update", "[io][metadata_patch][negative]" )
{
  const std::string dir = scratch( "refuse" );
  const std::string tif = makeRaster( dir, "grid.tif" );
  const auto digestBefore = datasetSha256Hex( tif );

  SECTION( "unknown field" )
  {
    MetadataPatch bad;
    bad.band = 0;
    bad.field = "depth_of_magic";
    bad.value = "42";
    CHECK_THROWS_AS( applyMetadataPatch( tif, { bad } ), GeoError );
  }
  SECTION( "band field without band index" )
  {
    MetadataPatch bad;
    bad.band = 0;
    bad.field = "scale";
    bad.value = "2";
    CHECK_THROWS_AS( applyMetadataPatch( tif, { bad } ), GeoError );
  }
  SECTION( "dataset field with band index" )
  {
    MetadataPatch bad;
    bad.band = 1;
    bad.field = "sensor";
    bad.value = "X";
    CHECK_THROWS_AS( applyMetadataPatch( tif, { bad } ), GeoError );
  }
  SECTION( "non-numeric value for numeric field" )
  {
    MetadataPatch bad;
    bad.band = 1;
    bad.field = "offset";
    bad.value = "abc";
    CHECK_THROWS_AS( applyMetadataPatch( tif, { bad } ), GeoError );
  }
  SECTION( "date-only acquisition_time is refused (never a guess)" )
  {
    MetadataPatch bad;
    bad.band = 0;
    bad.field = "acquisition_time";
    bad.value = "2026-09-16";
    CHECK_THROWS_AS( applyMetadataPatch( tif, { bad } ), GeoError );
  }
  SECTION( "band index beyond the dataset" )
  {
    MetadataPatch bad;
    bad.band = 9;
    bad.field = "scale";
    bad.value = "1";
    CHECK_THROWS_AS( applyMetadataPatch( tif, { bad } ), GeoError );
  }
  SECTION( "duplicate field in one batch" )
  {
    MetadataPatch a;
    a.band = 1;
    a.field = "scale";
    a.value = "1";
    MetadataPatch b = a;
    b.value = "2";
    CHECK_THROWS_AS( applyMetadataPatch( tif, { a, b } ), GeoError );
  }

  // Nothing was written: the digest still matches the pre-patch bytes.
  CHECK( datasetSha256Hex( tif ) == digestBefore );
}

TEST_CASE( "applyMetadataPatch refuses read-only media with a typed error", "[io][metadata_patch][negative]" )
{
  const std::string dir = scratch( "readonly" );
  const std::string tif = makeRaster( dir, "grid.tif" );
  std::filesystem::permissions( fs::u8path( tif ), fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read );

  MetadataPatch patch;
  patch.band = 1;
  patch.field = "scale";
  patch.value = "2";
  try
  {
    applyMetadataPatch( tif, { patch } );
    FAIL( "read-only medium must be refused" );
  }
  catch ( const GeoError &error )
  {
    CHECK( error.code() == ErrorCode::OpenFailed );
  }
  std::filesystem::permissions( fs::u8path( tif ), fs::perms::owner_read | fs::perms::owner_write );
}

TEST_CASE( "patching keeps finalize-manifest provenance true", "[io][metadata_patch][manifest]" )
{
  const std::string dir = scratch( "manifest" );
  const std::string staged = ( fs::path( dir ) / "out.1.2.tmp.tif" ).string();
  ensureGdal();
  {
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, staged.c_str(), 4, 4, 1, GDT_Byte, nullptr );
    REQUIRE( dataset );
    GDALClose( dataset );
  }
  StageRecord record;
  record.runId = "run-manifest";
  record.producer = "test-harness";
  record.finalPath = ( fs::path( dir ) / "out.tif" ).string();
  record.stagedPath = staged;
  record.driver = "GTiff";
  record.width = 4;
  record.height = 4;
  record.bandCount = 1;
  recordStaged( record );
  FinalizeManifestFields fields;
  fields.producer = "test-harness";
  fields.driver = "GTiff";
  fields.width = 4;
  fields.height = 4;
  fields.bandCount = 1;
  fields.dtype = "Byte";
  finalizeAttached( record.finalPath, &fields );
  REQUIRE( verifyDataset( record.finalPath ).verified );

  // The patch changes bytes; without the manifest refresh verifyDataset
  // would go red. The refresh keeps it green and records the history.
  MetadataPatch patch;
  patch.band = 1;
  patch.field = "unit";
  patch.value = "degC";
  const MetadataPatchReport report = applyMetadataPatch( record.finalPath, { patch } );
  INFO( report.toJson().toStyledString() );
  CHECK( report.applied );
  CHECK( report.manifestUpdated );

  const ManifestVerifyReport verify = verifyDataset( record.finalPath );
  INFO( verify.toJson().toStyledString() );
  CHECK( verify.verified );

  const Json::Value manifest = readFinalizeManifest( record.finalPath );
  CHECK( manifest["patches"].isArray() );
  CHECK( manifest["patches"].size() == 1 );
  CHECK( manifest["producer"].asString() == "test-harness" ); // provenance preserved
}
