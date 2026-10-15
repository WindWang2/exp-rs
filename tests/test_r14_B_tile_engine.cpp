// tests/test_r14_B_tile_engine.cpp — R14-B tile-inference engine regressions:
// strided-head halo alignment (stitch must be tile-count invariant), stitched
// Mask class tallies, morphology re-tally semantics + stage hygiene, and the
// cv::Mat channel-limit refusal for >512-band inputs.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

#include <opencv2/core.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gdal.h>
#include <gdal_priv.h>

namespace {

using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::TileInferenceEngine;

/// Writes a 1-band float32 GTiff whose pixel (r, c) carries the code fn(r, c)
/// (the builder has no per-pixel setter; a raw GDAL write is shorter).
QString writeCodeRaster( const QString &path, int w, int h,
                         const std::function<float( int, int )> &fn )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32,
                                nullptr );
  REQUIRE( ds );
  std::vector<float> row( static_cast<std::size_t>( w ) );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  for ( int r = 0; r < h; ++r )
  {
    for ( int c = 0; c < w; ++c )
      row[static_cast<std::size_t>( c )] = fn( r, c );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 )
             == CE_None );
  }
  GDALClose( ds );
  return path;
}

/// Stride-2 head: (B, C, H, W) → (B, C, H/2, W/2), out(y, x) = in(2y, 2x).
class Stride2Runtime final : public IModelRuntime
{
  public:
    std::string framework() const override { return "stridedfw"; }
    std::string backendName() const override { return "stride2-fake"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "stride2-fixture"; }
    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      const int B = nchwBlob.size[0];
      const int C = nchwBlob.size[1];
      const int H = nchwBlob.size[2];
      const int W = nchwBlob.size[3];
      const int dims[4] = { B, C, H / 2, W / 2 };
      cv::Mat out( 4, dims, CV_32F );
      for ( int b = 0; b < B; ++b )
        for ( int c = 0; c < C; ++c )
          for ( int y = 0; y < H / 2; ++y )
            for ( int x = 0; x < W / 2; ++x )
              out.ptr<float>( b, c, 0 )[static_cast<std::size_t>( y ) * ( W / 2 ) + x] =
                nchwBlob.ptr<float>( b, c, 0 )[static_cast<std::size_t>( 2 * y ) * W + 2 * x];
      return out;
    }
};

/// Foreground-probability head over a binary field: emits C channels of
/// class probabilities from a single indicator channel (c0 = 1-v, c1 = v, ...).
class ClassProbRuntime final : public IModelRuntime
{
  public:
    explicit ClassProbRuntime( int classes )
        : m_classes( classes )
    {
    }
    std::string framework() const override { return "prob"; }
    std::string backendName() const override { return "prob-fake"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return "prob-fixture"; }
    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      const int B = nchwBlob.size[0];
      const int C = nchwBlob.size[1];
      const int H = nchwBlob.size[2];
      const int W = nchwBlob.size[3];
      const int dims[4] = { B, m_classes, H, W };
      cv::Mat out( 4, dims, CV_32F );
      for ( int b = 0; b < B; ++b )
        for ( int y = 0; y < H; ++y )
          for ( int x = 0; x < W; ++x )
          {
            const float v = nchwBlob.ptr<float>( b, 0, 0 )[static_cast<std::size_t>( y ) * W + x];
            for ( int c = 0; c < m_classes; ++c )
            {
              const float p = c == 0 ? 1.0f - v : ( c == 1 ? v : 0.0f );
              out.ptr<float>( b, c, 0 )[static_cast<std::size_t>( y ) * W + x] = p;
            }
          }
      return out;
    }

  private:
    int m_classes;
};

ModelInfo engineModel( const std::string &framework )
{
  ModelInfo info;
  info.name = "r14b";
  info.framework = framework;
  info.readiness = ModelReadiness::Ready;
  info.resolvedArtifactPath = "/tmp/r14b-fixture.onnx";
  info.tiling.tileSize = 64;
  info.tiling.overlap = 16; // halo 8
  return info;
}

std::vector<float> readBand( const QString &path, int band, int w, int h )
{
  GdalDatasetWrapper ds;
  REQUIRE( ds.open( path ) );
  REQUIRE( ds.width() == w );
  REQUIRE( ds.height() == h );
  std::vector<float> pixels( static_cast<std::size_t>( w ) * h );
  REQUIRE( ds.readBandData( band, pixels.data(), w, h ) );
  return pixels;
}

} // namespace

TEST_CASE( "#r14B strided-head stitching is tile-count invariant and halo-aligned",
           "[models][engine][r14B]" )
{
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "code.tif" ) );
  writeCodeRaster( input, 128, 128, []( int, int c ) { return static_cast<float>( c ); } );

  const auto session = std::make_shared<Stride2Runtime>();

  std::vector<float> tiled;
  {
    ModelInfo info = engineModel( "stridedfw" );
    info.preprocess.interpolation = "nearest";
    RSOperatorContext context;
    TileInferenceEngine engine( info, session );
    engine.run( input.toStdString(), {}, dir.filePath( QStringLiteral( "tiled.tif" ) ).toStdString(),
                context );
    tiled = readBand( dir.filePath( QStringLiteral( "tiled.tif" ) ), 1, 128, 128 );
  }
  std::vector<float> single;
  {
    ModelInfo info = engineModel( "stridedfw" );
    info.preprocess.interpolation = "nearest";
    info.tiling.tileSize = 128; // one core tile, no seams
    RSOperatorContext context;
    TileInferenceEngine engine( info, session );
    engine.run( input.toStdString(), {}, dir.filePath( QStringLiteral( "single.tif" ) ).toStdString(),
                context );
    single = readBand( dir.filePath( QStringLiteral( "single.tif" ) ), 1, 128, 128 );
  }

  // (1) Tile-count invariance: the stitched product may not depend on where
  // the seams fell. The old window→core resize mapped window pixel (0,0)
  // onto core pixel (0,0) — a per-tile shift of halo·(core/fed) that moved
  // with each tile origin.
  for ( int r = 0; r < 128; ++r )
    for ( int c = 0; c < 128; ++c )
    {
      const std::size_t i = static_cast<std::size_t>( r ) * 128 + c;
      INFO( "r=" << r << " c=" << c << " tiled=" << tiled[i] << " single=" << single[i] );
      CHECK( tiled[i] == Catch::Approx( single[i] ).margin( 1e-4 ) );
    }

  // (2) Absolute alignment: a stride-2 head sampling even window pixels
  // must, on EVEN columns, reproduce the raster exactly (the core starts
  // halo pixels into the window; a misaligned crop misses by that margin).
  for ( int r = 0; r < 128; r += 2 )
    for ( int c = 0; c < 128; c += 2 )
    {
      const std::size_t i = static_cast<std::size_t>( r ) * 128 + c;
      INFO( "r=" << r << " c=" << c << " got=" << tiled[i] );
      CHECK( tiled[i] == Catch::Approx( static_cast<float>( c ) ).margin( 1e-3 ) );
    }
}

TEST_CASE( "#r14B stitched Mask products tally their class pixel counts",
           "[models][engine][r14B]" )
{
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "fg.tif" ) );
  writeCodeRaster( input, 32, 32, []( int r, int c ) {
    return ( r >= 14 && r <= 16 && c >= 14 && c <= 16 ) ? 0.9f : 0.1f;
  } );

  ModelInfo info = engineModel( "prob" );
  info.tiling.tileSize = 32; // single tile, stitched path
  info.output.classes = { "background", "foreground" };
  info.output.format = "mask";
  const auto session = std::make_shared<ClassProbRuntime>( 2 );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  const auto stats = engine.run( input.toStdString(), {},
                                 dir.filePath( QStringLiteral( "mask.tif" ) ).toStdString(),
                                 context );
  REQUIRE( stats.classPixelCounts.size() >= 2 );
  CHECK( stats.classPixelCounts[0] == 32 * 32 - 9 );
  CHECK( stats.classPixelCounts[1] == 9 );
}

TEST_CASE( "#r14B morphology re-tally replaces the pre-morphology counts",
           "[models][engine][r14B]" )
{
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "fg.tif" ) );
  writeCodeRaster( input, 32, 32, []( int r, int c ) {
    return ( r >= 14 && r <= 16 && c >= 14 && c <= 16 ) ? 0.9f : 0.1f;
  } );

  ModelInfo info = engineModel( "prob" );
  info.tiling.tileSize = 32;
  info.output.classes = { "background", "foreground" };
  info.output.format = "labels";
  info.postprocess.morphology = "erode";
  info.postprocess.morphologyKernelPx = 3;
  const auto session = std::make_shared<ClassProbRuntime>( 2 );

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  const auto stats = engine.run( input.toStdString(), {},
                                 dir.filePath( QStringLiteral( "labels.tif" ) ).toStdString(),
                                 context );
  // erode(3x3 block, kernel 3) keeps exactly the center pixel: the PUBLISHED
  // product holds ONE foreground pixel. The old re-tally added the
  // pre-morphology count on top (9 + 1 = 10 — a count describing neither
  // product).
  REQUIRE( stats.classPixelCounts.size() >= 2 );
  CHECK( stats.classPixelCounts[1] == 1 );
  CHECK( stats.classPixelCounts[0] == 32 * 32 - 1 );

  // Stage hygiene: neither the staging temp nor the morphology intermediate
  // may outlive a successful run.
  CHECK_FALSE( QFileInfo::exists( dir.filePath( QStringLiteral( "labels.tif.tmp~" ) ) ) );
  CHECK_FALSE( QFileInfo::exists( dir.filePath( QStringLiteral( "labels.tif.tmp~.morph~" ) ) ) );
}

TEST_CASE( "#r14B engine refuses >512-band inputs instead of wrapping the Mat type",
           "[models][engine][r14B]" )
{
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "hyper.tif" ) );
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  GDALDatasetH ds =
    GDALCreate( driver, input.toUtf8().constData(), 8, 8, 513, GDT_Float32, nullptr );
  REQUIRE( ds );
  GDALClose( ds );

  ModelInfo info = engineModel( "stridedfw" ); // bands empty → all 513 fed
  const auto session = std::make_shared<Stride2Runtime>();

  RSOperatorContext context;
  TileInferenceEngine engine( info, session );
  try
  {
    engine.run( input.toStdString(), {}, dir.filePath( QStringLiteral( "out.tif" ) ).toStdString(),
                context );
    FAIL( "a 513-band input must be refused" );
  }
  catch ( const sicnu::operators::RSOperatorError &e )
  {
    INFO( e.what() );
    CHECK( std::string( e.what() ).find( "channels" ) != std::string::npos );
  }
  CHECK_FALSE( QFileInfo::exists( dir.filePath( QStringLiteral( "out.tif" ) ) ) );
  CHECK_FALSE( QFileInfo::exists( dir.filePath( QStringLiteral( "out.tif.tmp~" ) ) ) );
}
