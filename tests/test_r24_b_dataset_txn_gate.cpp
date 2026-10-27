// tests/test_r24_b_dataset_txn_gate.cpp — R24-B (WP-B3, P1): the dataset
// store's draft/mutability gates must run INSIDE the write transaction
// (#811 playbook, previously applied only to the facets path). With the
// checks outside BEGIN IMMEDIATE, two connections on one file could
// interleave commitVersion against addSamples / addSourceAssetsToDraft /
// stageVersion and (a) freeze rows into a committed version, (b) commit a
// STALE manifest that silently loses the concurrent asset edit, or (c)
// return success() for an UPDATE that matched zero rows. The barrier racer
// below (same pattern as test_dataset_core.cpp's duplicate-create racer)
// asserts the serialized contract: commit and a draft-mutating write can
// never BOTH succeed on the same version.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/sample.h"

#include <QTemporaryDir>

#include <atomic>
#include <thread>

using namespace sicnu::dataset;

namespace {

// Same manifest shape as test_dataset_core.cpp's makeManifest, plus N filler
// source assets: a large manifest widens the parse/canonicalize/fingerprint
// phase of commitVersion so the racer's barrier reliably straddles the
// check→begin window the old code exposed.
DatasetManifest makeManifest( const QString &datasetId, const QString &versionId, int fillerAssets )
{
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId );
    manifest.setVersionId( versionId );
    manifest.setName( QStringLiteral( "R24-B Txn Gate Dataset" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString( QStringLiteral( "2026-10-27T00:00:00.000Z" ),
                                                    Qt::ISODateWithMs ) );

    SourceAssetRef base;
    base.assetId = QStringLiteral( "base-00000000-0000-4000-8000-000000000000" );
    base.revision = 1;
    base.role = QStringLiteral( "image" );
    manifest.sourceAssets().append( base );
    for ( int i = 0; i < fillerAssets; ++i )
    {
        SourceAssetRef filler;
        filler.assetId = QStringLiteral( "filler-%1-0000-4000-8000-000000000000" ).arg( i );
        filler.revision = 1;
        filler.role = QStringLiteral( "image" );
        manifest.sourceAssets().append( filler );
    }

    DatasetEntry entry;
    entry.kind = QStringLiteral( "asset" );
    entry.refId = base.assetId;
    entry.role = QStringLiteral( "image" );
    manifest.entries().append( entry );

    manifest.schema().modality = QStringLiteral( "optical" );
    manifest.schema().crs = QStringLiteral( "EPSG:32650" );
    manifest.schema().resolutionX = 10.0;
    manifest.schema().resolutionY = 10.0;
    manifest.schema().resolutionUnit = QStringLiteral( "m" );
    manifest.spatialExtent().valid = true;
    manifest.spatialExtent().minimumX = 500000.0;
    manifest.spatialExtent().minimumY = 4000000.0;
    manifest.spatialExtent().maximumX = 510000.0;
    manifest.spatialExtent().maximumY = 4010000.0;
    return manifest;
}

struct RacerOutcome
{
    bool ok = false;
    QString code;
};

} // namespace

TEST_CASE( "commitVersion vs addSourceAssetsToDraft cannot both succeed "
           "(draft gate inside the write transaction, R24-B)",
           "[r24_b][dataset][store][race]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "racer.db" ) );
    DatasetStore storeA;
    REQUIRE( storeA.open( dbPath ) );
    DatasetStore storeB;
    REQUIRE( storeB.open( dbPath ) );

    constexpr int kRounds = 40;
    constexpr int kFillerAssets = 200;
    int bothSucceeded = 0;
    int commitWon = 0;
    int assetWon = 0;
    int staleCommit = 0;

    for ( int round = 0; round < kRounds; ++round )
    {
        const DatasetId datasetId = DatasetId::generate();
        REQUIRE( storeA.createDataset( datasetId, QStringLiteral( "racer" ) ).has_value() );
        const DatasetVersionId versionId = DatasetVersionId::generate();
        REQUIRE( storeA
                     .createDraftVersion(
                         makeManifest( datasetId.toString(), versionId.toString(), kFillerAssets ),
                         QStringLiteral( "racer draft" ) )
                     .has_value() );
        REQUIRE( storeA.stageVersion( versionId ).has_value() );

        SourceAssetRef racerAsset;
        racerAsset.assetId = QStringLiteral( "racer-%1-0000-4000-8000-000000000000" ).arg( round );
        racerAsset.revision = 7;
        racerAsset.role = QStringLiteral( "label" );

        std::atomic<int> arrived{ 0 };
        RacerOutcome commitOut, assetOut;

        auto committer = [ & ]( DatasetStore *store, std::atomic<int> &gate, RacerOutcome *out )
        {
            ++gate;
            while ( gate.load() < 2 )
                std::this_thread::yield();
            const auto result = store->commitVersion( versionId );
            out->ok = result.has_value();
            if ( !result.has_value() )
                out->code = result.diagnostics().first().code;
        };
        auto assetter = [ & ]( DatasetStore *store, std::atomic<int> &gate, RacerOutcome *out )
        {
            ++gate;
            while ( gate.load() < 2 )
                std::this_thread::yield();
            const auto result = store->addSourceAssetsToDraft( versionId, { racerAsset } );
            out->ok = result.has_value();
            if ( !result.has_value() )
                out->code = result.diagnostics().first().code;
        };

        std::thread ta( committer, &storeA, std::ref( arrived ), &commitOut );
        std::thread tb( assetter, &storeB, std::ref( arrived ), &assetOut );
        ta.join();
        tb.join();

        if ( commitOut.ok && assetOut.ok )
        {
            ++bothSucceeded;
            // The old code could return success for an UPDATE that matched
            // zero rows (assetter raced past the commit) — the asset is then
            // nowhere at all. Either way both-succeed is already a violation
            // of the serialized contract; count the lost-update shape too.
            const auto reloaded = storeA.versionById( versionId );
            if ( reloaded && !reloaded->manifestJson().contains( racerAsset.assetId ) )
                ++staleCommit;
        }
        else if ( commitOut.ok )
        {
            ++commitWon;
            // Commit won: the committed manifest must be the staged snapshot
            // WITHOUT the racer asset (the assetter failed the draft gate).
            const auto reloaded = storeA.versionById( versionId );
            REQUIRE( reloaded.has_value() );
            CHECK( reloaded->status() == DatasetVersionStatus::Committed );
            CHECK_FALSE( reloaded->manifestJson().contains( racerAsset.assetId ) );
            CHECK( assetOut.code == QLatin1String( "dataset.not_draft" ) );
        }
        else if ( assetOut.ok )
        {
            ++assetWon;
            // Asset won: still a draft, asset present, staged flag reset by
            // the write, commit refused by the (re-checked) staged gate.
            const auto reloaded = storeA.versionById( versionId );
            REQUIRE( reloaded.has_value() );
            CHECK( reloaded->status() == DatasetVersionStatus::Draft );
            CHECK( reloaded->manifestJson().contains( racerAsset.assetId ) );
            CHECK( commitOut.code == QLatin1String( "dataset.not_staged" ) );
        }
        else
        {
            // Both failing is legal only for typed external reasons; with
            // these two calls it should not happen — surface it loudly.
            FAIL( "both racer sides failed: commit=" + commitOut.code.toStdString()
                  + " asset=" + assetOut.code.toStdString() );
        }
    }

    CHECK( bothSucceeded == 0 );
    CHECK( staleCommit == 0 );
    // The race must have exercised BOTH orderings — otherwise the barrier
    // never really interleaved and the assertions above were vacuous.
    CHECK( commitWon > 0 );
    CHECK( assetWon > 0 );
}

TEST_CASE( "addSamples vs commitVersion: samples never land in a committed "
           "version (draft gate inside the write transaction, R24-B)",
           "[r24_b][dataset][store][race]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "racer.db" ) );
    DatasetStore storeA;
    REQUIRE( storeA.open( dbPath ) );
    DatasetStore storeB;
    REQUIRE( storeB.open( dbPath ) );

    constexpr int kRounds = 40;
    int bothOk = 0;
    int commitOnly = 0;

    for ( int round = 0; round < kRounds; ++round )
    {
        const DatasetId datasetId = DatasetId::generate();
        REQUIRE( storeA.createDataset( datasetId, QStringLiteral( "racer" ) ).has_value() );
        const DatasetVersionId versionId = DatasetVersionId::generate();
        REQUIRE( storeA
                     .createDraftVersion(
                         makeManifest( datasetId.toString(), versionId.toString(), 200 ),
                         QStringLiteral( "racer draft" ) )
                     .has_value() );
        REQUIRE( storeA.stageVersion( versionId ).has_value() );

        SampleRecord sample;
        sample.setSampleId( QStringLiteral( "racer-sample-%1" ).arg( round ) );
        sample.setDatasetVersionId( versionId.toString() );
        sample.setKind( SampleKind::Point );
        PointSample point;
        point.x = round;
        point.y = 0;
        sample.payload() = point;

        std::atomic<int> arrived{ 0 };
        RacerOutcome commitOut, samplesOut;

        auto committer = [ & ]( DatasetStore *store, std::atomic<int> &gate, RacerOutcome *out )
        {
            ++gate;
            while ( gate.load() < 2 )
                std::this_thread::yield();
            const auto result = store->commitVersion( versionId );
            out->ok = result.has_value();
            if ( !result.has_value() )
                out->code = result.diagnostics().first().code;
        };
        auto sampler = [ & ]( DatasetStore *store, std::atomic<int> &gate, RacerOutcome *out )
        {
            ++gate;
            while ( gate.load() < 2 )
                std::this_thread::yield();
            const auto result = store->addSamples( { sample } );
            out->ok = result.has_value();
            if ( !result.has_value() )
                out->code = result.diagnostics().first().code;
        };

        std::thread ta( committer, &storeA, std::ref( arrived ), &commitOut );
        std::thread tb( sampler, &storeB, std::ref( arrived ), &samplesOut );
        ta.join();
        tb.join();

        // Serialized contract (fixed shape): exactly two legal outcomes.
        //  A. commit's BEGIN IMMEDIATE wins first → commit ok, the sample
        //     writer fails the IN-TRANSACTION draft gate, the frozen version
        //     holds zero racer samples. (Old code: the INSERT serialized
        //     after the commit and landed in the committed version.)
        //  B. the sample txn wins first → both ok: the sample was written
        //     while still a draft and the commit then froze it. The version
        //     is Committed and holds exactly the one racer sample.
        //  "sampler ok + commit failed" cannot happen (a successful sample
        //  write leaves the version staged+draft, and commit re-validates
        //  inside its own transaction).
        if ( commitOut.ok && !samplesOut.ok )
        {
            ++commitOnly;
            CHECK( samplesOut.code == QLatin1String( "dataset.not_draft" ) );
            CHECK( storeA.sampleCount( versionId ) == 0 );
            CHECK( storeA.versionById( versionId )->status()
                   == DatasetVersionStatus::Committed );
        }
        else if ( commitOut.ok && samplesOut.ok )
        {
            ++bothOk;
            CHECK( storeA.sampleCount( versionId ) == 1 );
            CHECK( storeA.versionById( versionId )->status()
                   == DatasetVersionStatus::Committed );
        }
        else
        {
            FAIL( "unexpected racer outcome: commit=" + commitOut.code.toStdString()
                  + " samples=" + samplesOut.code.toStdString() );
        }
    }

    CHECK( commitOnly > 0 );
    CHECK( bothOk > 0 );
}
