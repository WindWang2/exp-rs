// tests/test_r24_b_metamorphic.cpp — R24-B (WP-B6, oracle O-B3): tile-size
// invariance oracles for the raster classification lane. A per-pixel
// deterministic classifier (argmax over channels of the FED window) must
// produce a byte-identical labels raster regardless of how the engine
// tiles the input:
//   * tileSize sweep over non-divisor sizes (tail-tile math),
//   * halo>0 + "feather" blending (the #1709 per-slot accumulation path —
//     every overlapping tile yields the same one-hot plane at shared
//     pixels, so the blended result is invariant),
//   * double-run determinism (same config → same bytes).
// These properties had NO coverage: the existing suites pin single-tile
// arithmetic, never tile-size as a variable.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QTemporaryDir>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::TileInferenceEngine;
using sicnu::operators::runtime::TileInferenceStats;

/// Per-pixel argmax over the fed window's channels: output depends ONLY on
/// the (center) pixel content, so tiling cannot legitimately change it.
class ArgmaxRuntime : public IModelRuntime
{
  public:
    ArgmaxRuntime( std::string artifact, std::string framework, int classes )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_classes( classes )
    {
    }
    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "r24_b_argmax"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int N = blob.size[0], C = blob.size[1], H = blob.size[2], W = blob.size[3];
      int dims[4] = { N, m_classes, H, W };
      cv::Mat out( 4, dims, CV_32F, cv::Scalar( 0.0f ) );
      for ( int n = 0; n < N; ++n )
        for ( int y = 0; y < H; ++y )
          for ( int x = 0; x < W; ++x )
          {
            int best = 0;
            float bestV = blob.ptr<float>( n, 0, y )[x];
            for ( int c = 1; c < C; ++c )
            {
              const float v = blob.ptr<float>( n, c, y )[x];
              if ( v > bestV )
              {
                bestV = v;
                best = c;
              }
            }
            out.ptr<float>( n, best, y )[x] = 1.0f;
          }
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    int m_classes;
};

struct ProviderGuard
{
    ProviderGuard()
    {
      ModelRuntimeRegistry::instance().releaseAll();
      ModelRuntimeRegistry::instance().registerProvider(
        "r24-b-meta",
        []( const ModelInfo &model, const ModelHardwareCapabilities &, std::string *error )
          -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          return std::make_shared<ArgmaxRuntime>( model.resolvedArtifactPath, "r24-b-meta",
                                                  3 );
        } );
    }
    ~ProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

ModelInfo labelsModel( int tileSize, int halo, const std::string &blend )
{
  ModelInfo info;
  info.name = "r24-b-meta";
  info.task = "segmentation";
  info.framework = "r24-b-meta";
  info.readiness = ModelReadiness::Ready;
  info.tiling.tileSize = tileSize;
  info.tiling.halo = halo;
  info.tiling.blend = blend;
  info.output.classes = { "a", "b", "c" };
  info.output.format = "labels";
  info.resolvedArtifactPath = "r24-b-meta-weights";
  // width/height 0 → dynamic input, no resize: the fed window IS the tile.
  return info;
}

std::vector<std::uint16_t> readLabelsBand( const QString &path )
{
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  REQUIRE( ds );
  const int w = ds->GetRasterXSize();
  const int h = ds->GetRasterYSize();
  REQUIRE( ds->GetRasterCount() == 1 );
  const GDALDataType type = ds->GetRasterBand( 1 )->GetRasterDataType();
  REQUIRE( type == GDT_UInt16 );
  std::vector<std::uint16_t> values( static_cast<std::size_t>( w ) * h );
  for ( int y = 0; y < h; ++y )
    REQUIRE( ds->GetRasterBand( 1 )
                 ->RasterIO( GF_Read, 0, y, w, 1, values.data() + y * w, w, 1, GDT_UInt16, 0,
                             0 ) == CE_None );
  GDALClose( ds );
  return values;
}

} // namespace

TEST_CASE( "classification labels are invariant under tile size, feathering "
           "and re-runs (R24-B metamorphic)",
           "[r24_b][metamorphic][tile][labels]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;

  // 96x64 raster: 96 is divisible by 16 and 24 but NOT by 40 → tail tiles
  // in both axes for one of the sizes. Band content varies per pixel so the
  // argmax actually differs across the raster (a constant raster would make
  // any stitching bug invisible).
  const QString input = dir.filePath( QStringLiteral( "meta-in.tif" ) );
  {
    sicnu::testing::RsSyntheticRasterBuilder builder( 96, 64, 3, GDT_Float32 );
    builder.withCrs( QStringLiteral( "EPSG:4326" ) );
    // withConstantValue per band then per-pixel variation via regions:
    // the builder supports constant bands; vary through band-dominant
    // regions by writing raw pixels after creation.
    builder.writeToDisk( input );
    GDALAllRegister();
    GDALDataset *ds =
      static_cast<GDALDataset *>( GDALOpen( input.toUtf8().constData(), GA_Update ) );
    REQUIRE( ds );
    std::vector<float> row( 96 );
    for ( int y = 0; y < 64; ++y )
      for ( int band = 1; band <= 3; ++band )
      {
        for ( int x = 0; x < 96; ++x )
        {
          // Three class regions with a diagonal boundary so tile seams
          // cross class boundaries (the interesting case for stitching).
          const double t = ( x + y ) / 160.0;
          row[x] = band == 1 ? float( 1.0 - t ) : ( band == 2 ? float( t ) : float( t * 0.5 ) );
        }
        REQUIRE( ds->GetRasterBand( band )
                     ->RasterIO( GF_Write, 0, y, 96, 1, row.data(), 96, 1, GDT_Float32, 0,
                                 0 ) == CE_None );
      }
    GDALClose( ds );
  }

  struct Variant
  {
    int tileSize;
    int halo;
    std::string blend;
  };
  // 16: divisor; 40: tail tile in x (96 = 2*40 + 16); 33: tail in both axes.
  const std::vector<Variant> variants{
    { 16, 0, "" }, { 40, 0, "" }, { 33, 0, "" },
    { 16, 8, "feather" }, { 40, 8, "feather" }, { 33, 6, "feather" },
  };

  std::map<std::string, std::vector<std::uint16_t>> outputs;
  for ( const Variant &variant : variants )
  {
    const ModelInfo model =
      labelsModel( variant.tileSize, variant.halo, variant.blend );
    TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
    RSOperatorContext context;
    const QString output =
      dir.filePath( QStringLiteral( "meta-out-%1-%2-%3.tif" )
                        .arg( variant.tileSize )
                        .arg( variant.halo )
                        .arg( QString::fromStdString( variant.blend.empty() ? "none" : variant.blend ) ) );
    const auto stats =
      engine.run( input.toStdString(), {}, output.toStdString(), context );
    REQUIRE( stats.tilesProcessed > 0 );
    const std::string key = std::to_string( variant.tileSize ) + "-"
                          + std::to_string( variant.halo ) + "-" + variant.blend;
    outputs[key] = readLabelsBand( output );
  }

  // Every variant must agree BYTE-IDENTICALLY with the reference (16/none).
  const std::vector<std::uint16_t> &reference = outputs["16-0-"];
  REQUIRE( reference.size() == 96u * 64u );
  // Sanity: the reference is not constant (regions really differ).
  CHECK( *std::min_element( reference.begin(), reference.end() )
         != *std::max_element( reference.begin(), reference.end() ) );
  for ( const auto &[ key, labels ] : outputs )
  {
    INFO( "variant " << key );
    REQUIRE( labels.size() == reference.size() );
    CHECK( labels == reference );
  }

  // Determinism: a re-run of one feathered variant reproduces its own bytes.
  {
    const ModelInfo model = labelsModel( 40, 8, "feather" );
    TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
    RSOperatorContext context;
    const QString output = dir.filePath( QStringLiteral( "meta-out-rerun.tif" ) );
    engine.run( input.toStdString(), {}, output.toStdString(), context );
    CHECK( readLabelsBand( output ) == outputs["40-8-feather"] );
  }
}
