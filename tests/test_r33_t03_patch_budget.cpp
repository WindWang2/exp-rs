/***************************************************************************
  tests/test_r33_t03_patch_budget.cpp — R33-T03 WP14: dataset patch
  admission. A NaN nodata threshold compared false against BOTH (0,1) bounds
  and silently dropped every patch; the grid tile count was computed with
  overflowing qint64 arithmetic ((width + stride - 1) / stride, tilesX *
  tilesY) whose wrapped product could pass the batch cap and admit a
  nonsense enumeration.
 ***************************************************************************/

#include "dataset/patch_generator.h"
#include "dataset/sample.h"

#include <catch2/catch_test_macros.hpp>

#include <QtGlobal>

#include <limits>

namespace
{

sicnu::dataset::GeoTransform northUp10m()
{
  sicnu::dataset::GeoTransform transform;
  transform.values = { 500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
  return transform;
}

} // namespace

TEST_CASE( "patch config refuses a NaN nodata threshold before any enumeration",
           "[dataset][patch][r33t03]" )
{
  using namespace sicnu::dataset;

  PatchGeneratorConfig cfg;
  cfg.strategy = PatchStrategy::FixedGrid;
  cfg.windowWidth = 100;
  cfg.windowHeight = 100;
  cfg.strideX = 100;
  cfg.strideY = 100;
  cfg.noDataMode = NoDataMode::MinValidFraction;
  cfg.noDataThreshold = std::numeric_limits<double>::quiet_NaN();

  const auto validated = cfg.validate();
  REQUIRE_FALSE( validated.isOk() );
  CHECK( validated.diagnostics().front().code == QStringLiteral( "dataset.patch_config_invalid" ) );
  CHECK( validated.diagnostics().front().message.contains( QStringLiteral( "finite" ) ) );

  // generate() runs the same validation first: the refusal must happen
  // BEFORE any window is emitted (pre-fix the NaN threshold was accepted
  // and `validFraction >= NaN` silently dropped every patch).
  const auto generated =
    PatchGenerator::generate( cfg, 200, 200, northUp10m(),
                              []( const PixelWindow & ) { return 1.0; } );
  REQUIRE_FALSE( generated.isOk() );
  CHECK( generated.diagnostics().front().code == QStringLiteral( "dataset.patch_config_invalid" ) );

  // Positive controls: finite bounds and a mid-interval threshold still pass.
  for ( const double fine : { 0.5, 0.01, 0.99 } )
  {
    PatchGeneratorConfig ok = cfg;
    ok.noDataThreshold = fine;
    CHECK( ok.validate().isOk() );
  }
}

TEST_CASE( "patch grid admission uses checked arithmetic at qint64 extremes",
           "[dataset][patch][r33t03]" )
{
  using namespace sicnu::dataset;

  PatchGeneratorConfig cfg;
  cfg.strategy = PatchStrategy::FixedGrid;
  cfg.windowWidth = 256;
  cfg.windowHeight = 256;
  cfg.strideX = 1;
  cfg.strideY = 1;

  SECTION( "tile product beyond qint64 is a typed refusal, never a wrapped count" )
  {
    // 4e9 x 4e9 tiles = 1.6e19 > qint64 max: the wrapped product used to
    // pass the batch cap (it came out negative) and the walk enumerated a
    // nonsense window set. Refused up front now.
    const auto res = PatchGenerator::generate( cfg, 4'000'000'000, 4'000'000'000,
                                               northUp10m(),
                                               []( const PixelWindow & ) { return 1.0; } );
    REQUIRE_FALSE( res.isOk() );
    CHECK( res.diagnostics().front().code == QStringLiteral( "dataset.patch_config_invalid" ) );
    CHECK( res.diagnostics().front().message.contains( QStringLiteral( "overflows" ) ) );
  }

  SECTION( "representable-but-over-budget counts keep the batch-cap refusal" )
  {
    // 1e6/100 = 1e4 tiles per axis -> 1e8 patches: representable, over the
    // 100000 batch cap — the pre-existing PERF-01 contract is unchanged.
    PatchGeneratorConfig big = cfg;
    big.strideX = 100;
    big.strideY = 100;
    const auto res = PatchGenerator::generate( big, 1'000'000, 1'000'000, northUp10m(),
                                               []( const PixelWindow & ) { return 1.0; } );
    REQUIRE_FALSE( res.isOk() );
    CHECK( res.diagnostics().front().code == QStringLiteral( "dataset.patch_budget_exceeded" ) );
  }

  SECTION( "extreme extents with huge strides enumerate without overflow UB" )
  {
    // ceilDiv(INT64_MAX, INT64_MAX/2) == 3 (2*stride == INT64_MAX-1 < width):
    // gridWindows' stepping used to overflow qint64 on the third
    // `x += stride` at width = qint64 max. The saturating step yields
    // exactly the three in-bounds origins, and the consumer sites
    // (applyBorder's inside test, PixelWindow::intersected, the footprint
    // edges) compute their ends overflow-free, so the whole path is
    // UB-free end to end — the third window is CLIPPED (not dropped) by
    // BorderPolicy::Clip against the 1px-tall raster.
    const qint64 hugeStride = std::numeric_limits<qint64>::max() / 2;
    PatchGeneratorConfig strideCfg = cfg;
    strideCfg.strategy = PatchStrategy::SlidingWindow;
    strideCfg.borderPolicy = BorderPolicy::Clip;   // Drop would early-return
    strideCfg.strideX = hugeStride;
    strideCfg.strideY = hugeStride;
    const auto windows =
      gridWindows( std::numeric_limits<qint64>::max(), 1, 256, 256,
                   strideCfg.strideX, strideCfg.strideY );
    REQUIRE( windows.size() == 3 );
    CHECK( windows[0].x == 0 );
    CHECK( windows[1].x == hugeStride );
    CHECK( windows[2].x == 2 * hugeStride );

    const auto res =
      PatchGenerator::generate( strideCfg, std::numeric_limits<qint64>::max(), 1,
                                northUp10m(), []( const PixelWindow & ) { return 1.0; } );
    REQUIRE( res.isOk() );
    REQUIRE( res.value().size() == 3 );
    CHECK_FALSE( res.value()[0].dropped );
    CHECK( res.value()[0].window.x == 0 );
    CHECK( res.value()[0].window.height == 1 );   // clipped to the raster
    // The last origin sits 1px before the extent end: clipped to 1px wide.
    CHECK( res.value()[2].window.x == 2 * hugeStride );
    CHECK( res.value()[2].window.width == 1 );
    CHECK_FALSE( res.value()[2].groundFootprintWkt.isEmpty() );
  }

  SECTION( "normal small grids are unchanged" )
  {
    PatchGeneratorConfig small = cfg;
    small.windowWidth = 100;
    small.windowHeight = 100;
    small.strideX = 100;
    small.strideY = 100;
    const auto res = PatchGenerator::generate( small, 200, 200, northUp10m(),
                                               []( const PixelWindow & ) { return 1.0; } );
    REQUIRE( res.isOk() );
    CHECK( res.value().size() == 4 );
  }
}
