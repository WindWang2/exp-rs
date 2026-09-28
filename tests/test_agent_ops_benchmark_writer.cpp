// test_agent_ops_benchmark_writer.cpp — the agent_ops ExperimentStore sink is
// a deliberate fail-closed boundary (#1360): agentbench persist documents
// carry no geospatial evaluation protocol, so persisting them through
// ExperimentStore::saveBenchmarkResult would write rows the store's own read
// gate can never parse back. The sink must refuse with a typed reason and
// leave the store untouched.
#include <catch2/catch_test_macros.hpp>

#include "agent_ops/benchmark_adapter.h"
#include "app/agent_ops/benchmark_store_writer.h"
#include "experiment/benchmark_runner.h"
#include "experiment/experiment_store.h"

#include <QTemporaryDir>

using namespace sicnu::agent_ops;
using namespace sicnu::app::agent_ops;
using namespace sicnu::experiment;

namespace
{
BenchmarkPersistDocument makePersistDocument()
{
    BenchmarkPersistDocument doc;
    doc.suiteId = "suite-a";
    doc.suiteVersion = "1";
    doc.packDigest = "pack-digest";
    doc.resultId = "agentbench-result-1";
    doc.status = "completed";
    return doc;
}
} // namespace

TEST_CASE( "ExperimentStoreBenchmarkSink refuses protocol-less agentbench results",
           "[agent_ops][benchmark][r5]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp-agentops.sqlite" ) ) ) );

    ExperimentStoreBenchmarkSink sink( &store );
    std::string error;
    CHECK_FALSE( sink.save( makePersistDocument(), &error ) );
    // The refusal is typed and actionable, not a generic failure.
    CHECK( error.find( "evaluation protocol" ) != std::string::npos );
    CHECK( error.find( "#1360" ) != std::string::npos );

    // Nothing landed: the store has no row for the refused result id.
    CHECK( !store.benchmarkResultById( QStringLiteral( "agentbench-result-1" ) )
                .has_value() );
}

TEST_CASE( "the sink gate is at the producer, not the store",
           "[agent_ops][benchmark][r5]" )
{
    // Sanity anchor: the same store accepts a valid benchmark result — the
    // refusal above comes from the missing protocol, not from a store that
    // refuses everything.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp-agentops-2.sqlite" ) ) ) );

    BenchmarkResult result;
    result.setResultId( QStringLiteral( "br-valid" ) );
    result.setBenchmarkId( QStringLiteral( "bench-v" ) );
    EvaluationProtocol protocol;
    protocol.setDatasetVersionId( QStringLiteral( "dv-1" ) );
    protocol.setSplitManifestId( QStringLiteral( "sp-1" ) );
    result.setProtocol( protocol );
    REQUIRE( store.saveBenchmarkResult( result ).has_value() );
    CHECK( store.benchmarkResultById( QStringLiteral( "br-valid" ) ).has_value() );
}
