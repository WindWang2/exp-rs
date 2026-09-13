// F-OPS-3 — rs:qa_mask fail-open: NaN/NoData QA samples convert to word 0
// (Landsat "clear", SCL NO_DATA-class) and the mask product reports them
// clear. NOT wired into CMake (review-only draft).
//
// Expected failure on current master: the mask value for the NaN-QA pixel is
// 0 (clear); the contract for a quality gate demands 1 (masked / unknown).
#include <catch2/catch.hpp>
#include <gdal_priv.h>
#include <QDir>
#include <limits>

namespace {

/// Writes a 2x2 Float32 raster whose QA band carries one NaN sample.
QString writeQaInput( const QDir &dir )
{
  const QString path = dir.filePath( QStringLiteral( "qa_nan.tif" ) );
  GDALDriverH driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 2, 2, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  float samples[4] = { 2110.0f, std::numeric_limits<float>::quiet_NaN(),
                       2110.0f, 2110.0f }; // 2110 = S2 SCL "cloud high probability" word
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 2, 2, samples, 2, 2, GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  return path;
}

} // namespace

TEST_CASE( "F-OPS-3: unreadable QA samples must not classify as clear", "[review][F-OPS-3][test-draft]" )
{
  // Run rs:qa_mask through the operator registry with source=sentinel2_scl,
  // mask=all on writeQaInput(); read the output Byte band. The NaN pixel
  // (0,1) must read 1 (masked). Current master: convertSample returns 0 and
  // sclClasses never selects class 0 → mask value 0 → assertion fails.
  REQUIRE( false ); // placeholder per track rules: drafts are not compiled
}
