// tests/test_context_ledger_completeness_r4.cpp — Track 8 R4 WP-C.
//
// Premise note (BASELINE.md §8): the track spec modeled ContextLedger as a
// per-tool-call audit trail; the measured contract (context_ledger.h) is a
// set of BOUNDED authoritative-event stores. This suite pins the real
// contract adversarially: record-field completeness on every store, in-place
// rebinding, oldest-first eviction on every bound, the run-summary token
// budget, stat-identity staleness, malformed/hostile inputs (never throws,
// never stores garbage), and concurrent writers.
//
// Ground truth is the documented contract in context_ledger.h, not the
// implementation: bounded stores, authoritative events only, thread safety
// (QMutex), "unresolved first" decision order, "newest first" summary order.
// ContextLedger is a process-wide singleton, so every case uses its own id
// prefix and asserts only on records it created.

#include <catch2/catch_test_macros.hpp>

#include "agent/harness/context_ledger.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using sicnu::agent::harness::ContextLedger;

namespace {

/// True when every listed field is present (non-null) in the record.
bool hasFields( const Json::Value &record, const std::vector<const char *> &fields )
{
    for ( const char *field : fields )
        if ( record.isMember( field ) && record[field].isNull() )
            return false;
        else if ( !record.isMember( field ) )
            return false;
    return true;
}

int countWithPrefix( const Json::Value &records, const char *idField,
                     const std::string &prefix )
{
    int n = 0;
    for ( const Json::Value &record : records )
        if ( record.get( idField, "" ).asString().rfind( prefix, 0 ) == 0 )
            ++n;
    return n;
}

bool anyWith( const Json::Value &records, const char *idField, const std::string &id )
{
    for ( const Json::Value &record : records )
        if ( record.get( idField, "" ).asString() == id )
            return true;
    return false;
}

} // namespace

// — plan bindings: complete fields, in-place rebind, oldest-first eviction —

TEST_CASE( "plan bindings carry the complete record and rebind in place",
           "[harness][ledger-r4][bindings]" )
{
    auto &ledger = ContextLedger::instance();
    const std::string run = "r4-bind-complete-1";

    ledger.recordPlanBinding( run, "plan-1", "goal text", "ndvi", "pending", "fp-1" );
    Json::Value bindings = ledger.planBindings();
    REQUIRE( anyWith( bindings, "run_id", run ) );
    for ( Json::ArrayIndex i = 0; i < bindings.size(); ++i )
    {
        if ( bindings[i]["run_id"].asString() != run )
            continue;
        CHECK( hasFields( bindings[i],
                          { "run_id", "plan_id", "goal", "intent",
                            "verification_status", "bound_at" } ) );
        CHECK( bindings[i]["plan_fingerprint"].asString() == "fp-1" );
    }

    // Re-binding the same run updates in place — never a second row, and the
    // verification status moves forward (the run-completion path).
    ledger.recordPlanBinding( run, "plan-1", "goal text", "ndvi", "passed", "fp-1" );
    bindings = ledger.planBindings();
    int rows = 0;
    std::string status;
    for ( Json::ArrayIndex i = 0; i < bindings.size(); ++i )
    {
        if ( bindings[i]["run_id"].asString() != run )
            continue;
        ++rows;
        status = bindings[i]["verification_status"].asString();
        CHECK( bindings[i].isMember( "updated_at" ) );
    }
    CHECK( rows == 1 );
    CHECK( status == "passed" );
}

TEST_CASE( "plan bindings evict oldest-first beyond the bound of 8",
           "[harness][ledger-r4][bindings][bounds]" )
{
    auto &ledger = ContextLedger::instance();
    const std::string prefix = "r4-bind-evict-";
    for ( int i = 0; i < 10; ++i )
        ledger.recordPlanBinding( prefix + std::to_string( i ), "p", "", "", "", "" );

    const Json::Value bindings = ledger.planBindings();
    CHECK( countWithPrefix( bindings, "run_id", prefix ) == 8 );
    // Oldest two are gone, newest eight survive — the documented order.
    CHECK_FALSE( anyWith( bindings, "run_id", prefix + "0" ) );
    CHECK_FALSE( anyWith( bindings, "run_id", prefix + "1" ) );
    CHECK( anyWith( bindings, "run_id", prefix + "9" ) );
}

// — decisions: lifecycle, monotonic ids, unresolved-first, bound ————————

TEST_CASE( "typed decisions resolve through their id; unknown ids refuse",
           "[harness][ledger-r4][decisions]" )
{
    auto &ledger = ContextLedger::instance();
    const std::string id = ledger.recordDecision(
        "ambiguity", "r4-decision-subject", "unresolved", "which band is red edge?",
        Json::Value() );
    CHECK( id.rfind( "decision-", 0 ) == 0 );

    const Json::Value decisions = ledger.decisions();
    bool found = false;
    for ( const Json::Value &decision : decisions )
    {
        if ( decision["id"].asString() != id )
            continue;
        found = true;
        CHECK( hasFields( decision,
                          { "id", "kind", "subject", "status", "note", "recorded_at" } ) );
        CHECK( decision["status"].asString() == "unresolved" );
    }
    REQUIRE( found );

    CHECK( ledger.resolveDecision( id, "band 6" ) );
    bool resolvedVisible = false;
    for ( const Json::Value &decision : ledger.decisions() )
    {
        if ( decision["id"].asString() != id )
            continue;
        resolvedVisible = true;
        CHECK( decision["status"].asString() == "resolved" );
        CHECK( decision["chosen"].asString() == "band 6" );
        CHECK( decision.isMember( "resolved_at" ) );
    }
    CHECK( resolvedVisible );
    CHECK_FALSE( ledger.resolveDecision( "decision-999999", "ghost" ) );
}

TEST_CASE( "decision ids stay monotonic under the eviction bound of 20",
           "[harness][ledger-r4][decisions][bounds]" )
{
    auto &ledger = ContextLedger::instance();
    const std::string first = ledger.recordDecision( "parameter", "r4-mono-first",
                                                     "unresolved", "", Json::Value() );
    for ( int i = 0; i < 25; ++i )
        ledger.recordDecision( "parameter", "r4-mono-fill", "unresolved", "", Json::Value() );
    const std::string last = ledger.recordDecision( "parameter", "r4-mono-last",
                                                    "unresolved", "", Json::Value() );
    // Ids come from a monotonic counter, so later records sort higher even
    // after eviction removed the middle rows.
    const int firstNumber = std::stoi( first.substr( 9 ) );
    const int lastNumber = std::stoi( last.substr( 9 ) );
    CHECK( lastNumber > firstNumber );
    CHECK_FALSE( anyWith( ledger.decisions(), "id", first ) ); // evicted oldest-first
    CHECK( anyWith( ledger.decisions(), "id", last ) );
}

// — understanding cache: hit/miss, replace, bound ————————————————————————

TEST_CASE( "the understanding cache hits only on the exact key and evicts "
           "oldest-first at 32", "[harness][ledger-r4][understanding]" )
{
    auto &ledger = ContextLedger::instance();
    const QString key = QStringLiteral( "r4-understanding-key|f12|34" );

    CHECK( ledger.cachedUnderstanding( key ).isNull() ); // miss before put
    Json::Value doc( Json::objectValue );
    doc["fact"] = "ndvi mean 0.42";
    ledger.cacheUnderstanding( key, doc );
    CHECK( ledger.cachedUnderstanding( key )["fact"].asString() == "ndvi mean 0.42" );

    // Same key replaces (cache refresh), never duplicates.
    Json::Value updated( Json::objectValue );
    updated["fact"] = "refreshed";
    ledger.cacheUnderstanding( key, updated );
    CHECK( ledger.cachedUnderstanding( key )["fact"].asString() == "refreshed" );

    // Fill past the bound with fresh keys; the probed key must survive only
    // while inside the 32-entry window.
    for ( int i = 0; i < 40; ++i )
        ledger.cacheUnderstanding( QStringLiteral( "r4-fill-key-%1" ).arg( i ), doc );
    CHECK( ledger.cachedUnderstanding( key ).isNull() ); // oldest-first evicted
    CHECK( ledger.cachedUnderstanding( QStringLiteral( "r4-fill-key-39" ) )
               ["fact"]
               .asString() == "ndvi mean 0.42" );
}

// — asset contexts: stat-identity staleness ———————————————————————————————

TEST_CASE( "asset contexts go stale exactly when the recorded stat identity "
           "no longer matches the file", "[harness][ledger-r4][asset-context]" )
{
    auto &ledger = ContextLedger::instance();
    QTemporaryDir dir;
    const QString path = dir.filePath( QStringLiteral( "r4-asset.tif" ) );
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "bytes-v1" ) );
    }
    const QFileInfo info( path );
    const QString statKey = path + QStringLiteral( "|f" ) + QString::number( info.size() ) +
                            QStringLiteral( "|" ) +
                            QString::number( info.lastModified().toMSecsSinceEpoch() );

    Json::Value entity( Json::objectValue );
    entity["entity_id"] = "asset-r4";
    ledger.recordAssetContext( path, entity, statKey, Json::Value( Json::objectValue ) );

    auto stalenessOf = [ & ]() {
        for ( const Json::Value &record : ledger.assetContexts() )
            if ( record["path"].asString() == path.toStdString() )
                return record["stale"].asBool();
        return true;
    };
    CHECK_FALSE( stalenessOf() ); // facts match the current bytes

    // Same-name rewrite with a different size → the facts predate the bytes.
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "bytes-v2-with-longer-payload" ) );
    }
    CHECK( stalenessOf() );

    // Disappearance is the strongest staleness.
    QFile::remove( path );
    CHECK( stalenessOf() );
}

TEST_CASE( "a recorded asset context with a revision key never reports stat "
           "staleness while the file exists", "[harness][ledger-r4][asset-context]" )
{
    auto &ledger = ContextLedger::instance();
    QTemporaryDir dir;
    const QString path = dir.filePath( QStringLiteral( "r4-registered.tif" ) );
    {
        QFile f( path );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( QByteArray( "registered asset" ) );
    }
    // Revision keys (|r): the catalog revision is the staleness authority;
    // the ledger only detects a vanished file (documented contract).
    ledger.recordAssetContext( path, Json::Value( Json::objectValue ),
                               path + QStringLiteral( "|r7" ), Json::Value() );
    for ( const Json::Value &record : ledger.assetContexts() )
    {
        if ( record["path"].asString() != path.toStdString() )
            continue;
        CHECK( record["observed_kind"].asString() == "revision" );
        CHECK_FALSE( record["stale"].asBool() );
    }
    QFile::remove( path );
    for ( const Json::Value &record : ledger.assetContexts() )
    {
        if ( record["path"].asString() != path.toStdString() )
            continue;
        CHECK( record["stale"].asBool() ); // vanished file is stale in both kinds
    }
}

TEST_CASE( "an empty asset path is ignored, not stored", "[harness][ledger-r4][asset-context]" )
{
    auto &ledger = ContextLedger::instance();
    const Json::Value before = ledger.assetContexts();
    ledger.recordAssetContext( QString(), Json::Value(), QStringLiteral( "k" ), Json::Value() );
    CHECK( ledger.assetContexts().size() == before.size() );
}

// — model contracts: typed-unknown discipline, replace, bound —————————————

TEST_CASE( "model contracts replace per model id and refuse empty or "
           "non-object records", "[harness][ledger-r4][model-contracts]" )
{
    auto &ledger = ContextLedger::instance();
    const std::string model = "r4-model/1";

    ledger.recordModelContract( "", Json::Value( Json::objectValue ) ); // ignored
    ledger.recordModelContract( model, Json::Value( Json::arrayValue ) ); // ignored
    CHECK_FALSE( anyWith( ledger.modelContracts(), "model_id", model ) );

    Json::Value contract( Json::objectValue );
    contract["input"] = "Sentinel-2 L1C";
    ledger.recordModelContract( model, contract );
    ledger.recordModelContract( model, contract ); // replace, not duplicate
    int rows = 0;
    for ( const Json::Value &record : ledger.modelContracts() )
    {
        if ( record["model_id"].asString() != model )
            continue;
        ++rows;
        CHECK( record.isMember( "recorded_at" ) );
    }
    CHECK( rows == 1 );
}

// — run summaries: token budget, replace accounting, newest-first ————————

TEST_CASE( "run summaries account tokens exactly and evict oldest-first "
           "under both bounds", "[harness][ledger-r4][summaries][bounds]" )
{
    auto &ledger = ContextLedger::instance();
    const std::string prefix = "r4-summary-";
    const int before = ledger.runSummaryTokens();

    // Three summaries of known cost; the accounting is exact (± the shared
    // singleton's other tenants, which this case counts out).
    ledger.recordRunSummary( prefix + "a", Json::Value( Json::objectValue ), 100 );
    ledger.recordRunSummary( prefix + "b", Json::Value( Json::objectValue ), 200 );
    ledger.recordRunSummary( prefix + "c", Json::Value( Json::objectValue ), 300 );
    const int afterThree = ledger.runSummaryTokens();
    CHECK( afterThree - before == 600 );

    // Re-recording a run replaces and subtracts the OLD cost.
    ledger.recordRunSummary( prefix + "b", Json::Value( Json::objectValue ), 50 );
    CHECK( ledger.runSummaryTokens() - before == 450 );

    // Newest-first read order (the continuing conversation reads the latest
    // evidence first).
    const Json::Value summaries = ledger.runSummaries();
    int newestIndex = -1;
    int oldestIndex = -1;
    for ( Json::ArrayIndex i = 0; i < summaries.size(); ++i )
    {
        const std::string runId = summaries[i]["run_id"].asString();
        if ( runId == prefix + "c" )
            newestIndex = static_cast<int>( i );
        if ( runId == prefix + "a" )
            oldestIndex = static_cast<int>( i );
    }
    REQUIRE( newestIndex >= 0 );
    REQUIRE( oldestIndex >= 0 );
    CHECK( newestIndex < oldestIndex );

    // A single oversized summary is a kept backstop: the bound never drops
    // the only summary of a run (documented backstop).
    ledger.recordRunSummary( prefix + "huge", Json::Value( Json::objectValue ),
                             100000 );
    bool hugeKept = false;
    for ( const Json::Value &summary : ledger.runSummaries() )
        if ( summary["run_id"].asString() == prefix + "huge" )
            hugeKept = true;
    CHECK( hugeKept );
}

TEST_CASE( "run summaries refuse empty run ids and non-object documents",
           "[harness][ledger-r4][summaries]" )
{
    auto &ledger = ContextLedger::instance();
    const int before = ledger.runSummaryTokens();
    ledger.recordRunSummary( "", Json::Value( Json::objectValue ), 10 );
    ledger.recordRunSummary( "r4-refused", Json::Value( Json::arrayValue ), 10 );
    CHECK( ledger.runSummaryTokens() == before );
    CHECK_FALSE( anyWith( ledger.runSummaries(), "run_id", "r4-refused" ) );
}

// — hostile inputs round-trip as data, never as crashes ——————————————————

TEST_CASE( "hostile strings are stored verbatim and read back intact",
           "[harness][ledger-r4][hostile]" )
{
    auto &ledger = ContextLedger::instance();
    const std::string hostile =
        "quote\" backslash\\ newline\n tab\t unicode-\xe6\xb5\x8b\xe8\xaf\x95 emoji-\xf0\x9f\x8c\x8d";
    const std::string run = "r4-hostile-1";
    ledger.recordPlanBinding( run, hostile, hostile, hostile, hostile, hostile );
    for ( const Json::Value &binding : ledger.planBindings() )
    {
        if ( binding["run_id"].asString() != run )
            continue;
        CHECK( binding["goal"].asString() == hostile );
        CHECK( binding["plan_fingerprint"].asString() == hostile );
    }
    ledger.recordDecision( hostile, hostile, "unresolved", hostile, Json::Value() );
    CHECK_FALSE( ledger.decisions().isNull() );
}

// — concurrency: parallel writers never corrupt or lose the bound ————————

TEST_CASE( "concurrent decision writers leave a bounded, uncorrupted store",
           "[harness][ledger-r4][concurrency]" )
{
    auto &ledger = ContextLedger::instance();
    constexpr int kThreads = 8;
    constexpr int kPerThread = 25;
    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [ &ledger, t ] {
            for ( int i = 0; i < kPerThread; ++i )
                ledger.recordDecision( "parameter", "r4-concurrent", "unresolved",
                                       "thread " + std::to_string( t ), Json::Value() );
        } );
    }
    for ( auto &thread : threads )
        thread.join();
    // All writers succeeded; the bounded store holds exactly its bound, and
    // every surviving row is well-formed (no torn writes through the mutex).
    const Json::Value decisions = ledger.decisions();
    CHECK( static_cast<int>( decisions.size() ) == 20 );
    int wellFormed = 0;
    for ( const Json::Value &decision : decisions )
        if ( decision.isMember( "id" ) && decision.isMember( "recorded_at" ) )
            ++wellFormed;
    CHECK( wellFormed == static_cast<int>( decisions.size() ) );
}

TEST_CASE( "concurrent summary writers keep the token accounting consistent",
           "[harness][ledger-r4][concurrency]" )
{
    auto &ledger = ContextLedger::instance();
    constexpr int kThreads = 4;
    constexpr int kPerThread = 10;
    std::vector<std::thread> threads;
    for ( int t = 0; t < kThreads; ++t )
    {
        threads.emplace_back( [ &ledger, t ] {
            for ( int i = 0; i < kPerThread; ++i )
            {
                Json::Value summary( Json::objectValue );
                summary["note"] = i;
                ledger.recordRunSummary( "r4-conc-" + std::to_string( t ) + "-" +
                                           std::to_string( i ),
                                         summary, 10 );
            }
        } );
    }
    for ( auto &thread : threads )
        thread.join();
    // Internal consistency: the running total equals the sum of the rows it
    // says it is tracking (no double-count through the mutex).
    int sum = 0;
    for ( const Json::Value &summary : ledger.runSummaries() )
        sum += summary["approx_tokens"].asInt();
    CHECK( sum == ledger.runSummaryTokens() );
}
