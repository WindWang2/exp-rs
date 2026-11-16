// tests/test_r32_t02_feather_rows.cpp — R32-T02 WP01/WP02 (#2086).
//
// FeatherAccumulator commit order: the stitch loops used to iterate heads
// OUTER and tiles INNER while flushing the sliding window per (head, tile).
// With batchSize >= 2 a row-major batch crosses tile rows: head 0's
// later-row tiles finalized rows that heads 1..H-1 still had to blend —
// add() tripped the window CV_Assert (crash) or blended already-finalized
// rows (NaN bands), and addInvalidMask silently dropped marks on finalized
// rows (neighbor halos then inpainted NoData ground). The fix flushes ONCE
// PER TILE and immediately blends that tile's full head set (tile-outer /
// head-inner), recording the core validity mark once per tile.
//
// Oracles are independent per-pixel cosine-weight recomputations over the
// tile grid — no engine internals are reused — and model THREE engine
// behaviors exactly (verified against the production code, R1 review):
//   * withRect in the fixture builder is HALF-OPEN ([x0,x1) x [y0,y1));
//   * the engine's nodata_policy "zero" sweep turns every non-finite fed
//     sample (declared sentinel → NaN, out-of-raster NaN padding) into 0
//     BEFORE the forward — so a MIXED tile whose window CENTER sits on the
//     sentinel still contributes its zero-based constants (it does NOT
//     drop out);
//   * a tile whose CORE is entirely the sentinel is deferred PER TILE at
//     enumeration (R15-C C07) and never forwards: it contributes nothing
//     and its core is NoData by mark. The R32-T02 engine fix flushes any
//     pending batch at each deferral, so a batch never straddles the
//     deferred tile rows.
// Both engine paths are covered (run + runMultiInput).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;

/// Cosine feather weight at distance dx from the core (halo h) — an
/// independent copy of the documented ramp (0 inside the core,
/// 0.5·(1+cos(π·dx/h)) in the halo, 0 beyond).
double featherWeight( int dx, int halo )
{
  if ( dx <= 0 )
    return 1.0;
  if ( dx >= halo )
    return 0.0;
  return 0.5 * ( 1.0 + std::cos( dx * 3.14159265358979323846 / halo ) );
}

struct SimTile
{
  int x, y, w, h;
};

std::vector<SimTile> tileGrid( int rasterW, int rasterH, int tileSize )
{
  std::vector<SimTile> tiles;
  for ( int y = 0; y < rasterH; y += tileSize )
    for ( int x = 0; x < rasterW; x += tileSize )
      tiles.push_back( { x, y, std::min( tileSize, rasterW - x ),
                         std::min( tileSize, rasterH - y ) } );
  return tiles;
}

/// Tiles the engine never forwards. The engine defers PER TILE at
/// enumeration (R15-C C07: a tile whose core is entirely the sentinel — or
/// below the coverage gate, not used here — is pushed to deferredNoData
/// BEFORE it can enter a batch; the R32-T02 fix additionally flushes any
/// pending batch at that point, so a batch never straddles deferred rows).
std::set<std::size_t> deferredTileIndexes( const std::vector<SimTile> &tiles, int,
                                           const std::function<bool( const SimTile & )> &allNoData )
{
  std::set<std::size_t> deferred;
  for ( std::size_t i = 0; i < tiles.size(); ++i )
    if ( allNoData( tiles[i] ) )
      deferred.insert( i );
  return deferred;
}

/// Two heads × two channels. head "alpha" predicts {v, v+50}; head "beta"
/// predicts {2v+1, 2v+51}, where v is the fed window's CENTER value of
/// input channel 0 (per batch item). Bands therefore differ per head, per
/// channel and per tile — a wrong head/band offset or a lost head cannot
/// pass an independent oracle.
class TwoHeadCenterRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r32t02-fake"; }
    std::string backendName() const override { return "r32t02-twohead"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r32t02-twohead"; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      return headOutput( blob, 1.0f, 0.0f );
    }

    cv::Mat infer( const cv::Mat &blob, const std::string &outputName ) override
    {
      if ( outputName == "beta" )
        return headOutput( blob, 2.0f, 1.0f );
      return headOutput( blob, 1.0f, 0.0f );
    }

  private:
    // channels: { v + aOff, v + 50 + aOff }
    static cv::Mat headOutput( const cv::Mat &blob, float scale, float offset )
    {
      CV_Assert( blob.dims == 4 );
      const int B = blob.size[0];
      const int H = blob.size[2];
      const int W = blob.size[3];
      const cv::Mat contiguous = blob.isContinuous() ? blob : blob.clone();
      int dims[4] = { B, 2, H, W };
      cv::Mat out( 4, dims, CV_32F );
      for ( int b = 0; b < B; ++b )
      {
        const float *src = contiguous.ptr<float>( b, 0, 0 );
        const float v = src[( static_cast<std::size_t>( H / 2 ) ) * W + W / 2];
        const float v0 = v * scale + offset;
        const float v1 = v * scale + 50.0f + offset;
        float *d0 = out.ptr<float>( b, 0, 0 );
        float *d1 = out.ptr<float>( b, 1, 0 );
        for ( std::size_t i = 0; i < static_cast<std::size_t>( H ) * W; ++i )
        {
          d0[i] = v0;
          d1[i] = v1;
        }
      }
      return out;
    }
};

/// Multi-input fake: predicts {v, v+7} from the LAST feed's window-center
/// value (channel 0), one plane per class slot.
class TwoClassCenterMultiRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "r32t02-fake"; }
    std::string backendName() const override { return "r32t02-multicenter"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r32t02-multi"; }

    bool supportsMultiInput() const override { return true; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> & ) override
    {
      const TensorBlob &last = inputs.back().second;
      const std::int64_t B = last.shape[0];
      const std::int64_t C = last.shape[1];
      const std::int64_t H = last.shape[2];
      const std::int64_t W = last.shape[3];
      TensorBlob out;
      out.shape = { B, 2, H, W };
      out.dtype = TensorDType::Float32;
      out.bytes.assign( static_cast<std::size_t>( B * 2 * H * W ) * sizeof( float ), 0 );
      float *outData = reinterpret_cast<float *>( out.bytes.data() );
      const float *src = last.dataFloat32();
      for ( std::int64_t b = 0; b < B; ++b )
      {
        const float v = src[( ( b * C ) * H + H / 2 ) * W + W / 2];
        for ( std::int64_t p = 0; p < 2; ++p )
        {
          const float value = p == 0 ? v : v + 7.0f;
          float *plane = outData + static_cast<std::size_t>( ( b * 2 + p ) * H ) * W;
          for ( std::int64_t i = 0; i < H * W; ++i )
            plane[i] = value;
        }
      }
      return { NamedTensor{ std::string(), std::move( out ) } };
    }

    cv::Mat infer( const cv::Mat & ) override
    {
      throw std::logic_error( "multi fake driven through the single-input path" );
    }
};

ModelInfo twoHeadModel( int tile, int halo, int batch )
{
  ModelInfo model;
  model.name = "r32t02-twohead";
  model.framework = "r32t02-fake";
  model.task = "segmentation";
  model.tiling.tileSize = tile;
  model.tiling.halo = halo;
  model.tiling.batchSize = batch;
  model.output.classes = { "a", "b" };
  model.output.tensorNames = { "alpha", "beta" };
  return model;
}

ModelInfo twoInputModel( int tile, int halo, int batch )
{
  ModelInfo model;
  model.name = "r32t02-multi";
  model.framework = "r32t02-fake";
  model.task = "change_detection";
  model.tiling.tileSize = tile;
  model.tiling.halo = halo;
  model.tiling.batchSize = batch;
  sicnu::operators::ModelInputContract before;
  before.name = "before";
  sicnu::operators::ModelInputContract after;
  after.name = "after";
  model.inputs = { before, after };
  model.input = before;
  model.output.classes = { "stable", "change" };
  return model;
}

std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
  GDALDataset *ds =
    static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  width = ds->GetRasterXSize();
  height = ds->GetRasterYSize();
  std::vector<float> data( static_cast<std::size_t>( width ) * height );
  REQUIRE( ds->GetRasterBand( band )
             ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height,
                         GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  return data;
}

/// Independent per-pixel oracle for the blended probability stack.
///   get(x, y): the RAW raster value (NaN for NoData sentinel / outside);
///   isNoData(x, y): the core-validity mark (sentinel pixels);
///   bandValue(v, band): the fake's plane constant for a POST-ZEROING
///   center value v.
/// The engine's "zero" nodata policy feeds non-finite samples as 0 — a
/// tile whose center sits on the sentinel contributes 0-based constants;
/// a DEFERRED tile (whole batch all-NoData) contributes nothing.
float blendedOracle( const std::vector<SimTile> &tiles,
                     const std::set<std::size_t> &deferred, int halo,
                     const std::function<float( int, int )> &get,
                     const std::function<bool( int, int )> &isNoData,
                     const std::function<float( float, int )> &bandValue, int band,
                     int x, int y )
{
  if ( isNoData( x, y ) )
    return std::numeric_limits<float>::quiet_NaN();
  double sum = 0.0;
  double weight = 0.0;
  for ( std::size_t ti = 0; ti < tiles.size(); ++ti )
  {
    if ( deferred.count( ti ) )
      continue;
    const SimTile &t = tiles[ti];
    // The fed window's center pixel (buffer is t.w + 2·halo wide; the read
    // clips at the raster and NaN-pads outside; the zero sweep then maps
    // every non-finite sample to 0 BEFORE the forward).
    const int cx = t.x - halo + ( t.w + 2 * halo ) / 2;
    const int cy = t.y - halo + ( t.h + 2 * halo ) / 2;
    const float raw = get( cx, cy );
    const float v = std::isfinite( raw ) ? raw : 0.0f;
    const int dx = std::max( { t.x - x, x - ( t.x + t.w - 1 ), 0 } );
    const int dy = std::max( { t.y - y, y - ( t.y + t.h - 1 ), 0 } );
    const double w = featherWeight( dx, halo ) * featherWeight( dy, halo );
    if ( w <= 0.0 )
      continue;
    sum += bandValue( v, band ) * w;
    weight += w;
  }
  return weight > 0.0 ? static_cast<float>( sum / weight )
                      : std::numeric_limits<float>::quiet_NaN();
}

const std::function<float( float, int )> kTwoHeadBand = []( float v, int band ) {
  switch ( band )
  {
    case 0: return v;            // alpha ch0
    case 1: return v + 50.0f;    // alpha ch1
    case 2: return 2.0f * v + 1.0f; // beta ch0
    default: return 2.0f * v + 51.0f; // beta ch1
  }
};

const std::function<float( float, int )> kTwoClassBand = []( float v, int band ) {
  return band == 0 ? v : v + 7.0f;
};

/// Row-ramp raster: value = 1 + row (every tile's window center distinct).
std::vector<float> rowRamp( int w, int h )
{
  std::vector<float> data( static_cast<std::size_t>( w ) * h );
  for ( int y = 0; y < h; ++y )
    for ( int x = 0; x < w; ++x )
      data[static_cast<std::size_t>( y ) * w + x] = static_cast<float>( 1 + y );
  return data;
}

/// Write a row-ramp raster through HALF-OPEN rects (withRect covers
/// [x0,x1) x [y0,y1) — a full row is x1 = rasterW).
QString writeRowRampRaster( const QString &path, const std::vector<float> &ramp, int rasterW,
                            int rasterH )
{
  sicnu::testing::RsSyntheticRasterBuilder builder( rasterW, rasterH, 1, GDT_Float32 );
  for ( int y = 0; y < rasterH; ++y )
    builder.withRect( 1, 0, y, rasterW, y + 1, ramp[static_cast<std::size_t>( y ) * rasterW] );
  return builder.writeToDisk( path );
}

void checkFullGridAgainstOracle( const QString &path, int rasterW, int rasterH,
                                 const std::vector<float> &raster, bool rasterUsesSentinel,
                                 float sentinel,
                                 const std::function<float( float, int )> &bandValue,
                                 int bands, int tile, int halo, int batch )
{
  const std::vector<SimTile> tiles = tileGrid( rasterW, rasterH, tile );
  const auto get = [ & ]( int x, int y ) {
    if ( x < 0 || y < 0 || x >= rasterW || y >= rasterH )
      return std::numeric_limits<float>::quiet_NaN();
    const float v = raster[static_cast<std::size_t>( y ) * rasterW + x];
    if ( rasterUsesSentinel && v == sentinel )
      return std::numeric_limits<float>::quiet_NaN();
    return v;
  };
  const auto isNoData = [ & ]( int x, int y ) {
    return rasterUsesSentinel
           && raster[static_cast<std::size_t>( y ) * rasterW + x] == sentinel;
  };
  const auto tileAllNoData = [ & ]( const SimTile &t ) {
    for ( int y = t.y; y < t.y + t.h; ++y )
      for ( int x = t.x; x < t.x + t.w; ++x )
        if ( raster[static_cast<std::size_t>( y ) * rasterW + x] != sentinel )
          return false;
    return true;
  };
  const std::set<std::size_t> deferred =
    rasterUsesSentinel ? deferredTileIndexes( tiles, batch, tileAllNoData )
                       : std::set<std::size_t>{};
  int w = 0;
  int h = 0;
  for ( int band = 1; band <= bands; ++band )
  {
    const std::vector<float> img = readBand( path, band, w, h );
    REQUIRE( w == rasterW );
    REQUIRE( h == rasterH );
    for ( int y = 0; y < rasterH; ++y )
    {
      for ( int x = 0; x < rasterW; ++x )
      {
        const float expected =
          blendedOracle( tiles, deferred, halo, get, isNoData, bandValue, band - 1, x, y );
        const float actual = img[static_cast<std::size_t>( y ) * rasterW + x];
        if ( std::isnan( expected ) )
        {
          if ( !std::isnan( actual ) )
          {
            CAPTURE( band, x, y, actual );
            FAIL( "expected NoData at (band,x,y)" );
          }
        }
        else
        {
          if ( std::isnan( actual ) )
          {
            CAPTURE( band, x, y, expected );
            FAIL( "unexpected NoData at (band,x,y)" );
          }
          else if ( std::fabs( actual - expected ) > 1e-3 )
          {
            CAPTURE( band, x, y, expected, actual );
            FAIL( "blended band deviates from the cosine oracle at (band,x,y)" );
          }
        }
      }
    }
  }
}

} // namespace

TEST_CASE( "R32-T02 WP01: two heads, batch crossing TWO tile rows blends every band "
           "to the cosine oracle",
           "[r32t02][wp01][feather-rows]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // 8x24 raster, tileSize 8, halo 2 → a single tile COLUMN and THREE tile
  // rows. batchSize 2 fills row-major ACROSS the row boundary ([t0, t1]
  // spans rows 0-1) — the exact #2086 trigger for the head-outer flush.
  const int rasterW = 8;
  const int rasterH = 24;
  const std::vector<float> ramp = rowRamp( rasterW, rasterH );
  const QString raster =
    writeRowRampRaster( dir.filePath( QStringLiteral( "ramp.tif" ) ), ramp, rasterW, rasterH );
  REQUIRE_FALSE( raster.isEmpty() );

  TileInferenceEngine engine( twoHeadModel( 8, 2, 2 ),
                              std::make_shared<TwoHeadCenterRuntime>() );
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  RSOperatorContext context;
  const TileInferenceStats stats =
    engine.run( raster.toStdString(), {}, out.toStdString(), context, feather );

  CHECK( stats.outBands == 4 );
  CHECK( stats.headChannels == std::vector<int>{ 2, 2 } );
  CHECK( stats.tilesProcessed == 3 );
  CHECK( stats.tilesSkippedNoData == 0 );
  checkFullGridAgainstOracle( out, rasterW, rasterH, ramp, false, 0.0f, kTwoHeadBand, 4, 8,
                              2, 2 );
}

TEST_CASE( "R32-T02 WP01: batch spanning THREE tile rows and the whole grid in one "
           "batch stays exact",
           "[r32t02][wp01][feather-rows]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // 8x40, tileSize 8 → 5 tile rows; batchSize 3 puts rows 0-2 into ONE
  // batch (multiple row crossings per batch), the remainder into the next.
  const int rasterW = 8;
  const int rasterH = 40;
  const std::vector<float> ramp = rowRamp( rasterW, rasterH );
  const QString raster =
    writeRowRampRaster( dir.filePath( QStringLiteral( "ramp.tif" ) ), ramp, rasterW, rasterH );
  REQUIRE_FALSE( raster.isEmpty() );

  TileInferenceEngine engine( twoHeadModel( 8, 1, 3 ),
                              std::make_shared<TwoHeadCenterRuntime>() );
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  RSOperatorContext context;
  const TileInferenceStats stats =
    engine.run( raster.toStdString(), {}, out.toStdString(), context, feather );
  CHECK( stats.tilesProcessed == 5 );
  checkFullGridAgainstOracle( out, rasterW, rasterH, ramp, false, 0.0f, kTwoHeadBand, 4, 8,
                              1, 3 );
}

TEST_CASE( "R32-T02 WP01: non-divisible edge tiles keep every head's band layout",
           "[r32t02][wp01][feather-rows]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // 10x18 at tileSize 8 → edge tiles of width 2 (right) and height 2
  // (bottom); halo 3. Edge fed windows are NaN-padded outside the raster
  // and the zero sweep feeds those samples as 0 — the oracle models it.
  const int rasterW = 10;
  const int rasterH = 18;
  const std::vector<float> ramp = rowRamp( rasterW, rasterH );
  const QString raster =
    writeRowRampRaster( dir.filePath( QStringLiteral( "ramp.tif" ) ), ramp, rasterW, rasterH );
  REQUIRE_FALSE( raster.isEmpty() );

  TileInferenceEngine engine( twoHeadModel( 8, 3, 2 ),
                              std::make_shared<TwoHeadCenterRuntime>() );
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  RSOperatorContext context;
  const TileInferenceStats stats =
    engine.run( raster.toStdString(), {}, out.toStdString(), context, feather );
  CHECK( stats.outBands == 4 );
  // 10x18 → gridX 2, gridY 3 = 6 tiles.
  CHECK( stats.tilesProcessed + stats.tilesSkippedNoData == 6 );
  checkFullGridAgainstOracle( out, rasterW, rasterH, ramp, false, 0.0f, kTwoHeadBand, 4, 8,
                              3, 2 );
}

TEST_CASE( "R32-T02 WP02: NoData tile rows longer than the window span stay NoData; "
           "valid rows blend exactly",
           "[r32t02][wp02][feather-nodata]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // 8x40, tileSize 8, halo 2 (window span 12). Rows [8,32) are the NoData
  // sentinel — three consecutive all-nodata tiles deferred PER TILE (R15-C
  // C07) between valid ground, a 24-row run vs the 12-row window (the
  // R18-B tall-tail shape). The R32-T02 engine fix flushes t0's pending
  // batch before t1 defers, so no batch ever straddles the deferred rows
  // (the pre-fix shape crashed: t4 completed a straddling batch [t0, t4]
  // and the stitch's one-window-per-call flush could not catch up). Valid
  // tiles' halos reach INTO the NoData rows; the per-tile core marks must
  // keep them NoData on every head's bands.
  const int rasterW = 8;
  const int rasterH = 40;
  const float sentinel = 0.0f;
  std::vector<float> raster( static_cast<std::size_t>( rasterW ) * rasterH, sentinel );
  for ( int y = 0; y < 8; ++y )
    for ( int x = 0; x < rasterW; ++x )
      raster[static_cast<std::size_t>( y ) * rasterW + x] = static_cast<float>( 1 + y );
  for ( int y = 32; y < rasterH; ++y )
    for ( int x = 0; x < rasterW; ++x )
      raster[static_cast<std::size_t>( y ) * rasterW + x] = static_cast<float>( 1 + y );
  sicnu::testing::RsSyntheticRasterBuilder builder( rasterW, rasterH, 1, GDT_Float32 );
  builder.withNoData( sentinel );
  for ( int y = 0; y < rasterH; ++y )
    builder.withRect( 1, 0, y, rasterW, y + 1, raster[static_cast<std::size_t>( y ) * rasterW] );
  const QString path = builder.writeToDisk( dir.filePath( QStringLiteral( "holed.tif" ) ) );
  REQUIRE_FALSE( path.isEmpty() );

  TileInferenceEngine engine( twoHeadModel( 8, 2, 2 ),
                              std::make_shared<TwoHeadCenterRuntime>() );
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  RSOperatorContext context;
  const TileInferenceStats stats =
    engine.run( path.toStdString(), {}, out.toStdString(), context, feather );

  // t1/t2/t3 defer per tile (24 NoData rows); t0's batch is flushed before
  // t1 defers and t4 forms its own batch: 2 processed, 3 skipped, 5 planned.
  CHECK( stats.tilesProcessed == 2 );
  CHECK( stats.tilesSkippedNoData == 3 );
  CHECK( stats.tilesProcessed + stats.tilesSkippedNoData == 5 );
  checkFullGridAgainstOracle( out, rasterW, rasterH, raster, true, sentinel, kTwoHeadBand,
                              4, 8, 2, 2 );
}

TEST_CASE( "R32-T02 WP02: multiple NoData blocks inside one tile row are marked, "
           "not inpainted",
           "[r32t02][wp02][feather-nodata]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // 32x8 single tile row, column-ramp. Columns [8,12) and [20,24) are the
  // sentinel — two NoData blocks inside ONE tile row (no whole tile
  // defers): the mixed tiles forward with partial masks. A mixed tile
  // whose window CENTER sits on the sentinel feeds a ZEROED sample (the
  // "zero" nodata policy) and still contributes its zero-based constants —
  // the oracle models exactly that.
  const int rasterW = 32;
  const int rasterH = 8;
  const float sentinel = 0.0f;
  std::vector<float> raster( static_cast<std::size_t>( rasterW ) * rasterH, sentinel );
  for ( int y = 0; y < rasterH; ++y )
    for ( int x = 0; x < rasterW; ++x )
    {
      if ( ( x >= 8 && x < 12 ) || ( x >= 20 && x < 24 ) )
        continue;
      raster[static_cast<std::size_t>( y ) * rasterW + x] = static_cast<float>( 1 + x );
    }
  sicnu::testing::RsSyntheticRasterBuilder builder( rasterW, rasterH, 1, GDT_Float32 );
  builder.withNoData( sentinel );
  for ( int x = 0; x < rasterW; ++x )
    builder.withRect( 1, x, 0, x + 1, rasterH,
                      raster[static_cast<std::size_t>( x )] );
  const QString path = builder.writeToDisk( dir.filePath( QStringLiteral( "blocks.tif" ) ) );
  REQUIRE_FALSE( path.isEmpty() );

  TileInferenceEngine engine( twoHeadModel( 8, 2, 4 ),
                              std::make_shared<TwoHeadCenterRuntime>() );
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  RSOperatorContext context;
  const TileInferenceStats stats =
    engine.run( path.toStdString(), {}, out.toStdString(), context, feather );

  CHECK( stats.tilesProcessed == 4 );
  CHECK( stats.tilesSkippedNoData == 0 );
  checkFullGridAgainstOracle( out, rasterW, rasterH, raster, true, sentinel, kTwoHeadBand,
                              4, 8, 2, 4 );
}

TEST_CASE( "R32-T02 WP02: an all-NoData raster publishes NoData everywhere with "
           "honest counters",
           "[r32t02][wp02][feather-nodata]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const int rasterW = 8;
  const int rasterH = 16;
  sicnu::testing::RsSyntheticRasterBuilder builder( rasterW, rasterH, 1, GDT_Float32 );
  builder.withNoData( 0.0f );
  const QString path = builder.writeToDisk( dir.filePath( QStringLiteral( "empty.tif" ) ) );
  REQUIRE_FALSE( path.isEmpty() );

  TileInferenceEngine engine( twoHeadModel( 8, 2, 2 ),
                              std::make_shared<TwoHeadCenterRuntime>() );
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  RSOperatorContext context;
  const TileInferenceStats stats =
    engine.run( path.toStdString(), {}, out.toStdString(), context, feather );

  CHECK( stats.tilesSkippedNoData == 2 );
  CHECK( stats.tilesProcessed <= 1 );
  int w = 0;
  int h = 0;
  for ( int band = 1; band <= stats.outBands; ++band )
  {
    const std::vector<float> img = readBand( out, band, w, h );
    REQUIRE( w == rasterW );
    REQUIRE( h == rasterH );
    for ( float v : img )
      CHECK( std::isnan( v ) );
  }
}

TEST_CASE( "R32-T02 WP02: a cancelled run throws Cancelled and a fresh rerun completes",
           "[r32t02][wp02][feather-nodata]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const int rasterW = 8;
  const int rasterH = 24;
  const std::vector<float> ramp = rowRamp( rasterW, rasterH );
  const QString raster =
    writeRowRampRaster( dir.filePath( QStringLiteral( "ramp.tif" ) ), ramp, rasterW, rasterH );
  REQUIRE_FALSE( raster.isEmpty() );

  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );

  // Cancelled BEFORE the first tile: the engine must throw Cancelled, not
  // publish a partial product.
  {
    std::atomic<bool> cancelled{ true };
    RSOperatorContext cancelledContext;
    cancelledContext.setCancelFlag( &cancelled );
    TileInferenceEngine engine( twoHeadModel( 8, 2, 2 ),
                                std::make_shared<TwoHeadCenterRuntime>() );
    bool threw = false;
    try
    {
      engine.run( raster.toStdString(), {}, out.toStdString(), cancelledContext, feather );
    }
    catch ( const RSOperatorError &e )
    {
      threw = true;
      CHECK( e.code() == sicnu::operators::ErrorCode::Cancelled );
    }
    REQUIRE( threw );
    CHECK( !QFile::exists( out ) );
  }

  // A fresh run over the same fixture completes and matches the oracle.
  TileInferenceEngine engine( twoHeadModel( 8, 2, 2 ),
                              std::make_shared<TwoHeadCenterRuntime>() );
  RSOperatorContext context;
  const TileInferenceStats stats =
    engine.run( raster.toStdString(), {}, out.toStdString(), context, feather );
  CHECK( stats.tilesProcessed == 3 );
  checkFullGridAgainstOracle( out, rasterW, rasterH, ramp, false, 0.0f, kTwoHeadBand, 4, 8,
                              2, 2 );
}

TEST_CASE( "R32-T02 WP01: multi-input batch crossing tile rows blends to the cosine "
           "oracle (runMultiInput)",
           "[r32t02][wp01][multi-feather-rows]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // Same row-crossing shape through the MULTI-INPUT engine: 8x24, tileSize
  // 8, halo 2, batchSize 2 → [t0, t1] spans two tile rows.
  const int rasterW = 8;
  const int rasterH = 24;
  const std::vector<float> ramp = rowRamp( rasterW, rasterH );
  const QString after =
    writeRowRampRaster( dir.filePath( QStringLiteral( "after.tif" ) ), ramp, rasterW, rasterH );
  REQUIRE_FALSE( after.isEmpty() );
  sicnu::testing::RsSyntheticRasterBuilder flat( rasterW, rasterH, 1, GDT_Float32 );
  flat.withConstantValue( 1, 5.0f );
  const QString before = flat.writeToDisk( dir.filePath( QStringLiteral( "before.tif" ) ) );
  REQUIRE_FALSE( before.isEmpty() );

  TileInferenceEngine engine( twoInputModel( 8, 2, 2 ),
                              std::make_shared<TwoClassCenterMultiRuntime>() );
  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString out = dir.filePath( QStringLiteral( "blended.tif" ) );
  RSOperatorContext context;
  std::vector<NamedRasterFeed> feeds{
    { "before", { before.toStdString() }, {}, {}, {}, {} },
    { "after", { after.toStdString() }, {}, {}, {}, {} },
  };
  const TileInferenceStats stats =
    engine.runMultiInput( feeds, out.toStdString(), context, feather );

  CHECK( stats.outBands == 2 );
  CHECK( stats.tilesProcessed == 3 );
  checkFullGridAgainstOracle( out, rasterW, rasterH, ramp, false, 0.0f, kTwoClassBand, 2, 8,
                              2, 2 );
}
