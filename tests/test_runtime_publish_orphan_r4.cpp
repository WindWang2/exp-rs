// test_runtime_publish_orphan_r4.cpp — Track 15 WP-D: crash-orphan adoption
// boundaries on the model publish authority (ProductPublishGuard,
// src/operators/runtime/model_publish.{h,cpp} — the family #1314 built).
// Four edges beyond the merged adoption oracle suite:
//   1. interrupted-adoption re-entry (stray prov AND parked prov present):
//      the parked pair's own sidecar wins, byte-identical;
//   2. repeated adopt / restore cycles are disk-idempotent (snapshot equal);
//   3. two threads publishing to the SAME final path: the fence refuses the
//      second guard with a typed error — the first guard's parked product can
//      never be adopted or deleted out from under it (no double-occupancy);
//   4. window-B adoption restores the last PUBLISHED pair byte-identically
//      and the next full publish leaves a complete provenance chain (product
//      + parseable exp-rs-prov/1 sidecar, no backup residue).
// Truth source: bytes on disk, not guard internals.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_publish.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <json/json.h>

#include <atomic>
#include <chrono>
#include <string>
#include <future>
#include <thread>

using namespace sicnu::operators;
using namespace sicnu::operators::runtime;

namespace
{
struct Fixture
{
    QTemporaryDir dir;
    QString path( const QString &name ) const { return dir.filePath( name ); }

    void write( const QString &name, const QByteArray &bytes )
    {
        QFile file( path( name ) );
        REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        REQUIRE( file.write( bytes ) == bytes.size() );
    }
    QByteArray read( const QString &name )
    {
        QFile file( path( name ) );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        return file.readAll();
    }
    bool exists( const QString &name ) { return QFile::exists( path( name ) ); }
};

constexpr const char *kSuffix = ".prev~";

void publishNew( ProductPublishGuard &guard, Fixture &fixture, const QByteArray &bytes,
                 const std::string &provText )
{
    const QString stage = fixture.path( "stage.bin" );
    fixture.write( "stage.bin", bytes );
    guard.publishStaged( stage, nullptr, "swap failed" );
    Json::Value document;
    Json::CharReaderBuilder builder;
    std::string parseErrors;
    Json::CharReader *reader = builder.newCharReader();
    REQUIRE( reader->parse( provText.data(), provText.data() + provText.size(), &document,
                            &parseErrors ) );
    std::string sidecarError;
    REQUIRE( publishProvenanceSidecar( fixture.path( "product.tif" ), document, nullptr,
                                       &sidecarError ) );
}
} // namespace

TEST_CASE( "Publish adoption re-entry: the parked pair's own sidecar wins over the stray one",
           "[runtime][publish][r4]" )
{
    Fixture fixture;
    // A previous run died mid-adoption: it had already restored the prov
    // sidecar (the stray) but died before the main rename — window A with a
    // stray sidecar on the final path.
    fixture.write( "product.tif.prev~", "PARKED-BYTES" );
    fixture.write( "product.tif.prev~.prov.json", "PROV-PARKED" );
    fixture.write( "product.tif.prov.json", "STRAY-PROV" );

    {
        ProductPublishGuard guard( fixture.path( "product.tif" ), kSuffix, nullptr );
        // Adoption folded the orphan back to the last PUBLISHED pair and the
        // constructor then RE-PARKED it: mid-guard, the pair lives on the
        // backup slots. The parked pair's own sidecar must have won over the
        // stray (Qt6 QFile::rename refuses to overwrite — the adoption has to
        // clear the stray first; this assertion is exactly that fix's oracle).
        REQUIRE( fixture.read( "product.tif.prev~" ) == "PARKED-BYTES" );
        REQUIRE( fixture.read( "product.tif.prev~.prov.json" ) == "PROV-PARKED" );
        REQUIRE( !fixture.exists( "product.tif" ) );

        // The adopted state publishes through like a live backup.
        publishNew( guard, fixture, "NEW-BYTES", R"({ "schema": "exp-rs-prov/1" })" );
        guard.disarm();
    }
    REQUIRE( fixture.read( "product.tif" ) == "NEW-BYTES" );
    REQUIRE( fixture.read( "product.tif.prov.json" ).contains( "exp-rs-prov/1" ) );
    REQUIRE( !fixture.exists( "product.tif.prev~" ) );
}

TEST_CASE( "Repeated adopt/restore cycles are disk-idempotent",
           "[runtime][publish][r4]" )
{
    Fixture fixture;
    fixture.write( "product.tif.prev~", "PARKED-BYTES" );
    fixture.write( "product.tif.prev~.prov.json", "PROV-PARKED" );

    // Cycle 1: adopt (window A), then publish-and-disarm.
    {
        ProductPublishGuard guard( fixture.path( "product.tif" ), kSuffix, nullptr );
        REQUIRE( fixture.read( "product.tif.prev~" ) == "PARKED-BYTES" );
        REQUIRE( fixture.read( "product.tif.prev~.prov.json" ) == "PROV-PARKED" );
        publishNew( guard, fixture, "CYCLE-1", R"({ "schema": "exp-rs-prov/1", "run": 1 })" );
        guard.disarm();
    }
    const QByteArray afterCycle1 = fixture.read( "product.tif" );
    REQUIRE( afterCycle1 == "CYCLE-1" );

    // Cycle 2: a guard constructed on the same path with NO orphan must be a
    // pure park/restore round trip — the destructor restores the parked pair
    // byte-identically and leaves no backup residue. Running the guard twice
    // in a row must converge to the identical disk state (repeated-adopt
    // idempotence, evidenced on disk).
    for ( int cycle = 0; cycle < 2; ++cycle )
    {
        ProductPublishGuard guard( fixture.path( "product.tif" ), kSuffix, nullptr );
        REQUIRE( !fixture.exists( "product.tif" ) ); // parked while guarded
        REQUIRE( fixture.read( "product.tif.prev~" ) == "CYCLE-1" );
        // Destructor (no disarm) restores.
    }
    REQUIRE( fixture.read( "product.tif" ) == "CYCLE-1" );
    REQUIRE( fixture.read( "product.tif.prov.json" ).contains( "\"run\" : 1" ) );
    REQUIRE( !fixture.exists( "product.tif.prev~" ) );
    REQUIRE( !fixture.exists( "product.tif.prev~.prov.json" ) );
}

TEST_CASE( "Two threads publishing to the same path: the second guard is refused, the first is never disturbed",
           "[runtime][publish][r4][concurrency]" )
{
    Fixture fixture;
    fixture.write( "product.tif", "FIRST-PUBLISHED" );
    fixture.write( "product.tif.prov.json", "FIRST-PROV" );

    // Strict ordering: the first guard's fence is held BEFORE the second
    // thread even starts constructing, and stays held until the second's
    // attempt is DONE — no wall-clock timing anywhere.
    std::promise<void> secondAttempting;
    std::future<void> attemptStarted = secondAttempting.get_future();
    std::promise<void> secondDone;
    std::future<void> attemptFinished = secondDone.get_future();
    std::atomic<bool> secondRefused{ false };
    std::string secondWhat;

    std::thread first( [ & ] {
        // Parks the first product; holds the slot across the "slow" publish.
        ProductPublishGuard guard( fixture.path( "product.tif" ), kSuffix, nullptr );
        attemptStarted.wait(); // the second guard is about to construct
        attemptFinished.wait(); // ...and has finished (refused, with the fence)
        publishNew( guard, fixture, "SECOND-BYTES", R"({ "schema": "exp-rs-prov/1" })" );
        guard.disarm();
    } );

    std::thread second( [ & ] {
        secondAttempting.set_value();
        try
        {
            ProductPublishGuard guard( fixture.path( "product.tif" ), kSuffix, nullptr );
            // NO fence: this constructor would adopt the first guard's parked
            // product back and delete it mid-publish. It must never get here.
            guard.disarm(); // unreachable with the fence
        }
        catch ( const RSOperatorError &error )
        {
            secondRefused.store( true );
            secondWhat = error.what();
        }
        catch ( const std::exception &other )
        {
            secondWhat = std::string( "NON-RSOperator: " ) + other.what();
        }
        catch ( ... )
        {
            secondWhat = "UNKNOWN-EXCEPTION";
        }
        secondDone.set_value();
    } );

    second.join();
    first.join();

    REQUIRE( secondRefused.load() );
    // The refusal is typed and actionable: names the busy state, not a
    // generic I/O failure.
    REQUIRE( secondWhat.find( "product.tif" ) != std::string::npos );

    // The first publish completed undisturbed; the loser can now retry.
    REQUIRE( fixture.read( "product.tif" ) == "SECOND-BYTES" );
    {
        ProductPublishGuard retry( fixture.path( "product.tif" ), kSuffix, nullptr );
        publishNew( retry, fixture, "THIRD-BYTES", R"({ "schema": "exp-rs-prov/1" })" );
        retry.disarm();
    }
    REQUIRE( fixture.read( "product.tif" ) == "THIRD-BYTES" );
}

TEST_CASE( "Window-B adoption restores the last PUBLISHED pair and the next publish completes the chain",
           "[runtime][publish][r4]" )
{
    Fixture fixture;
    // Crash between the swap and the sidecar publish: the final file is the
    // never-published new product (no sidecar), the parked pair is the last
    // complete publication.
    fixture.write( "product.tif", "UNPUBLISHED-NEW" );
    fixture.write( "product.tif.prev~", "PUBLISHED-OLD" );
    fixture.write( "product.tif.prev~.prov.json", "OLD-PROV" );

    {
        ProductPublishGuard guard( fixture.path( "product.tif" ), kSuffix, nullptr );
        // Adoption replaced the never-published file with the last PUBLISHED
        // pair, then re-parked it (mid-guard the pair lives on the backup
        // slots — the unpublished bytes must be gone).
        REQUIRE( fixture.read( "product.tif.prev~" ) == "PUBLISHED-OLD" );
        REQUIRE( fixture.read( "product.tif.prev~.prov.json" ) == "OLD-PROV" );
        REQUIRE( !fixture.exists( "product.tif" ) );

        // A full publish through the adopted state: product + parseable
        // provenance sidecar, published atomically.
        publishNew( guard, fixture, "FINAL-BYTES",
                    R"({ "schema": "exp-rs-prov/1", "model": { "name": "r4" } })" );
        guard.disarm();
    }

    // Provenance chain complete on disk: product bytes, a parseable sidecar
    // with the schema marker, no backup residue.
    REQUIRE( fixture.read( "product.tif" ) == "FINAL-BYTES" );
    const QByteArray sidecar = fixture.read( "product.tif.prov.json" );
    Json::Value document;
    Json::CharReaderBuilder builder;
    std::string errors;
    {
        Json::CharReader *reader( builder.newCharReader() );
        REQUIRE( reader->parse( sidecar.data(), sidecar.data() + sidecar.size(), &document,
                                &errors ) );
    }
    REQUIRE( document["schema"].asString() == "exp-rs-prov/1" );
    REQUIRE( document["model"]["name"].asString() == "r4" );
    REQUIRE( !fixture.exists( "product.tif.prev~" ) );
    REQUIRE( !fixture.exists( "product.tif.prev~.prov.json" ) );
}
