#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <opencv2/core.hpp>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"
#include "rs_classifier_normalbayes.h"
#include "rs_cross_validation.h"
#include "rs_post_process_task.h"

#include <qgstaskmanager.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

std::atomic_int gGdalFailureCount = 0;

void makeGaussianData( cv::Mat &features, cv::Mat &labels, int perClass = 200,
                       int seed = 42 )
{
  cv::RNG rng( seed );
  const int total = perClass * 3;
  features.create( total, 2, CV_32F );
  labels.create( total, 1, CV_32S );
  for ( int i = 0; i < perClass; ++i )
  {
    features.at<float>( i, 0 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    features.at<float>( i, 1 ) = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    labels.at<int>( i, 0 ) = 1;
  }
  for ( int i = 0; i < perClass; ++i )
  {
    features.at<float>( perClass + i, 0 )
      = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    features.at<float>( perClass + i, 1 )
      = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    labels.at<int>( perClass + i, 0 ) = 2;
  }
  for ( int i = 0; i < perClass; ++i )
  {
    features.at<float>( 2 * perClass + i, 0 )
      = static_cast<float>( rng.gaussian( 2.0 ) ) + 5.0f;
    features.at<float>( 2 * perClass + i, 1 )
      = static_cast<float>( rng.gaussian( 2.0 ) ) + 20.0f;
    labels.at<int>( 2 * perClass + i, 0 ) = 3;
  }
}

void CPL_STDCALL countGdalFailures( CPLErr errorClass, CPLErrorNum errorNumber,
                                    const char *message )
{
  if ( errorClass >= CE_Failure )
    gGdalFailureCount.fetch_add( 1 );
  CPLDefaultErrorHandler( errorClass, errorNumber, message );
}

class BlockingPostProcessTask final : public RsPostProcessTask
{
  public:
    BlockingPostProcessTask( std::atomic_bool &started, std::atomic_bool &cancelled,
                             std::atomic_bool &release )
      : RsPostProcessTask( RsPostProcessConfig{} )
      , mStarted( started )
      , mCancelled( cancelled )
      , mRelease( release )
    {
    }

    bool run() override
    {
      mStarted.store( true );
      while ( !mRelease.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
      return true;
    }

    void cancel() override
    {
      mCancelled.store( true );
      RsPostProcessTask::cancel();
    }

  private:
    std::atomic_bool &mStarted;
    std::atomic_bool &mCancelled;
    std::atomic_bool &mRelease;
};

/// Local stand-in for the deleted cross-validation task adapter (ADR 0053):
/// these tests exercise Task Center submit/cancel/terminal-status
/// integration, not the wrapper itself, so a minimal QgsTask that runs
/// RsCrossValidation::kFold with the same progress/cancel behavior is enough.
class TestCvTask : public QgsTask
{
  public:
    using ClassifierFactory = std::function<std::unique_ptr<RsClassifierBackend>()>;

    TestCvTask( const cv::Mat &X, const cv::Mat &y,
                ClassifierFactory factory, int k = 5,
                const QString &description = QStringLiteral( "Cross Validation" ) )
      : QgsTask( description, QgsTask::CanCancel )
      , mX( X.clone() )
      , mY( y.clone() )
      , mFactory( std::move( factory ) )
      , mK( k )
    {
    }

    bool run() override
    {
      setProgress( 5 );

      if ( isCanceled() )
        return false;

      mResult = RsCrossValidation::kFold(
        mX, mY, mFactory, mK,
        /*scaleFeatures=*/true,
        [this]() { return isCanceled(); } );

      setProgress( 95 );

      if ( isCanceled() || mResult.errorMessage == QStringLiteral( "Cancelled" ) )
        return false;

      setProgress( 100 );

      return mResult.ok();
    }

    const RsCrossValidation::Result &result() const { return mResult; }

  private:
    cv::Mat mX;
    cv::Mat mY;
    ClassifierFactory mFactory;
    int mK;
    RsCrossValidation::Result mResult;
};

class BlockingCrossValidationTask final : public TestCvTask
{
  public:
    BlockingCrossValidationTask( std::atomic_bool &started, std::atomic_bool &cancelled,
                                 std::atomic_bool &release )
      : TestCvTask( cv::Mat{}, cv::Mat{}, [] { return std::unique_ptr<RsClassifierBackend>{}; } )
      , mStarted( started )
      , mCancelled( cancelled )
      , mRelease( release )
    {
    }

    bool run() override
    {
      mStarted.store( true );
      while ( !mRelease.load() )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
      return true;
    }

    void cancel() override
    {
      mCancelled.store( true );
      QgsTask::cancel();
    }

  private:
    std::atomic_bool &mStarted;
    std::atomic_bool &mCancelled;
    std::atomic_bool &mRelease;
};

RsPostProcessConfig validPostProcessConfig( const QTemporaryDir &tmp )
{
  GDALAllRegister();
  const QString inputPath = tmp.path() + QStringLiteral( "/labels.tif" );
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    throw std::runtime_error( "GTiff driver is unavailable" );

  GDALDataset *dataset = driver->Create( inputPath.toUtf8().constData(),
                                         32, 32, 1, GDT_Int32, nullptr );
  if ( !dataset )
    throw std::runtime_error( "Could not create class-ID raster" );

  std::vector<int> labels( 32 * 32, 1 );
  const CPLErr result = dataset->GetRasterBand( 1 )->RasterIO(
    GF_Write, 0, 0, 32, 32, labels.data(), 32, 32, GDT_Int32, 0, 0 );
  GDALClose( dataset );
  if ( result != CE_None )
    throw std::runtime_error( "Could not write class-ID raster" );

  RsPostProcessConfig config;
  config.inputPath = inputPath;
  config.outputRasterPath = tmp.path() + QStringLiteral( "/post-process.tif" );
  config.runSieve = false;
  config.runMajority = false;
  config.runClump = false;
  config.runRecode = false;
  config.runPolygonize = false;
  config.creationOptions.clear();
  return config;
}

sicnu::AlgorithmTaskInfo waitForTerminalTask( long taskId )
{
  sicnu::AlgorithmTaskInfo info;
  for ( int attempt = 0; attempt < 1000; ++attempt )
  {
    info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
      return info;
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
  }
  return info;
}

long submitPostProcess( std::unique_ptr<RsPostProcessTask> &task )
{
  sicnu::jobs::JobRequest request;
  request.algorithmId = "module:classify:postprocess";
  request.source = "test";

  return sicnu::TaskCenter::instance().submitJob(
    request,
    [worker = task.get()]( const sicnu::jobs::JobRequest &,
                           sicnu::operators::RSOperatorContext &ctx ) {
      const bool ok = worker->run();
      if ( ctx.isCancelled()
           || ( !ok && worker->result().errorMessage == QStringLiteral( "Cancelled" ) ) )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      if ( !ok )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          worker->result().errorMessage.toStdString() );
      }
      Json::Value payload( Json::objectValue );
      payload["output"] = worker->config().outputRasterPath.toStdString();
      payload["durationMs"] = worker->result().durationMs;
      return payload;
    },
    [worker = task.get()] { worker->cancel(); } );
}

long submitCrossValidation( std::unique_ptr<TestCvTask> &task )
{
  sicnu::jobs::JobRequest request;
  request.algorithmId = "module:classify:cv";
  request.source = "test";

  return sicnu::TaskCenter::instance().submitJob(
    request,
    [worker = task.get()]( const sicnu::jobs::JobRequest &,
                           sicnu::operators::RSOperatorContext &ctx ) {
      const bool ok = worker->run();
      if ( ctx.isCancelled()
           || ( !ok && worker->result().errorMessage == QStringLiteral( "Cancelled" ) ) )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::Cancelled, "Cancelled" );
      }
      if ( !ok )
      {
        throw sicnu::operators::RSOperatorError(
          sicnu::operators::ErrorCode::ComputationError,
          worker->result().errorMessage.isEmpty()
            ? "Cross validation failed"
            : worker->result().errorMessage.toStdString() );
      }
      Json::Value payload( Json::objectValue );
      payload["meanAccuracy"] = worker->result().meanAccuracy;
      payload["stdAccuracy"] = worker->result().stdAccuracy;
      return payload;
    },
    [worker = task.get()] { worker->cancel(); } );
}

} // namespace

TEST_CASE( "Classification Task Center completes post-processing workers", "[classify][post]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  auto worker = std::make_unique<RsPostProcessTask>( validPostProcessConfig( tmp ) );
  gGdalFailureCount.store( 0 );
  const CPLErrorHandler previousHandler = CPLSetErrorHandler( countGdalFailures );
  const auto info = waitForTerminalTask( submitPostProcess( worker ) );
  CPLSetErrorHandler( previousHandler );

  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  REQUIRE( gGdalFailureCount.load() == 0 );
  REQUIRE( info.resultPayload["output"].asString()
           == worker->config().outputRasterPath.toStdString() );
  REQUIRE( QFile::exists( worker->config().outputRasterPath ) );
}

TEST_CASE( "Classification Task Center reports post-processing failures", "[classify][post]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );

  auto cfg = validPostProcessConfig( tmp );
  cfg.inputPath.clear();
  auto worker = std::make_unique<RsPostProcessTask>( std::move( cfg ) );

  const auto failed = waitForTerminalTask( submitPostProcess( worker ) );

  REQUIRE( failed.status == sicnu::TaskStatus::Failed );
  REQUIRE( failed.errorMessage.contains( QStringLiteral( "Empty input path" ) ) );
}

TEST_CASE( "Classification Task Center keeps cancellation running until its worker exits", "[classify][cancel]" )
{
  sicnu::jobs::JobEngine::instance().shutdownForTests();
  std::atomic_bool started = false;
  std::atomic_bool workerCancelled = false;
  std::atomic_bool releaseWorker = false;

  std::unique_ptr<RsPostProcessTask> worker = std::make_unique<BlockingPostProcessTask>(
    started, workerCancelled, releaseWorker );
  const long taskId = submitPostProcess( worker );

  for ( int attempt = 0; attempt < 100 && !started.load(); ++attempt )
    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
  REQUIRE( started.load() );
  REQUIRE( sicnu::TaskCenter::instance().cancelTask( taskId ) );
  REQUIRE( workerCancelled.load() );
  // Aligned with the cascade-cancel semantics (#604): cancelTask moves a
  // running task to Cancelling (not Running) until its worker exits.
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status
           == sicnu::TaskStatus::Cancelling );
  releaseWorker.store( true );
  REQUIRE( waitForTerminalTask( taskId ).status == sicnu::TaskStatus::Canceled );
}

TEST_CASE( "Classification Task Center keeps cross-validation cancellation running until its worker exits", "[classify][cv][cancel]" )
{
  sicnu::jobs::JobEngine::instance().shutdownForTests();
  std::atomic_bool started = false;
  std::atomic_bool workerCancelled = false;
  std::atomic_bool releaseWorker = false;

  std::unique_ptr<TestCvTask> worker = std::make_unique<BlockingCrossValidationTask>(
    started, workerCancelled, releaseWorker );
  const long taskId = submitCrossValidation( worker );

  for ( int attempt = 0; attempt < 100 && !started.load(); ++attempt )
    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
  REQUIRE( started.load() );
  REQUIRE( sicnu::TaskCenter::instance().cancelTask( taskId ) );
  REQUIRE( workerCancelled.load() );
  // Aligned with the cascade-cancel semantics (#604): cancelTask moves a
  // running task to Cancelling (not Running) until its worker exits.
  REQUIRE( sicnu::TaskCenter::instance().getTaskInfo( taskId ).status
           == sicnu::TaskStatus::Cancelling );
  releaseWorker.store( true );
  REQUIRE( waitForTerminalTask( taskId ).status == sicnu::TaskStatus::Canceled );
}

TEST_CASE( "Classification Task Center completes cross-validation workers", "[classify][cv]" )
{
  cv::Mat features;
  cv::Mat labels;
  makeGaussianData( features, labels );
  auto worker = std::make_unique<TestCvTask>(
    features, labels,
    []() -> std::unique_ptr<RsClassifierBackend> {
      return std::make_unique<RsClassifierNormalBayes>();
    } );

  const auto completed = waitForTerminalTask( submitCrossValidation( worker ) );

  REQUIRE( completed.status == sicnu::TaskStatus::Completed );
  REQUIRE( completed.resultPayload.isMember( "meanAccuracy" ) );
  REQUIRE( completed.resultPayload["meanAccuracy"].asDouble() > 0.85 );
  REQUIRE( completed.resultPayload.isMember( "stdAccuracy" ) );
}

TEST_CASE( "Classification Task Center reports cross-validation failures", "[classify][cv]" )
{
  cv::Mat features;
  cv::Mat labels;
  auto worker = std::make_unique<TestCvTask>(
    features, labels,
    []() -> std::unique_ptr<RsClassifierBackend> {
      return std::make_unique<RsClassifierNormalBayes>();
    } );

  const auto failed = waitForTerminalTask( submitCrossValidation( worker ) );

  REQUIRE( failed.status == sicnu::TaskStatus::Failed );
  REQUIRE_FALSE( failed.errorMessage.isEmpty() );
}
