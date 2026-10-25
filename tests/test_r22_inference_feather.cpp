// tests/test_r22_inference_feather.cpp — R22-03 inference track regression
// suite for #1706: FeatherAccumulator multi-slot aliasing + weight
// double-accumulation.
//
// #1706 (@911e0b17): resetWindow()'s `m_sum.assign(n, cv::Mat::zeros(...))`
// made every slot SHARE one cv::Mat buffer (Mat copies are refcounted
// shallow copies), and add() accumulated the feather weight once per SLOT,
// so a C-class feathered run published mean(probabilities) on every band and
// argmax=class 0 everywhere. These cases pin the per-slot contract:
//   - every class band blends independently (known cosine answers at the
//     seam, exact constants in pure cores),
//   - the per-pixel weight accumulates ONCE per contributing tile,
//   - derived argmax matches the unblended argmax,
//   - the row-window slide keeps slots isolated on tall rasters,
//   - all-nodata runs stay NoData in EVERY band,
//   - the uncertainty slot blends independently of the class slots.
// Runs in EVERY OpenCV-enabled build (fake provider, synthetic rasters).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators;
using namespace sicnu::operators::runtime;

/// Reads one band of a raster as float (GDAL convert-on-read).
std::vector<float> readBand( const QString &path, int band, int &width, int &height )
{
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds != nullptr );
  width = ds->GetRasterXSize();
  height = ds->GetRasterYSize();
  std::vector<float> data( static_cast<std::size_t>( width ) * height );
  REQUIRE( ds->GetRasterBand( band )
             ->RasterIO( GF_Read, 0, 0, width, height, data.data(), width, height,
                         GDT_Float32, 0, 0 ) == CE_None );
  GDALClose( ds );
  return data;
}

/// Fake three-class runtime: predicts a CONSTANT per-channel plane whose
/// profile depends on the value at the fed window's core center — 10 picks
/// the "class 0 dominant" profile [0.8, 0.1, 0.1], 20 the "class 2 dominant"
/// profile [0.1, 0.1, 0.8]. A NaN center (all-nodata tile) still emits the
/// right profile; the ENGINE masks such cores back to NaN after inference,
/// which is the contract under test in the NoData case.
class ThreeClassStepRuntime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "m9-fake"; }
    std::string backendName() const override { return "r22step"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "fake://r22step"; }

    bool supportsMultiInput() const override { return false; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int B = blob.size[0];
      const int H = blob.size[2];
      const int W = blob.size[3];
      const float v = blob.ptr<float>( 0 )[static_cast<std::size_t>( H / 2 ) * W
                                            + static_cast<std::size_t>( W / 2 )];
      const bool left = v < 15.0f;
      // The output stack is the MODEL's head, not the input's band count:
      // three class channels whatever the fed raster carries.
      const int C = 3;
      const int dims[4] = { B, C, H, W };
      cv::Mat out( 4, dims, CV_32F );
      // (b, c, y, x) live at ((b·C + c)·H + y)·W + x in the contiguous 4D Mat.
      float *base = out.ptr<float>( 0 );
      const std::size_t plane = static_cast<std::size_t>( H ) * W;
      for ( int b = 0; b < B; ++b )
        for ( int c = 0; c < C; ++c )
        {
          const float value = ( ( c == 0 && left ) || ( c == 2 && !left ) ) ? 0.8f : 0.1f;
          std::fill_n( base + ( static_cast<std::size_t>( b * C + c ) ) * plane, plane, value );
        }
      return out;
    }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      ( void )outputNames;
      const cv::Mat mat = inputs.front().second.toMat();
      return { NamedTensor{ std::string(), TensorBlob::fromMat( infer( mat ) ) } };
    }
};

/// The 32×16 left/right step raster shared by the seam cases (10 left of
/// x=16, 20 right of it), like the M5 single-channel fixture.
QString writeStepRaster( const QTemporaryDir &dir, const QString &name, int height )
{
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, height, 1, GDT_Float32 );
  builder.withRect( 1, 0, 0, 16, height, 10.0f );
  builder.withRect( 1, 16, 0, 32, height, 20.0f );
  const QString raster = builder.writeToDisk( dir.filePath( name ) );
  REQUIRE_FALSE( raster.isEmpty() );
  return raster;
}

ModelInfo threeClassModel()
{
  ModelInfo model;
  model.name = "r22-feather3";
  model.framework = "m9-fake";
  model.task = "segmentation";
  model.tiling.tileSize = 16;
  model.tiling.halo = 4;
  model.output.classes = { "water", "bare", "forest" };
  return model;
}

/// Cosine feather weight at distance dx from the core (halo h): 0 inside
/// the core, 0.5·(1+cos(π·dx/h)) in the halo.
double featherWeight( int dx, int halo )
{
  if ( dx <= 0 )
    return 1.0;
  if ( dx >= halo )
    return 0.0;
  return 0.5 * ( 1.0 + std::cos( dx * 3.14159265358979323846 / halo ) );
}

} // namespace

// ---------------------------------------------------------------------------
// #1706 — probability stack: per-class bands must stay independent
// ---------------------------------------------------------------------------

TEST_CASE( "#1706 feather probability keeps the three class bands independent",
           "[r22][blend][feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeStepRaster( dir, QStringLiteral( "step.tif" ), 16 );

  ModelInfo model = threeClassModel();

  TileInferenceRunOptions none;
  none.blend = TileBlend::None;
  const QString plainPath = dir.filePath( QStringLiteral( "plain.tif" ) );
  TileInferenceEngine plainEngine( model, std::make_shared<ThreeClassStepRuntime>() );
  RSOperatorContext context;
  plainEngine.run( raster.toStdString(), {}, plainPath.toStdString(), context, none );

  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString blendedPath = dir.filePath( QStringLiteral( "blended.tif" ) );
  TileInferenceEngine engine( model, std::make_shared<ThreeClassStepRuntime>() );
  engine.run( raster.toStdString(), {}, blendedPath.toStdString(), context, feather );

  int w = 0;
  int h = 0;
  const std::vector<float> plain1 = readBand( plainPath, 1, w, h );
  const std::vector<float> plain3 = readBand( plainPath, 3, w, h );
  const std::vector<float> band1 = readBand( blendedPath, 1, w, h );
  const std::vector<float> band2 = readBand( blendedPath, 2, w, h );
  const std::vector<float> band3 = readBand( blendedPath, 3, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 16 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };

  // Unblended reference: pure-core class constants, band per class.
  CHECK( at( plain1, 8, 8 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );
  CHECK( at( plain3, 8, 8 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( plain1, 24, 8 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( plain3, 24, 8 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );

  // Pure cores: each band blends to its own class constant exactly (a
  // single tile covers the pixel with weight 1 — the aliased accumulator
  // published mean(0.8,0.1,0.1)=1/3 on every band instead).
  CHECK( at( band1, 8, 8 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );
  CHECK( at( band2, 8, 8 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( band3, 8, 8 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( band1, 24, 8 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( band2, 24, 8 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( band3, 24, 8 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );
  // The class bands must differ where the classes differ.
  CHECK( std::abs( at( band1, 8, 8 ) - at( band3, 8, 8 ) ) > 0.5f );

  // Seam with known cosine answers (x=14: left core weight 1, right halo
  // distance 2/4 → weight 0.5 — the weight must be counted ONCE per tile,
  // not once per class slot):
  //   band1 = (0.8·1 + 0.1·0.5)/(1 + 0.5)
  //   band3 = (0.1·1 + 0.8·0.5)/(1 + 0.5)
  CHECK( at( band1, 14, 8 ) == Catch::Approx( ( 0.8 + 0.1 * 0.5 ) / 1.5 ).margin( 1e-3 ) );
  CHECK( at( band3, 14, 8 ) == Catch::Approx( ( 0.1 + 0.8 * 0.5 ) / 1.5 ).margin( 1e-3 ) );
  // x=18: left halo distance 3/4 → wA = 0.5·(1+cos(3π/4)), right core 1.
  const double wA = featherWeight( 3, 4 );
  CHECK( wA == Catch::Approx( 0.1464466 ).margin( 1e-5 ) );
  CHECK( at( band1, 18, 8 ) == Catch::Approx( ( 0.8 * wA + 0.1 ) / ( wA + 1.0 ) ).margin( 1e-3 ) );
  CHECK( at( band3, 18, 8 ) == Catch::Approx( ( 0.1 * wA + 0.8 ) / ( wA + 1.0 ) ).margin( 1e-3 ) );
}

// ---------------------------------------------------------------------------
// #1706 — derived labels: blended argmax must match the unblended argmax
// ---------------------------------------------------------------------------

TEST_CASE( "#1706 feather labels argmax matches the unblended argmax",
           "[r22][blend][feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeStepRaster( dir, QStringLiteral( "step.tif" ), 16 );

  ModelInfo model = threeClassModel();
  model.output.format = "labels";

  TileInferenceRunOptions none;
  none.blend = TileBlend::None;
  const QString plainPath = dir.filePath( QStringLiteral( "plain-labels.tif" ) );
  TileInferenceEngine plainEngine( model, std::make_shared<ThreeClassStepRuntime>() );
  RSOperatorContext context;
  plainEngine.run( raster.toStdString(), {}, plainPath.toStdString(), context, none );

  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString blendedPath = dir.filePath( QStringLiteral( "blended-labels.tif" ) );
  TileInferenceEngine engine( model, std::make_shared<ThreeClassStepRuntime>() );
  engine.run( raster.toStdString(), {}, blendedPath.toStdString(), context, feather );

  int w = 0;
  int h = 0;
  const std::vector<float> plain = readBand( plainPath, 1, w, h );
  const std::vector<float> blended = readBand( blendedPath, 1, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 16 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };

  // Unblended truth: argmax is class 0 on the left, class 2 on the right.
  CHECK( at( plain, 8, 8 ) == 0.0f );
  CHECK( at( plain, 24, 8 ) == 2.0f );

  // Blending probabilities then collapsing must preserve the argmax at pure
  // cores. The aliased accumulator made every slot read the class MEAN, so
  // argmax tied at 0 everywhere and the right half silently became class 0.
  CHECK( at( blended, 8, 8 ) == 0.0f );
  CHECK( at( blended, 24, 8 ) == 2.0f );
  // Seam x=14: blended (0.5667, 0.1, 0.3333) → class 0. x=18: blended
  // (0.189, 0.1, 0.710) → class 2 — the seam flips exactly once.
  CHECK( at( blended, 14, 8 ) == 0.0f );
  CHECK( at( blended, 18, 8 ) == 2.0f );
  // Every row agrees (the profile is constant along y).
  for ( int y = 0; y < 16; ++y )
  {
    INFO( "row " << y );
    CHECK( at( blended, 8, y ) == 0.0f );
    CHECK( at( blended, 24, y ) == 2.0f );
  }
}

// ---------------------------------------------------------------------------
// #1706 — tall raster: the row-window slide must keep slots isolated
// ---------------------------------------------------------------------------

TEST_CASE( "#1706 feather slides the row window without cross-slot bleed",
           "[r22][blend][feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // 32×48: tileSize 16 + 2·halo 4 → window span 24 rows < 48, so the
  // accumulator slides its window at least once mid-run.
  const QString raster = writeStepRaster( dir, QStringLiteral( "tall.tif" ), 48 );

  ModelInfo model = threeClassModel();

  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString blendedPath = dir.filePath( QStringLiteral( "tall-blended.tif" ) );
  TileInferenceEngine engine( model, std::make_shared<ThreeClassStepRuntime>() );
  RSOperatorContext context;
  engine.run( raster.toStdString(), {}, blendedPath.toStdString(), context, feather );

  int w = 0;
  int h = 0;
  const std::vector<float> band1 = readBand( blendedPath, 1, w, h );
  const std::vector<float> band3 = readBand( blendedPath, 3, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 48 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };

  // Rows in the third tile-row (below two window slides) still carry the
  // per-class constants, not the class mean.
  CHECK( at( band1, 8, 40 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );
  CHECK( at( band3, 8, 40 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( band1, 24, 40 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
  CHECK( at( band3, 24, 40 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );
  // The horizontal seam still blends per class deep in the raster.
  CHECK( at( band1, 14, 40 ) == Catch::Approx( ( 0.8 + 0.1 * 0.5 ) / 1.5 ).margin( 1e-3 ) );
  CHECK( at( band3, 14, 40 ) == Catch::Approx( ( 0.1 + 0.8 * 0.5 ) / 1.5 ).margin( 1e-3 ) );
}

// ---------------------------------------------------------------------------
// #1706 — all-nodata run taller than the window: every band stays NoData
// ---------------------------------------------------------------------------

TEST_CASE( "#1706 feather keeps an all-nodata run NoData in every class band",
           "[r22][blend][feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // #r18B geometry, now multi-class: top 32 rows all NaN, bottom 16 valid.
  const float kNaN = std::numeric_limits<float>::quiet_NaN();
  sicnu::testing::RsSyntheticRasterBuilder builder( 32, 48, 1, GDT_Float32 );
  builder.withRect( 1, 0, 0, 32, 32, kNaN );
  builder.withRect( 1, 0, 32, 32, 48, 10.0f );
  const QString raster = builder.writeToDisk( dir.filePath( QStringLiteral( "nodata.tif" ) ) );
  REQUIRE_FALSE( raster.isEmpty() );

  ModelInfo model = threeClassModel();
  model.tiling.batchSize = 1; // defer each all-nodata tile on its own (#r18B path)

  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString blendedPath = dir.filePath( QStringLiteral( "nodata-blended.tif" ) );
  TileInferenceEngine engine( model, std::make_shared<ThreeClassStepRuntime>() );
  RSOperatorContext context;
  engine.run( raster.toStdString(), {}, blendedPath.toStdString(), context, feather );

  int w = 0;
  int h = 0;
  const std::vector<float> band1 = readBand( blendedPath, 1, w, h );
  const std::vector<float> band3 = readBand( blendedPath, 3, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 48 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };

  for ( int y = 0; y < 32; ++y )
    for ( int x = 8; x < 24; ++x )
    {
      INFO( "band1 row " << y << " col " << x );
      CHECK( std::isnan( at( band1, x, y ) ) );
      INFO( "band3 row " << y << " col " << x );
      CHECK( std::isnan( at( band3, x, y ) ) );
    }
  // Valid rows keep the per-class constants (left region: class 0 wins).
  CHECK( at( band1, 8, 40 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );
  CHECK( at( band3, 8, 40 ) == Catch::Approx( 0.1f ).margin( 1e-4 ) );
}

// ---------------------------------------------------------------------------
// #1706 — uncertainty slot: blends independently of the class slots
// ---------------------------------------------------------------------------

TEST_CASE( "#1706 feather blends the uncertainty slot independently",
           "[r22][blend][feather]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString raster = writeStepRaster( dir, QStringLiteral( "step.tif" ), 16 );

  ModelInfo model = threeClassModel();
  model.output.uncertainty = "margin"; // adds band 4: softmax top1 − top2

  TileInferenceRunOptions feather;
  feather.blend = TileBlend::Feather;
  const QString blendedPath = dir.filePath( QStringLiteral( "unc.tif" ) );
  TileInferenceEngine engine( model, std::make_shared<ThreeClassStepRuntime>() );
  RSOperatorContext context;
  engine.run( raster.toStdString(), {}, blendedPath.toStdString(), context, feather );

  int w = 0;
  int h = 0;
  const std::vector<float> band1 = readBand( blendedPath, 1, w, h );
  const std::vector<float> band4 = readBand( blendedPath, 4, w, h );
  REQUIRE( w == 32 );
  REQUIRE( h == 16 );
  auto at = []( const std::vector<float> &img, int x, int y ) {
    return img[static_cast<std::size_t>( y ) * 32 + x];
  };

  // margin([0.8, 0.1, 0.1]) as logits: softmax ≈ (0.50175, 0.24913,
  // 0.24913) → top1 − top2 ≈ 0.25262. A shared buffer published the mean
  // over all four slots instead.
  const double e08 = std::exp( 0.8 );
  const double e01 = std::exp( 0.1 );
  const double sum = e08 + 2.0 * e01;
  const double expectedMargin = ( e08 / sum ) - ( e01 / sum );
  CHECK( at( band1, 8, 8 ) == Catch::Approx( 0.8f ).margin( 1e-4 ) );
  CHECK( at( band4, 8, 8 ) == Catch::Approx( expectedMargin ).margin( 1e-3 ) );
  // The right profile is the mirrored stack — same margin value.
  CHECK( at( band4, 24, 8 ) == Catch::Approx( expectedMargin ).margin( 1e-3 ) );
  // The uncertainty band must NOT equal the class band (aliasing symptom).
  CHECK( std::abs( at( band4, 8, 8 ) - at( band1, 8, 8 ) ) > 0.3f );
}
