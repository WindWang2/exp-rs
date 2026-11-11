// test_r28_t01_memory_bench.cpp — R28-T01 WP02/WP11 sampling memory curve.
//
// NOT a CI gate (repo perf discipline, cf. test_r25_a_perf_baseline): each
// case measures and REPORTS the process high-water RSS (VmHWM) around one
// RsTrainingDataExtraction::extract run on a synthetic ROI that covers the
// whole raster, so candidates N = W×H, with a tiny per-class cap and
// increasing band counts. The numbers to compare across revisions:
//   legacy X pre-allocation = N × B × 4 bytes (pre-R28 code path)
//   restructured X          = kept × B × 4 bytes (this revision)
// plus the measured VmHWM which also contains the irreducible O(N)
// candidate bookkeeping (pixelClass/pixelGroup hashes, sample list, ranks).
//
// The only hard assertions are functional: every measured run must succeed,
// fill the cap with valid samples, and keep NoData out of X.
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal_priv.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "rs_training_data_extraction.h"

namespace
{

double peakRssMiB()
{
  std::ifstream status( "/proc/self/status" );
  std::string key;
  long value = 0;
  std::string unit;
  while ( status >> key )
  {
    if ( key == "VmHWM:" )
    {
      status >> value >> unit;
      return static_cast<double>( value ) / 1024.0;
    }
    // skip the rest of the line
    std::string rest;
    std::getline( status, rest );
  }
  return -1.0;
}

/// Whole-frame ROI over a WxH raster; valid = (r+c)%2==0 when nodataHalf,
/// so half the candidates are NoData.
void createRaster( const QString &path, int W, int H, int B, bool nodataHalf )
{
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, B, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  std::vector<float> plane( static_cast<size_t>( W ) * H );
  for ( int b = 0; b < B; ++b )
  {
    for ( int r = 0; r < H; ++r )
      for ( int c = 0; c < W; ++c )
      {
        const size_t i = static_cast<size_t>( r ) * W + c;
        plane[i] = ( nodataHalf && ( r + c ) % 2 == 1 )
                     ? -9999.0f
                     : static_cast<float>( ( r * 31 + c * 17 + b * 7 ) % 900 + 50 );
      }
    GDALRasterBand *band = ds->GetRasterBand( b + 1 );
    band->RasterIO( GF_Write, 0, 0, W, H, plane.data(), W, H, GDT_Float32, 0, 0 );
    band->SetNoDataValue( -9999.0 );
  }
  double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
  ds->SetGeoTransform( gt );
  GDALClose( ds );
}

void runShape( const char *label, int W, int H, int B, bool nodataHalf, int cap )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  GDALAllRegister();
  // Bound GDAL's block cache so its footprint does not mask the extractor's.
  CPLSetConfigOption( "GDAL_CACHEMAX", "64" );

  const QString raster = tmp.path() + "/bench.tif";
  createRaster( raster, W, H, B, nodataHalf );

  QgsPolygonXY polygon;
  polygon.append( { QgsPointXY( 0, 0 ), QgsPointXY( W, 0 ), QgsPointXY( W, H ),
                    QgsPointXY( 0, H ), QgsPointXY( 0, 0 ) } );
  RsTrainingGeometry tg;
  tg.classId = 1;
  tg.geometry = QgsGeometry::fromPolygonXY( polygon );

  RsTrainingDataExtraction::Options options;
  options.maxSamplesPerClass = cap;

  const double rssBefore = peakRssMiB();
  const RsTrainingDataResult res =
    RsTrainingDataExtraction::extract( raster, QVector<int>( B, 1 ), { tg }, options );
  const double rssAfter = peakRssMiB();

  REQUIRE( res.ok );
  const int expectedKept = cap > 0 ? cap : static_cast<int>( W ) * H;
  REQUIRE( res.classCounts.value( 1 ) == expectedKept );
  REQUIRE( res.X.rows == expectedKept );
  REQUIRE( res.X.cols == B );
  for ( int s = 0; s < res.X.rows; ++s )
    for ( int b = 0; b < B; ++b )
      REQUIRE( res.X.at<float>( s, b ) != -9999.0f );

  const double N = static_cast<double>( W ) * H;
  const double legacyXMiB = N * B * 4.0 / ( 1024.0 * 1024.0 );
  // cap == 0 means unlimited: the kept set is every valid candidate.
  const double kept = cap > 0 ? static_cast<double>( cap ) : N;
  const double newXMiB = kept * B * 4.0 / ( 1024.0 * 1024.0 );
  std::printf(
    "[R28T01-MEM] %-10s N=%.2fM B=%d cap=%d nodata=%d | legacy-X=%7.1f MiB -> X=%6.3f MiB | "
    "VmHWM %.1f -> %.1f MiB (delta %.1f)\n",
    label, N / 1e6, B, cap, nodataHalf ? 1 : 0, legacyXMiB, newXMiB,
    rssBefore, rssAfter, rssAfter - rssBefore );
}

} // namespace

TEST_CASE( "R28-T01 sampling memory curve: 1024² × 4 bands, cap 8",
           "[r28][perf][sampling][memory]" )
{
  runShape( "S1-1024x4", 1024, 1024, 4, true, 8 );
}

TEST_CASE( "R28-T01 sampling memory curve: 2048² × 4 bands, cap 8",
           "[r28][perf][sampling][memory]" )
{
  runShape( "S2-2048x4", 2048, 2048, 4, true, 8 );
}

TEST_CASE( "R28-T01 sampling memory curve: 2048² × 8 bands, cap 8 (worst X shape)",
           "[r28][perf][sampling][memory]" )
{
  runShape( "S3-2048x8", 2048, 2048, 8, false, 8 );
}

TEST_CASE( "R28-T01 sampling memory curve: 2048² × 8 bands, no cap",
           "[r28][perf][sampling][memory]" )
{
  // Uncapped, no NoData: kept == N, the honest worst case (X must hold
  // every valid sample; the win vs legacy is the removed pre-cap N×B
  // allocation + kept×B compaction copy).
  runShape( "S4-2048x8-nc", 2048, 2048, 8, false, 0 );
}

TEST_CASE( "R29-T01 candidate-index overhead: N scales, K stays capped at 8",
           "[r29][perf][sampling][memory]" )
{
  // WP04 (A2 evidence): with the kept set pinned at K=8 per class, the
  // feature matrix is constant-size; the VmHWM delta therefore isolates the
  // CANDIDATE-INDEX footprint (pixel→owner hash + candidate vector + match
  // counts) as N grows. R29 merges the two same-key pixel hashes into one
  // and stores one candidate struct (class/pixel/group/rank) instead of
  // three parallel arrays plus an index-sort copy — compare this curve
  // against the same case on the base revision.
  runShape( "R29-1024x4", 1024, 1024, 4, true, 8 );
  runShape( "R29-2048x4", 2048, 2048, 4, true, 8 );
  runShape( "R29-4096x4", 4096, 4096, 4, true, 8 );
}
