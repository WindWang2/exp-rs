// tests/test_ensemble_parallel.cpp — bounded parallel member execution
// (Platform 13.0): serial vs bounded-parallel output EQUIVALENCE on
// deterministic members, a provable admission ceiling (no "parallel = start
// everything"), 8-thread concurrent ensemble stress with ledger conservation,
// and zero staged residue after injected member failure and cancellation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <json/json.h>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_ensemble.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provenance_verify.h"
#include "synthetic_raster_builder.h"

#include "runtime/observability/fault_registry.h"

#include <gdal_priv.h>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using sicnu::operators::ModelCatalog;
using sicnu::operators::ModelInfo;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::RSOperatorError;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelExecutionRequest;
using sicnu::operators::runtime::ModelExecutionResult;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;

QString identityModelPath()
{
  return QFileInfo( __FILE__ ).absolutePath() + QStringLiteral( "/data/test_infer_identity.onnx" );
}

/// Scale provider: NCHW in → NCHW out × scale (per framework id), so member
/// outputs are exact, hand-checkable multiples of the constant input.
class ScaleRuntime final : public IModelRuntime
{
  public:
    ScaleRuntime( std::string artifact, std::string framework, double scale )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_scale( scale ) {}

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "scale_backend"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      cv::Mat out = blob.clone();
      out.convertTo( out, CV_32F, m_scale );
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    double m_scale;
};

/// Observable concurrency probe shared by all ensemble runs in a process:
/// every forward pass registers as "active" for a bounded dwell time, records
/// the maximum simultaneously-active count, and flags over-admission. The
/// wait-until-full handshake PROVES the expected parallelism materialized
/// (the call only returns once `budget` members are simultaneously active),
/// while a single-threaded budget cannot deadlock (its own entry satisfies
/// the condition immediately).
struct ConcurrencyProbe
{
  std::mutex mutex;
  std::condition_variable cv;
  int active = 0;
  int maxActive = 0;
  int overAdmitted = 0;
  int budget = 1;

  void enter()
  {
    std::unique_lock<std::mutex> lock( mutex );
    ++active;
    maxActive = std::max( maxActive, active );
    if ( active > budget )
      ++overAdmitted;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 20 );
    cv.wait_for( lock, deadline - std::chrono::steady_clock::now(),
                 [ this ] { return active >= budget; } );
    // Hold the slot briefly so overlapping admissions are observable.
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }

  void leave()
  {
    std::unique_lock<std::mutex> lock( mutex );
    --active;
    cv.notify_all();
  }
};

ConcurrencyProbe &probe()
{
  static ConcurrencyProbe instance;
  return instance;
}

/// Scale runtime that reports every forward pass to the concurrency probe.
class ProbingScaleRuntime final : public IModelRuntime
{
  public:
    ProbingScaleRuntime( std::string artifact, std::string framework, double scale )
        : m_artifact( std::move( artifact ) ), m_framework( std::move( framework ) ),
          m_scale( scale ) {}

    std::string framework() const override { return m_framework; }
    std::string backendName() const override { return "scale_backend"; }
    std::string deviceName() const override { return "cpu"; }
    std::string artifactPath() const override { return m_artifact; }

    cv::Mat infer( const cv::Mat &blob ) override
    {
      probe().enter();
      struct Leave
      {
        ~Leave() { probe().leave(); }
      } leave{};
      cv::Mat out = blob.clone();
      out.convertTo( out, CV_32F, m_scale );
      return out;
    }

  private:
    std::string m_artifact;
    std::string m_framework;
    double m_scale;
};

struct ScaleProviderGuard
{
  ScaleProviderGuard( const std::string &framework, double scale, bool probing )
  {
    ModelRuntimeRegistry::instance().registerProvider(
      framework,
      [ framework, scale, probing ]( const ModelInfo &model,
                                     const ModelHardwareCapabilities &,
                                     std::string *error ) -> ModelRuntimePtr {
        if ( model.resolvedArtifactPath.empty() )
        {
          if ( error )
            *error = "no resolved artifact";
          return nullptr;
        }
        if ( probing )
          return std::make_shared<ProbingScaleRuntime>( model.resolvedArtifactPath, framework,
                                                        scale );
        return std::make_shared<ScaleRuntime>( model.resolvedArtifactPath, framework, scale );
      } );
  }
};

struct RegistryReset
{
  RegistryReset()
  {
    ModelRuntimeRegistry::instance().releaseAll();
    ModelRuntimeRegistry::instance().resetLoadCount();
    ModelRuntimeRegistry::instance().setMaxCachedSessions( 16 );
    ModelRuntimeRegistry::instance().setIdleEvictionMs( 0 );
  }
  ~RegistryReset() { ModelRuntimeRegistry::instance().releaseAll(); }
};

void registerMember( const std::string &name, const std::string &framework,
                     const QTemporaryDir &dir, int tileSize = 0 )
{
  Json::Value json( Json::objectValue );
  json["name"] = name;
  json["task"] = "segmentation";
  json["framework"] = framework;
  json["artifact"]["path"] = identityModelPath().toStdString();
  if ( tileSize > 0 )
    json["tiling"]["tile_size"] = tileSize;
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QString::fromStdString( name ) + QStringLiteral( "/model.json" ) ).toStdString(),
    &error );
  if ( !ok )
    FAIL( "member '" + name + "' rejected: " + error );
}

/// Registers an N-member ensemble over member-a..member-N with per-member
/// weights (member i gets scale (i+1) so each member's product differs).
void registerEnsemble( const QTemporaryDir &dir, const std::string &ensembleName,
                       int memberCount, int maxConcurrentMembers,
                       const std::string &uncertainty = std::string(),
                       const std::vector<double> &weights = {} )
{
  Json::Value json( Json::objectValue );
  json["name"] = ensembleName;
  json["task"] = "segmentation";
  json["framework"] = "onnx";
  Json::Value &ensemble = json["ensemble"] = Json::Value( Json::objectValue );
  Json::Value members( Json::arrayValue );
  for ( int i = 0; i < memberCount; ++i )
  {
    Json::Value entry( Json::objectValue );
    entry["model"] = "member-" + std::string( 1, static_cast<char>( 'a' + i ) );
    entry["weight"] = i < static_cast<int>( weights.size() ) ? weights[static_cast<std::size_t>( i )]
                                                            : 1.0;
    members.append( entry );
  }
  ensemble["members"] = members;
  if ( maxConcurrentMembers > 0 )
    ensemble["max_concurrent_members"] = maxConcurrentMembers;
  if ( !uncertainty.empty() )
    ensemble["uncertainty"] = uncertainty;
  std::string error;
  const bool ok = ModelCatalog::instance().registerManifestJson(
    Json::writeString( Json::StreamWriterBuilder(), json ),
    dir.filePath( QString::fromStdString( ensembleName ) + "/model.json" ).toStdString(), &error );
  if ( !ok )
    FAIL( "ensemble '" + ensembleName + "' rejected: " + error );
}

QString writeConstantRaster( const QTemporaryDir &dir, const QString &name, int size,
                             int bands, float value )
{
  const QString path = dir.filePath( name );
  sicnu::testing::RsSyntheticRasterBuilder builder( size, size, bands, GDT_Float32 );
  for ( int b = 1; b <= bands; ++b )
    builder.withConstantValue( b, value );
  builder.withCrs( QStringLiteral( "EPSG:4326" ) ).writeToDisk( path );
  return path;
}

/// Reads every pixel of every band into a flat vector (NaN preserved).
std::vector<float> readAllPixels( const QString &path, int bands, int width, int height )
{
  std::vector<float> values;
  GDALDataset *ds = GDALDataset::Open( path.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    return values;
  values.reserve( static_cast<std::size_t>( bands ) * width * height );
  for ( int b = 1; b <= bands; ++b )
  {
    std::vector<float> band( static_cast<std::size_t>( width ) * height, 0.0f );
    ds->GetRasterBand( b )->RasterIO( GF_Read, 0, 0, width, height, band.data(), width, height,
                                      GDT_Float32, 0, 0 );
    values.insert( values.end(), band.begin(), band.end() );
  }
  GDALClose( ds );
  return values;
}

float readPixel( const QString &path, int band, int x, int y )
{
  GDALDataset *ds = GDALDataset::Open( path.toUtf8().constData(), GA_ReadOnly );
  if ( !ds )
    return 0.0f;
  float value = 0.0f;
  ds->GetRasterBand( band )->RasterIO( GF_Read, x, y, 1, 1, &value, 1, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  return value;
}

bool sameValues( const std::vector<float> &a, const std::vector<float> &b )
{
  if ( a.size() != b.size() )
    return false;
  for ( std::size_t i = 0; i < a.size(); ++i )
  {
    if ( std::isnan( a[i] ) != std::isnan( b[i] ) )
      return false;
    if ( !std::isnan( a[i] ) && a[i] != b[i] )
      return false;
  }
  return true;
}

} // namespace

TEST_CASE( "serial and bounded-parallel ensemble runs are bit-identical",
           "[models][ensemble][parallel]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "parfw-a", 2.0, false );
  const ScaleProviderGuard guardB( "parfw-b", 0.5, false );
  const ScaleProviderGuard guardC( "parfw-c", 1.5, false );
  const ScaleProviderGuard guardD( "parfw-d", 3.0, false );
  QTemporaryDir dir;
  registerMember( "member-a", "parfw-a", dir );
  registerMember( "member-b", "parfw-b", dir );
  registerMember( "member-c", "parfw-c", dir );
  registerMember( "member-d", "parfw-d", dir );
  registerEnsemble( dir, "ens-serial", 4, 1, "variance" );
  registerEnsemble( dir, "ens-parallel", 4, 4, "variance" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  RSOperatorContext context;

  const QString serialOut = dir.filePath( QStringLiteral( "serial.tif" ) );
  ModelExecutionRequest serialRequest;
  serialRequest.inputPath = input.toStdString();
  serialRequest.outputPath = serialOut.toStdString();
  serialRequest.modelReference = "ens-serial";
  ModelExecutionResult serialResult;
  REQUIRE_NOTHROW( serialResult =
                     sicnu::operators::runtime::runModelInference( serialRequest, context ) );

  const QString parallelOut = dir.filePath( QStringLiteral( "parallel.tif" ) );
  ModelExecutionRequest parallelRequest;
  parallelRequest.inputPath = input.toStdString();
  parallelRequest.outputPath = parallelOut.toStdString();
  parallelRequest.modelReference = "ens-parallel";
  ModelExecutionResult parallelResult;
  REQUIRE_NOTHROW( parallelResult =
                     sicnu::operators::runtime::runModelInference( parallelRequest, context ) );

  // Bit-identical products: 2 class planes + variance band, values
  // (10×2 + 10×0.5 + 10×1.5 + 10×3)/4 = 17.5; the variance band is the mean
  // over classes of the weighted per-class variance: deviations 2.5, 12.5,
  // 2.5, 12.5 → (6.25 + 156.25 + 6.25 + 156.25)/4 = 81.25 (identical in both
  // class planes, so the class mean is the same value).
  const int bands = serialResult.rasterStats.outBands;
  REQUIRE( bands == 3 );
  const std::vector<float> serialPixels =
    readAllPixels( serialOut, bands, 16, 16 );
  const std::vector<float> parallelPixels =
    readAllPixels( parallelOut, bands, 16, 16 );
  CHECK( sameValues( serialPixels, parallelPixels ) );
  CHECK( serialPixels.front() == Catch::Approx( 17.5f ).margin( 1e-4f ) );
  CHECK( serialPixels.back() == Catch::Approx( 81.25f ).margin( 1e-3f ) );

  // Member ordering in the payload is the member index order, never the
  // completion order.
  const Json::Value &serialMembers = serialResult.payload["ensemble_members"];
  const Json::Value &parallelMembers = parallelResult.payload["ensemble_members"];
  REQUIRE( serialMembers.size() == 4 );
  REQUIRE( parallelMembers.size() == 4 );
  for ( Json::ArrayIndex i = 0; i < 4; ++i )
  {
    CHECK( parallelMembers[i]["model"].asString() == serialMembers[i]["model"].asString() );
    CHECK( parallelMembers[i]["weight"].asDouble() == serialMembers[i]["weight"].asDouble() );
  }
}

TEST_CASE( "bounded admission proves the concurrency ceiling",
           "[models][ensemble][parallel][admission]" )
{
  RegistryReset reset;
  // Four members, budget 2: the probe's handshake completes only when two
  // members are simultaneously inside infer(); over-admission (3+) is flagged.
  const ScaleProviderGuard guardA( "admfw-a", 2.0, true );
  const ScaleProviderGuard guardB( "admfw-b", 0.5, true );
  const ScaleProviderGuard guardC( "admfw-c", 1.5, true );
  const ScaleProviderGuard guardD( "admfw-d", 3.0, true );
  QTemporaryDir dir;
  registerMember( "member-a", "admfw-a", dir );
  registerMember( "member-b", "admfw-b", dir );
  registerMember( "member-c", "admfw-c", dir );
  registerMember( "member-d", "admfw-d", dir );
  registerEnsemble( dir, "ens-admit", 4, 2, "none" );

  ConcurrencyProbe &p = probe();
  {
    std::lock_guard<std::mutex> lock( p.mutex );
    p.active = 0;
    p.maxActive = 0;
    p.overAdmitted = 0;
    p.budget = 2;
  }

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "admit.tif" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "ens-admit";
  RSOperatorContext context;
  ModelExecutionResult result;
  REQUIRE_NOTHROW( result = sicnu::operators::runtime::runModelInference( request, context ) );

  std::lock_guard<std::mutex> lock( p.mutex );
  CHECK( p.maxActive == 2 );  // the budget really ran in parallel
  CHECK( p.overAdmitted == 0 ); // never more than the budget
}

TEST_CASE( "8 concurrent ensemble runs conserve the VRAM ledger and leave no residue",
           "[models][ensemble][parallel][stress]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "strfw-a", 2.0, false );
  const ScaleProviderGuard guardB( "strfw-b", 0.5, false );
  QTemporaryDir dir;
  registerMember( "member-a", "strfw-a", dir );
  registerMember( "member-b", "strfw-b", dir );
  registerEnsemble( dir, "ens-stress", 2, 2, "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );

  constexpr int kThreads = 8;
  std::atomic<int> completed{ 0 };
  std::atomic<int> cancelled{ 0 };
  std::atomic<bool> failed{ false };
  std::vector<std::string> failures;
  std::mutex failuresMutex;
  std::vector<std::thread> threads;
  for ( int t = 0; t < kThreads; ++t )
  {
    threads.emplace_back( [ &, t ]() {
      const QString output = dir.filePath( QStringLiteral( "stress-%1.tif" ).arg( t ) );
      ModelExecutionRequest request;
      request.inputPath = input.toStdString();
      request.outputPath = output.toStdString();
      request.modelReference = "ens-stress";
      RSOperatorContext context;
      std::atomic<bool> cancelFlag{ t % 2 == 1 };
      if ( t % 2 == 1 )
        context.setCancelFlag( &cancelFlag );
      try
      {
        sicnu::operators::runtime::runModelInference( request, context );
        completed.fetch_add( 1 );
      }
      catch ( const RSOperatorError &error )
      {
        if ( error.code() == sicnu::operators::ErrorCode::Cancelled )
          cancelled.fetch_add( 1 );
        else
        {
          failed.store( true );
          std::lock_guard<std::mutex> lock( failuresMutex );
          failures.push_back( error.message() );
        }
      }
      catch ( const std::exception &error )
      {
        failed.store( true );
        std::lock_guard<std::mutex> lock( failuresMutex );
        failures.push_back( error.what() );
      }
    } );
  }
  for ( std::thread &thread : threads )
    thread.join();

  CHECK_FALSE( failed.load() );
  for ( const std::string &failure : failures )
    WARN( "unexpected failure: " + failure );
  CHECK( completed.load() == kThreads / 2 );
  CHECK( cancelled.load() == kThreads / 2 );

  // Every completed product is valid; every cancelled run left nothing.
  for ( int t = 0; t < kThreads; ++t )
  {
    const QString output = dir.filePath( QStringLiteral( "stress-%1.tif" ).arg( t ) );
    if ( t % 2 == 0 )
    {
      CHECK( QFile::exists( output ) );
      const std::vector<float> pixels = readAllPixels( output, 2, 16, 16 );
      REQUIRE( pixels.size() == 2 * 16 * 16 );
      CHECK( pixels.front() == Catch::Approx( 12.5f ).margin( 1e-4f ) );
    }
    else
    {
      CHECK_FALSE( QFile::exists( output ) );
      CHECK_FALSE( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
    }
    CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
  }

  // Ledger conservation: every session reservation returned (CPU fake
  // runtimes reserve nothing; the invariant is "nothing leaked").
  ModelRuntimeRegistry::instance().releaseAll();
  const auto devices = ModelRuntimeRegistry::instance().deviceReport();
  for ( const auto &device : devices )
    CHECK( device.reservedMb == 0 );

  // Zero staged residue across the whole output directory.
  const QStringList residue =
    QDir( dir.path() ).entryList( { "*.tmp~*", ".*.tmp~*", "*.prev~*", "*.prov.json.stage~" },
                                  QDir::Files | QDir::Hidden );
  for ( const QString &entry : residue )
    WARN( "residue: " + entry.toStdString() );
  CHECK( residue.isEmpty() );
}

TEST_CASE( "one member failure stops the run with zero residue",
           "[models][ensemble][parallel][failure]" )
{
  RegistryReset reset;
  // Member B waits until member A has COMPLETED its forward pass, then
  // throws: the sibling's staged artifacts therefore certainly existed, so
  // the zero-residue assertion proves cleanup (not "never created").
  std::atomic<int> aForwards{ 0 };
  ModelRuntimeRegistry::instance().registerProvider(
    "failfw-b",
    [ &aForwards ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                    std::string * ) -> ModelRuntimePtr {
      const std::string artifact = model.resolvedArtifactPath;
      struct Throwing final : IModelRuntime
      {
        Throwing( std::string artifact, std::atomic<int> *aForwards )
            : m_artifact( std::move( artifact ) ), m_aForwards( aForwards ) {}
        cv::Mat infer( const cv::Mat & ) override
        {
          while ( m_aForwards->load() == 0 )
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
          throw std::runtime_error( "injected member failure (out of memory)" );
        }
        std::string framework() const override { return "failfw-b"; }
        std::string backendName() const override { return "throwing"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return m_artifact; }
        std::string m_artifact;
        std::atomic<int> *m_aForwards;
      };
      return std::make_shared<Throwing>( artifact, &aForwards );
    } );
  // Member A counts its forward passes so the test can prove it ran.
  ModelRuntimeRegistry::instance().registerProvider(
    "failfw-a",
    [ &aForwards ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                    std::string *error ) -> ModelRuntimePtr {
      if ( model.resolvedArtifactPath.empty() )
      {
        if ( error )
          *error = "no resolved artifact";
        return nullptr;
      }
      struct Counting final : IModelRuntime
      {
        Counting( std::string artifact, std::atomic<int> *aForwards )
            : m_artifact( std::move( artifact ) ), m_aForwards( aForwards ) {}
        cv::Mat infer( const cv::Mat &blob ) override
        {
          m_aForwards->fetch_add( 1 );
          cv::Mat out = blob.clone();
          out.convertTo( out, CV_32F, 2.0 );
          return out;
        }
        std::string framework() const override { return "failfw-a"; }
        std::string backendName() const override { return "scale_backend"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return m_artifact; }
        std::string m_artifact;
        std::atomic<int> *m_aForwards;
      };
      return std::make_shared<Counting>( model.resolvedArtifactPath, &aForwards );
    } );
  QTemporaryDir dir;
  registerMember( "member-a", "failfw-a", dir );
  registerMember( "member-b", "failfw-b", dir );
  registerEnsemble( dir, "ens-fail", 2, 2, "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "fail.tif" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "ens-fail";
  RSOperatorContext context;
  REQUIRE_THROWS_WITH( sicnu::operators::runtime::runModelInference( request, context ),
                       Catch::Matchers::ContainsSubstring( "injected member failure" ) );
  // The sibling really ran (so its stack + sidecar existed) …
  CHECK( aForwards.load() >= 1 );

  CHECK_FALSE( QFile::exists( output ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
  for ( int member = 0; member < 2; ++member )
  {
    const QString stack =
      dir.filePath( QStringLiteral( ".fail.tif.ensemble-member%1.tmp~" ).arg( member ) );
    CHECK_FALSE( QFile::exists( stack ) );
    CHECK_FALSE( QFile::exists( stack + QStringLiteral( ".prov.json" ) ) );
    CHECK_FALSE( QFile::exists( stack + QStringLiteral( ".tmp~" ) ) );
  }
}

TEST_CASE( "mid-run cancellation leaves zero residue",
           "[models][ensemble][parallel][cancel]" )
{
  RegistryReset reset;
  // 64x64 with 16px tiles: 16 tiles per member. Member A signals on its FIRST
  // forward pass; the main thread then cancels — so the abort provably lands
  // INSIDE a running member (its next tile boundary), not at worker entry.
  std::atomic<int> forwards{ 0 };
  ModelRuntimeRegistry::instance().registerProvider(
    "cancfw-a",
    [ &forwards ]( const ModelInfo &model, const ModelHardwareCapabilities &,
                   std::string *error ) -> ModelRuntimePtr {
      if ( model.resolvedArtifactPath.empty() )
      {
        if ( error )
          *error = "no resolved artifact";
        return nullptr;
      }
      struct Counting final : IModelRuntime
      {
        Counting( std::string artifact, std::atomic<int> *forwards )
            : m_artifact( std::move( artifact ) ), m_forwards( forwards ) {}
        cv::Mat infer( const cv::Mat &blob ) override
        {
          m_forwards->fetch_add( 1 );
          cv::Mat out = blob.clone();
          out.convertTo( out, CV_32F, 2.0 );
          return out;
        }
        std::string framework() const override { return "cancfw-a"; }
        std::string backendName() const override { return "scale_backend"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return m_artifact; }
        std::string m_artifact;
        std::atomic<int> *m_forwards;
      };
      return std::make_shared<Counting>( model.resolvedArtifactPath, &forwards );
    } );
  const ScaleProviderGuard guardB( "cancfw-b", 0.5, false );
  QTemporaryDir dir;
  registerMember( "member-a", "cancfw-a", dir, 16 ); // 16 tiles over 64x64
  registerMember( "member-b", "cancfw-b", dir, 16 );
  registerEnsemble( dir, "ens-cancel", 2, 2, "none" );

  const QString input = writeConstantRaster( dir, "input.tif", 64, 2, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "cancel.tif" ) );
  ModelExecutionRequest request;
  request.inputPath = input.toStdString();
  request.outputPath = output.toStdString();
  request.modelReference = "ens-cancel";
  RSOperatorContext context;
  std::atomic<bool> cancelFlag{ false };
  context.setCancelFlag( &cancelFlag );

  // Cancel from a watchdog once the first forward pass has happened.
  std::thread watchdog( [ & ]() {
    while ( forwards.load() == 0 )
      std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    cancelFlag.store( true );
  } );
  bool cancelled = false;
  try
  {
    sicnu::operators::runtime::runModelInference( request, context );
  }
  catch ( const RSOperatorError &error )
  {
    cancelled = error.code() == sicnu::operators::ErrorCode::Cancelled;
    if ( !cancelled )
      WARN( "unexpected error: " + error.message() );
  }
  watchdog.join();
  CHECK( cancelled );
  CHECK( forwards.load() >= 1 ); // the abort was mid-member, not pre-run

  CHECK_FALSE( QFile::exists( output ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
  CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
  for ( int member = 0; member < 2; ++member )
  {
    const QString stack =
      dir.filePath( QStringLiteral( ".cancel.tif.ensemble-member%1.tmp~" ).arg( member ) );
    CHECK_FALSE( QFile::exists( stack ) );
    CHECK_FALSE( QFile::exists( stack + QStringLiteral( ".prov.json" ) ) );
    CHECK_FALSE( QFile::exists( stack + QStringLiteral( ".tmp~" ) ) );
  }
}

TEST_CASE( "a sidecar failure restores the previous product instead of destroying it",
           "[models][ensemble][parallel][publish]" )
{
  RegistryReset reset;
  const ScaleProviderGuard guardA( "rollfw-a", 2.0, false );
  const ScaleProviderGuard guardB( "rollfw-b", 0.5, false );
  QTemporaryDir dir;
  registerMember( "member-a", "rollfw-a", dir );
  registerMember( "member-b", "rollfw-b", dir );
  registerEnsemble( dir, "ens-roll-a", 2, 2, "none", { 1.0, 1.0 } );
  // Weights 3:1 would produce (20·3 + 5)/4 = 16.25 — a DIFFERENT product, so a
  // successful second run is distinguishable from the rolled-back first one.
  registerEnsemble( dir, "ens-roll-b", 2, 2, "none", { 3.0, 1.0 } );

  const QString input = writeConstantRaster( dir, "input.tif", 16, 1, 10.0f );
  const QString output = dir.filePath( QStringLiteral( "roll.tif" ) );
  RSOperatorContext context;

  // First run publishes the product + sidecar (mean of 20 and 5 = 12.5).
  ModelExecutionRequest first;
  first.inputPath = input.toStdString();
  first.outputPath = output.toStdString();
  first.modelReference = "ens-roll-a";
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( first, context ) );
  REQUIRE( QFile::exists( output ) );
  REQUIRE( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
  const float firstValue = readPixel( output, 1, 4, 4 );
  CHECK( firstValue == Catch::Approx( 12.5f ).margin( 1e-4f ) );

  // Second run with the sidecar publish faulted: the new product is rolled
  // back and the PREVIOUS product survives (12.5, not the new 16.25 the
  // weights would have produced — the same manifest, different weights).
  {
    sicnu::runtime::observability::fault::ArmedFault fault(
      { "ensemble.publish_sidecar", sicnu::runtime::observability::fault::Mode::NextN, 1, "" } );
    ModelExecutionRequest second;
    second.inputPath = input.toStdString();
    second.outputPath = output.toStdString();
    second.modelReference = "ens-roll-b";
    REQUIRE_THROWS( sicnu::operators::runtime::runModelInference( second, context ) );

    // The previous product is intact …
    CHECK( QFile::exists( output ) );
    CHECK( readPixel( output, 1, 4, 4 ) == firstValue );
    // … and (hardening 15/20) the rolled-back product keeps ITS OWN sidecar:
    // the previous verified product+sidecar pair is restored intact instead
    // of downgrading a verified product to MissingSidecar. The parked sidecar
    // is the FIRST run's (ens-roll-a), so consumer-side verification passes.
    CHECK( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
    const auto restoredVerdict =
      sicnu::operators::runtime::verifyProductAgainstModel( output.toStdString(), "ens-roll-a" );
    CHECK( restoredVerdict.state == sicnu::operators::runtime::ProvenanceVerdict::State::Ok );
    // … and nothing else leaked.
    CHECK_FALSE( QFile::exists( output + QStringLiteral( ".tmp~" ) ) );
    CHECK_FALSE( QFile::exists( output + QStringLiteral( ".prev~" ) ) );
    for ( int member = 0; member < 2; ++member )
    {
      const QString stack =
        dir.filePath( QStringLiteral( ".roll.tif.ensemble-member%1.tmp~" ).arg( member ) );
      CHECK_FALSE( QFile::exists( stack ) );
      CHECK_FALSE( QFile::exists( stack + QStringLiteral( ".prov.json" ) ) );
      CHECK_FALSE( QFile::exists( stack + QStringLiteral( ".tmp~" ) ) );
    }
  }

  // Disarmed: a third run succeeds and republishes both product and sidecar.
  ModelExecutionRequest third;
  third.inputPath = input.toStdString();
  third.outputPath = output.toStdString();
  third.modelReference = "ens-roll-a";
  REQUIRE_NOTHROW( sicnu::operators::runtime::runModelInference( third, context ) );
  CHECK( QFile::exists( output + QStringLiteral( ".prov.json" ) ) );
}
