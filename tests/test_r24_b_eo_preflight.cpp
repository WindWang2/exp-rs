// tests/test_r24_b_eo_preflight.cpp — R24-B (WP-B5): EO preflight honesty
// gaps found by adversarial review.
//   F1: eo.wavelengths_nm declared but the input contract binds NO
//       band_roles → the per-role window loop degenerated to "never
//       refuses, never advises" — the model's own sensitivity declaration
//       silently vanished from the report. Now: a named advisory.
//   F2: WAVELENGTH read as nm with WAVELENGTH_UNITS ignored — a converted
//       external product stamped WAVELENGTH=0.665/WAVELENGTH_UNITS=um was
//       refused as "centers at 0.665000 nm" (false block of a legitimate
//       run). Now: units normalize (um→nm) and an UNKNOWN unit leaves the
//       wavelength unset (absence advisory) instead of guessing.
// Fakes copied in trimmed form from tests/test_eo_platform_10.cpp.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"
#include "synthetic_raster_builder.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QTemporaryDir>

#include <string>

namespace {

using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;
using sicnu::operators::runtime::TileInferenceEngine;

class Argmax3Runtime : public IModelRuntime
{
  public:
    Argmax3Runtime( std::string artifact, std::string framework )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) )
    {
    }
    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "r24_b_argmax3"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }
    cv::Mat infer( const cv::Mat &blob ) override
    {
      const int N = blob.size[0], C = blob.size[1], H = blob.size[2], W = blob.size[3];
      int dims[4] = { N, 3, H, W };
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
};

struct ProviderGuard
{
    ProviderGuard()
    {
      ModelRuntimeRegistry::instance().releaseAll();
      ModelRuntimeRegistry::instance().registerProvider(
        "r24-b-eo",
        []( const ModelInfo &model, const ModelHardwareCapabilities &, std::string *error )
          -> ModelRuntimePtr {
          if ( model.resolvedArtifactPath.empty() )
          {
            if ( error )
              *error = "no resolved artifact";
            return nullptr;
          }
          return std::make_shared<Argmax3Runtime>( model.resolvedArtifactPath, "r24-b-eo" );
        } );
    }
    ~ProviderGuard() { ModelRuntimeRegistry::instance().releaseAll(); }
};

ModelInfo eoModel()
{
  ModelInfo info;
  info.name = "r24-b-eo";
  info.task = "segmentation";
  info.framework = "r24-b-eo";
  info.readiness = ModelReadiness::Ready;
  info.tiling.tileSize = 32;
  info.output.classes = { "a", "b", "c" };
  info.output.format = "labels";
  info.resolvedArtifactPath = "r24-b-eo-weights";
  info.eo.declared = true;
  info.eo.wavelengthsNm["red"] = { 630.0, 700.0 };
  return info;
}

/// Stamps WAVELENGTH (optionally with WAVELENGTH_UNITS) per band.
void stampWavelength( const QString &path, const std::vector<double> &values,
                      const QString &unit )
{
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>( GDALOpen( path.toUtf8().constData(), GA_Update ) );
  REQUIRE( ds );
  for ( int b = 0; b < static_cast<int>( values.size() ); ++b )
  {
    if ( values[b] <= 0.0 )
      continue;
    ds->GetRasterBand( b + 1 )
        ->SetMetadataItem( "WAVELENGTH", std::to_string( values[b] ).c_str() );
    if ( !unit.isEmpty() )
      ds->GetRasterBand( b + 1 )
          ->SetMetadataItem( "WAVELENGTH_UNITS", unit.toUtf8().constData() );
  }
  GDALClose( ds );
}

} // namespace

TEST_CASE( "eo preflight names wavelength windows it cannot check when no "
           "band roles are bound (R24-B)",
           "[r24_b][eo][preflight][wavelength]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "noroles-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "noroles-out.tif" ) );

  // Windows declared, input contract binds NO band roles (roles left empty).
  ModelInfo model = eoModel();
  model.inputs.clear();
  model.input.bandRoles.clear();

  stampWavelength( input, { 665.0, 832.0 }, QString() );

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats = engine.run( input.toStdString(), {}, output.toStdString(), context );
  CHECK( stats.eoPreflight["applicable"].asBool() );
  // F1: the unchecked-window state is NAMED, not silently absent. The old
  // loop emitted no advisory at all (checks AND advisories stayed empty).
  bool namedGap = false;
  for ( const auto &advisory : stats.eoPreflight["advisories"] )
    if ( advisory.asString().find( "binds no band_roles" ) != std::string::npos )
      namedGap = true;
  CHECK( namedGap );
}

TEST_CASE( "eo preflight normalizes WAVELENGTH_UNITS instead of refusing "
           "micrometer-stamped bands (R24-B)",
           "[r24_b][eo][preflight][units]" )
{
  ProviderGuard guard;
  QTemporaryDir dir;
  const QString input = dir.filePath( QStringLiteral( "um-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input );
  const QString output = dir.filePath( QStringLiteral( "um-out.tif" ) );

  // Role red over band 1 with window [630,700] nm; the band stamps
  // WAVELENGTH=0.665 WAVELENGTH_UNITS=um (ENVI/HDF-EOS family). The old
  // reader treated 0.665 as NM and refused the run typed.
  ModelInfo model = eoModel();
  model.inputs.clear();
  sicnu::operators::ModelInputContract feed;
  feed.name = "red";
  feed.bandRoles = { "red" };
  model.inputs.push_back( feed );
  model.input = feed;

  stampWavelength( input, { 0.665, 0.832 }, QStringLiteral( "um" ) );

  RSOperatorContext context;
  TileInferenceEngine engine( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats = engine.run( input.toStdString(), { 1 }, output.toStdString(), context );
  CHECK( stats.eoPreflight["applicable"].asBool() );
  bool checked = false;
  for ( const auto &check : stats.eoPreflight["checks"] )
    if ( check.asString().find( "within" ) != std::string::npos )
      checked = true;
  CHECK( checked );

  // Unknown unit string: absence, never a guess — the advisory path fires.
  const QString input2 = dir.filePath( QStringLiteral( "badunit-in.tif" ) );
  sicnu::testing::RsSyntheticRasterBuilder( 32, 32, 2, GDT_Float32 )
    .withCrs( QStringLiteral( "EPSG:4326" ) )
    .writeToDisk( input2 );
  const QString output2 = dir.filePath( QStringLiteral( "badunit-out.tif" ) );
  stampWavelength( input2, { 665.0, 832.0 }, QStringLiteral( "furlongs" ) );
  TileInferenceEngine engine2( model, ModelRuntimeRegistry::instance().acquire( model ) );
  const auto stats2 = engine2.run( input2.toStdString(), { 1 }, output2.toStdString(), context );
  bool absenceNamed = false;
  for ( const auto &advisory : stats2.eoPreflight["advisories"] )
    if ( advisory.asString().find( "declares no center wavelength" ) != std::string::npos )
      absenceNamed = true;
  CHECK( absenceNamed );
}
