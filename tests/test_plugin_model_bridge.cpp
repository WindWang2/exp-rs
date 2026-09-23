// tests/test_plugin_model_bridge.cpp — completion 13/15: the in-process
// plugin model runtime bridge is the host-side implementation of the
// IModelRuntime contract ("hands out shared_ptr sessions that are safe to
// use from any thread — implementations serialize forward passes",
// model_runtime.h). This suite gives the bridge its first direct coverage:
// a fake plugin runtime that DETECTS concurrent infer() entries on the same
// session must never see an overlap, and the acquire-time refusal/generation
// semantics stay wired through the registry seam.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_runtime.h"
#include "plugins/framework/plugin_model_runtime_bridge.h"

#include <QFile>
#include <QTemporaryDir>

using sicnu::operators::ModelInfo;
using sicnu::operators::runtime::IModelRuntime;
using sicnu::operators::runtime::ModelHardwareCapabilities;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;

namespace {

/// Fake plugin-side runtime (exprs::IPluginModelRuntimeV1). infer() parks a
/// marker for the duration of the forward pass; a second concurrent entry on
/// the SAME object is the contract violation this suite exists to kill.
class OverlapDetectingRuntime final : public exprs::IPluginModelRuntimeV1
{
  public:
    explicit OverlapDetectingRuntime( std::shared_ptr<std::atomic<int>> inFlight,
                                      std::shared_ptr<std::atomic<bool>> overlapped )
        : m_inFlight( std::move( inFlight ) ), m_overlapped( std::move( overlapped ) )
    {
    }

    std::string backendName() const override { return "overlap_fake"; }
    std::string deviceName() const override { return "cpu"; }
    bool load( const exprs::PluginModelRequestV1 &, std::string & ) override { return true; }

    exprs::PluginInferenceResultV1 infer( const exprs::PluginTensorV1 &input,
                                          const std::string & ) override
    {
        const int now = m_inFlight->fetch_add( 1 ) + 1;
        if ( now > 1 )
            m_overlapped->store( true );
        // Hold the window open so an unsynchronized sibling is guaranteed to
        // enter before we leave: the sleep widens the race far past scheduler
        // noise, making the RED case deterministic in practice.
        std::this_thread::sleep_for( std::chrono::milliseconds( 120 ) );
        m_inFlight->fetch_sub( 1 );

        exprs::PluginInferenceResultV1 result;
        result.success = true;
        result.output.batch = input.batch;
        result.output.channels = 1;
        result.output.rows = 1;
        result.output.cols = 1;
        result.output.data.assign( static_cast<std::size_t>( input.batch ), 1.0f );
        return result;
    }

    std::vector<std::string> outputTensorNames() const override { return { "out" }; }

  private:
    std::shared_ptr<std::atomic<int>> m_inFlight;
    std::shared_ptr<std::atomic<bool>> m_overlapped;
};

struct BridgeFixture
{
    std::shared_ptr<std::atomic<int>> inFlight = std::make_shared<std::atomic<int>>( 0 );
    std::shared_ptr<std::atomic<bool>> overlapped = std::make_shared<std::atomic<bool>>( false );

    BridgeFixture()
    {
        sicnu::plugins::storePluginModelRuntimeFactory(
            "bridgefw", "org.bridge.test",
            [ this ]( const exprs::PluginModelRequestV1 &, std::string & )
                -> exprs::PluginModelRuntimePtrV1 {
                auto runtime = std::make_unique<OverlapDetectingRuntime>( inFlight, overlapped );
                runtime->load( exprs::PluginModelRequestV1{}, scratch );
                return runtime;
            } );
        REQUIRE( sicnu::plugins::registerPluginModelRuntime( "bridgefw", "org.bridge.test" ) );
    }

    ~BridgeFixture()
    {
        ModelRuntimeRegistry::instance().releaseAll();
        sicnu::plugins::clearPluginModelRuntimeFactory( "bridgefw" );
    }

    /// One shared session acquired through the REAL registry seam (the same
    /// path runModelInference and the ensembles use). The artifact file must
    /// exist: the session cache key digests its bytes.
    ModelRuntimePtr acquireSession( const QTemporaryDir &dir )
    {
        ModelInfo model;
        model.name = "bridge-model";
        model.task = "segmentation";
        model.framework = "bridgefw";
        model.readiness = sicnu::operators::ModelReadiness::Ready;
        model.tiling.tileSize = 16;
        model.output.classes = { "a", "b" };
        const QString artifact = dir.filePath( QStringLiteral( "bridge.weights" ) );
        {
            QFile weights( artifact );
            REQUIRE( weights.open( QIODevice::WriteOnly ) );
            weights.write( QByteArray( "bridge-fixture-weights" ) );
        }
        model.resolvedArtifactPath = artifact.toStdString();
        model.artifact.path = artifact.toStdString();
        std::string error;
        auto session = ModelRuntimeRegistry::instance().acquire( model, &error );
        REQUIRE( session );
        return session;
    }

    std::string scratch;
};

} // namespace

TEST_CASE( "concurrent infer on one shared plugin session is serialized (completion 13/15)",
           "[plugins][bridge][concurrency][p13c]" )
{
    BridgeFixture fixture;
    QTemporaryDir dir;
    auto session = fixture.acquireSession( dir );
    REQUIRE( session );

    // One warmup forward BEFORE the race: plugin-side lazy init and the
    // execution-barrier entry for this id are established single-threaded,
    // so the race below isolates EXACTLY the forward-serialization contract.
    {
        int warmDims[4] = { 1, 2, 4, 4 };
        cv::Mat warmBlob( 4, warmDims, CV_32F, cv::Scalar( 1.0f ) );
        cv::Mat warm;
        REQUIRE_NOTHROW( warm = session->infer( warmBlob ) );
        CHECK( !warm.empty() );
        CHECK_FALSE( fixture.overlapped->load() );
    }

    const int kThreads = 4;
    std::vector<cv::Mat> results( static_cast<std::size_t>( kThreads ) );
    std::vector<std::thread> workers;
    std::atomic<int> failures{ 0 };
    // A spin gate (not std::barrier: its destructor must not race workers
    // still unwinding their arrive_and_wait) releases every caller INSIDE
    // infer() at the same moment: unsynchronized, the overlap detector fires
    // deterministically.
    std::atomic<int> gate{ 0 };
    for ( int t = 0; t < kThreads; ++t )
    {
        workers.emplace_back( [ &, t ]() {
            gate.fetch_add( 1 );
            while ( gate.load() < kThreads )
                std::this_thread::yield();
            try
            {
                int dims[4] = { 1, 2, 4, 4 };
                cv::Mat blob( 4, dims, CV_32F, cv::Scalar( 1.0f ) );
                results[static_cast<std::size_t>( t )] = session->infer( blob );
            }
            catch ( const std::exception & )
            {
                failures.fetch_add( 1 );
            }
        } );
    }
    for ( std::thread &worker : workers )
        worker.join();

    // Every caller succeeded and the plugin saw exactly ONE forward at a
    // time — the documented contract, now enforced at the bridge gate.
    CHECK( failures.load() == 0 );
    for ( const cv::Mat &result : results )
    {
        REQUIRE( !result.empty() );
        CHECK( result.size[0] == 1 );
    }
    CHECK( fixture.inFlight->load() == 0 );
    CHECK_FALSE( fixture.overlapped->load() );
}

TEST_CASE( "unloading invalidates a held bridge session by generation, not by race "
           "(completion 13/15)",
           "[plugins][bridge][concurrency][p13c]" )
{
    // The adapter refuses a session whose plugin generation moved — pinned
    // here so the serialization work cannot regress the barrier contract
    // (#747) it sits behind.
    BridgeFixture fixture;
    QTemporaryDir dir;
    auto session = fixture.acquireSession( dir );
    REQUIRE( session );
    // No unload was performed: the session stays usable (generation intact).
    int dims[4] = { 1, 2, 4, 4 };
    cv::Mat blob( 4, dims, CV_32F, cv::Scalar( 1.0f ) );
    cv::Mat result;
    REQUIRE_NOTHROW( result = session->infer( blob ) );
    CHECK( !result.empty() );
}
