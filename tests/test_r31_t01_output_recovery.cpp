// tests/test_r31_t01_output_recovery.cpp — R31-T01 WP04.
//
// Output identity facts across the commit / cache-hit boundary:
//   • a cache hit (PUBLISHED-marker reconcile) whose catalog record an
//     external-content event left non-Ready must CONVERGE to the usable
//     state a full commit() run produces — same Asset ID, Ready, truthful
//     provenance — instead of handing back a stale record as delivered
//     output;
//   • a genuinely unchanged hit stays untouched (no revision advance, no
//     provenance rewrite, no display re-request);
//   • a record whose kind disagrees with the standing file is refused with
//     a typed diagnostic, never silently reused or forked onto one path;
//   • an artifact that carries a finalize manifest keeps a truthful one
//     across a republish, and a replaced artifact (same path, different
//     bytes) demotes the marker hit to a rebuild instead of serving stale
//     bytes as a zero-work success.
//
// The chunked half uses the synthetic-adopter pattern: the artifact's
// bytes are what the test manipulates, so the digest facts under test are
// exactly the ones the framework observes.
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal.h>
#include <ogr_api.h>

#include <string>
#include <vector>

#include "data/data_manager.h"
#include "geospatial/io/finalize_manifest.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator_context.h"
#include "processing/framework/output_committer.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using sicnu::operators::ChunkedOutputCommitSpec;
using sicnu::operators::ChunkedRunOptions;
using sicnu::operators::ChunkTileKernel;
using sicnu::operators::RSOperatorContext;
using sicnu::operators::runChunkedOperator;
using sicnu::runtime::chunk::TileRunDeterminism;
using sicnu::runtime::chunk::TileSpec;

namespace
{

constexpr int kRasterW = 24;
constexpr int kRasterH = 8;
constexpr int kTileW = 4;
constexpr int kTileH = 2;
constexpr int kBands = 2;

/// Deterministic single-band Float32 GeoTIFF. Same parameters ⇒ same bytes
/// (GDAL's GTiff writer embeds no timestamp), which is what makes the
/// digest comparisons in this file meaningful.
void writeRaster( const QString &path, float value )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    QDir().mkpath( QFileInfo( path ).absolutePath() );
    GDALDatasetH ds =
        GDALCreate( driver, path.toUtf8().constData(), kRasterW, kRasterH, 1,
                    GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( kRasterH ), 0.0, -1.0 };
    REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( static_cast<size_t>( kRasterW ), value );
    for ( int row = 0; row < kRasterH; ++row )
        REQUIRE( GDALRasterIO( band, GF_Write, 0, row, kRasterW, 1, line.data(),
                               kRasterW, 1, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
}

void writeVector( const QString &path )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GPKG" );
    REQUIRE( driver != nullptr );
    QDir().mkpath( QFileInfo( path ).absolutePath() );
    GDALDatasetH ds =
        GDALCreate( driver, path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
    REQUIRE( ds != nullptr );
    OGRLayerH layer = GDALDatasetCreateLayer( ds, "pts", nullptr, wkbPoint, nullptr );
    REQUIRE( layer != nullptr );
    GDALClose( ds );
}

sicnu::AlgorithmOutputRequest rasterRequest( const QString &stablePath )
{
    sicnu::AlgorithmOutputRequest req;
    req.kind = sicnu::data::AssetKind::Raster;
    req.stablePath = stablePath;
    req.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    req.autoLoad = false;
    req.derivation.algorithmId = QStringLiteral( "test:r31_t01_output_recovery" );
    req.derivation.algorithmVersion = QStringLiteral( "1.0.0" );
    return req;
}

sicnu::runtime::chunk::TileRunPartition recoveryPartition()
{
    sicnu::runtime::chunk::TileRunPartition p;
    p.rasterWidth = kRasterW;
    p.rasterHeight = kRasterH;
    p.tileWidth = kTileW;
    p.tileHeight = kTileH;
    p.bands = kBands;
    return p;
}

std::vector<float> recoveryKernel( const TileSpec &s )
{
    std::vector<float> buffer( s.bufferElementCount(), 0.25f );
    return buffer;
}

/// Writes the platform finalize manifest for an already-published artifact,
/// declaring the facts the canonical inspector reads from the standing
/// bytes — the state a finalizing producer leaves behind.
void writeManifestForStandingArtifact( const QString &stablePath )
{
    const std::string mainPath = stablePath.toStdString();
    const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( mainPath );
    sicnu::geo::io::FinalizeManifestFields fields;
    fields.producer = "test:r31_t01_output_recovery";
    fields.driver = meta.driver;
    fields.width = meta.width;
    fields.height = meta.height;
    fields.bandCount = meta.bandCount;
    fields.dtype = meta.bands.empty() ? std::string() : meta.bands.front().dtype;
    fields.crsAuthid = meta.crs.authid;
    sicnu::geo::io::writeFinalizeManifest(
        mainPath, sicnu::geo::io::buildFinalizeManifest( mainPath, fields ) );
}

} // namespace

TEST_CASE( "R31-T01 WP04: reconcile converges a record an external change left non-Ready",
           "[r31][t01][output_recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString stable = dir.filePath( "out.tif" );
    const QString temp = dir.filePath( "out.temp.tif" );

    writeRaster( temp, 0.5f );
    sicnu::data::DataManager catalog;
    sicnu::OutputCommitter committer( &catalog );

    auto commitReq = rasterRequest( stable );
    commitReq.tempPath = temp;
    const auto committed = committer.commit( commitReq );
    REQUIRE( committed );
    const sicnu::data::AssetId id = committed.value();
    const auto ready = catalog.asset( id );
    REQUIRE( ready.has_value() );
    CHECK( ready->state() == sicnu::data::AssetState::Ready );
    REQUIRE( catalog.provenance( id ).has_value() );
    const quint64 revisionAfterCommit = ready->revision().value();

    // External deletion: the catalog record no longer describes a file. The
    // Data Manager re-resolves and flags the record non-Ready.
    REQUIRE( QFile::remove( stable ) );
    REQUIRE( static_cast<bool>( catalog.notifyExternalContentChange( id ) ) );
    const auto nonReady = catalog.asset( id );
    REQUIRE( nonReady.has_value() );
    REQUIRE( nonReady->state() != sicnu::data::AssetState::Ready );

    // Same bytes restored at the stable path — the cache-hit scenario of
    // E2E-5 ("Error → restore → cache hit").
    writeRaster( stable, 0.5f );

    const auto reconciled = committer.reconcilePublished( rasterRequest( stable ) );
    REQUIRE( reconciled );
    CHECK( reconciled.value() == id ); // stable Asset ID, not a fork
    const auto recovered = catalog.asset( id );
    REQUIRE( recovered.has_value() );
    CHECK( recovered->state() == sicnu::data::AssetState::Ready );
    // Honest refresh: the record moved, so the revision advanced. A silent
    // reuse here would leave the non-Ready facts standing.
    CHECK( recovered->revision().value() > revisionAfterCommit );
    CHECK( catalog.provenance( id ).has_value() );
}

TEST_CASE( "R31-T01 WP04: a genuinely unchanged reconcile hit stays untouched",
           "[r31][t01][output_recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString stable = dir.filePath( "out.tif" );
    const QString temp = dir.filePath( "out.temp.tif" );

    writeRaster( temp, 0.5f );
    sicnu::data::DataManager catalog;
    sicnu::OutputCommitter committer( &catalog );

    auto commitReq = rasterRequest( stable );
    commitReq.tempPath = temp;
    const auto committed = committer.commit( commitReq );
    REQUIRE( committed );
    const sicnu::data::AssetId id = committed.value();
    const quint64 revisionAfterCommit = catalog.asset( id )->revision().value();
    const quint64 generationAfterCommit = catalog.catalogGeneration();

    const auto first = committer.reconcilePublished( rasterRequest( stable ) );
    REQUIRE( first );
    CHECK( first.value() == id );
    const auto second = committer.reconcilePublished( rasterRequest( stable ) );
    REQUIRE( second );
    CHECK( second.value() == id );

    // Unchanged hit: no revision advance, no catalog mutation, provenance
    // neither dropped nor rewritten.
    const auto after = catalog.asset( id );
    REQUIRE( after.has_value() );
    CHECK( after->revision().value() == revisionAfterCommit );
    CHECK( catalog.catalogGeneration() == generationAfterCommit );
    CHECK( after->state() == sicnu::data::AssetState::Ready );
    CHECK( catalog.provenance( id ).has_value() );
}

TEST_CASE( "R31-T01 WP04: reconcile repairs a missing derivation record",
           "[r31][t01][output_recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString stable = dir.filePath( "out.tif" );
    writeRaster( stable, 0.5f );

    sicnu::data::DataManager catalog;
    sicnu::OutputCommitter committer( &catalog );

    // A directly-registered artifact: no derivation record (the state an
    // interrupted or legacy publication leaves behind).
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = stable;
    sicnu::data::RegisterRequest registration;
    registration.source = source;
    registration.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    const auto registered = catalog.registerSource( registration );
    REQUIRE( !registered.assetId.isNull() );
    const sicnu::data::AssetId id = registered.assetId;
    CHECK( catalog.asset( id )->state() == sicnu::data::AssetState::Ready );
    CHECK_FALSE( catalog.provenance( id ).has_value() );

    const auto reconciled = committer.reconcilePublished( rasterRequest( stable ) );
    REQUIRE( reconciled );
    CHECK( reconciled.value() == id );
    CHECK( catalog.provenance( id ).has_value() );
    CHECK( catalog.asset( id )->state() == sicnu::data::AssetState::Ready );
}

TEST_CASE( "R31-T01 WP04: reconcile refuses a kind mismatch under one path",
           "[r31][t01][output_recovery]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString stable = dir.filePath( "out.gpkg" );
    writeVector( stable );

    sicnu::data::DataManager catalog;
    sicnu::OutputCommitter committer( &catalog );

    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "ogr" );
    source.canonicalSource = stable;
    sicnu::data::RegisterRequest registration;
    registration.source = source;
    registration.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    const auto registered = catalog.registerSource( registration );
    REQUIRE( !registered.assetId.isNull() );
    const sicnu::data::AssetId id = registered.assetId;
    REQUIRE( catalog.asset( id )->kind() == sicnu::data::AssetKind::Vector );

    // The path now holds a raster while the catalog record describes a
    // vector: refuse instead of reusing the wrong record or forking a
    // second asset onto the same path.
    REQUIRE( QFile::remove( stable ) );
    writeRaster( stable, 0.5f );

    const auto reconciled = committer.reconcilePublished( rasterRequest( stable ) );
    REQUIRE_FALSE( reconciled );
    const auto &diagnostics = reconciled.diagnostics();
    REQUIRE_FALSE( diagnostics.isEmpty() );
    CHECK( diagnostics.first().code
           == QStringLiteral( "output.reconcile_kind_changed" ) );
    // The wrong-kind record survives untouched (no silent re-registration).
    CHECK( catalog.asset( id )->kind() == sicnu::data::AssetKind::Vector );
}

TEST_CASE( "R31-T01 WP04: manifest-carrying artifacts prove content on a marker hit",
           "[r31][t01][output_recovery][chunked]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString tempOut = dir.filePath( "artifact.temp.tif" );
    const QString stableOut = dir.filePath( "artifact.tif" );
    const QString workDir = dir.filePath( "work" );
    const std::string stateBase = dir.filePath( "state" ).toStdString() + "/ref";

    sicnu::data::DataManager catalog;
    sicnu::OutputCommitter committer( &catalog );

    ChunkedOutputCommitSpec commitSpec;
    commitSpec.committer = &committer;
    commitSpec.tempPath = tempOut;
    commitSpec.stablePath = stableOut;
    commitSpec.kind = sicnu::data::AssetKind::Raster;
    commitSpec.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
    commitSpec.autoLoad = false;
    commitSpec.derivation.algorithmId = QStringLiteral( "test:chunk_recovery" );
    commitSpec.derivation.algorithmVersion = QStringLiteral( "1.0.0" );

    ChunkedRunOptions options;
    options.commitSpec = commitSpec;
    options.scratchRoot = dir.filePath( "scratch" ).toStdString();
    options.resumeStateBase = stateBase;
    options.publish = [&]() { writeRaster( tempOut, 0.25f ); };

    ChunkTileKernel kernel = []( const TileSpec &s ) { return recoveryKernel( s ); };
    std::function<void( const sicnu::runtime::chunk::TilePayload & )> sink =
        []( const sicnu::runtime::chunk::TilePayload & ) {};

    const auto runOnce = [&]( RSOperatorContext &ctx ) {
        return runChunkedOperator( "test:chunk_recovery",
                                   Json::Value( Json::objectValue ), ctx,
                                   recoveryPartition(), TileRunDeterminism::BitExact,
                                   kernel, sink, options );
    };

    // --- Run 1: full run + commit. No manifest yet (a first publication
    // fabricates no provenance sidecar).
    RSOperatorContext ctx1( workDir.toStdString() );
    const auto r1 = runOnce( ctx1 );
    REQUIRE_FALSE( r1.alreadyPublished );
    REQUIRE( r1.tilesComputed > 0 );
    REQUIRE_FALSE( r1.publishedAssetId.isEmpty() );
    const QString assetA = r1.publishedAssetId;
    const auto assetIdA = sicnu::data::AssetId::fromString( assetA );
    REQUIRE( assetIdA.has_value() );
    const quint64 revisionAfterRun1 = catalog.asset( *assetIdA )->revision().value();

    // --- A finalizing producer's manifest over the standing artifact.
    writeManifestForStandingArtifact( stableOut );

    // WP19: the verification authority must agree with the standing bytes —
    // the manifest the producer wrote describes the artifact it stands for.
    {
      const auto report = sicnu::geo::io::verifyDataset( stableOut.toStdString() );
      CHECK( report.verified );
      CHECK( report.digestMatched );
    }

    // --- Run 2: marker hit whose artifact passes the manifest gate —
    // zero work, same asset, record untouched.
    RSOperatorContext ctx2( workDir.toStdString() );
    const auto r2 = runOnce( ctx2 );
    CHECK( r2.alreadyPublished );
    CHECK( r2.tilesComputed == 0 );
    CHECK( r2.publishedAssetId == assetA );
    CHECK( catalog.asset( *assetIdA )->revision().value() == revisionAfterRun1 );

    // --- Another writer replaced the artifact at the same path with
    // different bytes (same shape): the manifest gate demotes the hit, the
    // run rebuilds and re-commits, and the manifest is refreshed from the
    // new bytes so the NEXT hit is provable again.
    writeRaster( stableOut, 0.9f );
    {
      // WP19: while the foreign bytes stand, the verification authority
      // reports the digest mismatch — "data exists" is not "integrity
      // passes", and nothing may green-wash it.
      const auto report = sicnu::geo::io::verifyDataset( stableOut.toStdString() );
      CHECK_FALSE( report.verified );
      CHECK_FALSE( report.digestMatched );
    }
    RSOperatorContext ctx3( workDir.toStdString() );
    const auto r3 = runOnce( ctx3 );
    CHECK_FALSE( r3.alreadyPublished );
    CHECK( r3.tilesComputed > 0 );
    CHECK( r3.publishedAssetId == assetA ); // same path dedup
    CHECK( catalog.asset( *assetIdA )->revision().value() > revisionAfterRun1 );
    {
      // The rebuild's commit refreshed the manifest from the new bytes:
      // verification passes again — truthfully, not by tolerance.
      const auto report = sicnu::geo::io::verifyDataset( stableOut.toStdString() );
      CHECK( report.verified );
      CHECK( report.digestMatched );
    }

    // --- Run 4: the refreshed manifest proves the standing bytes.
    RSOperatorContext ctx4( workDir.toStdString() );
    const auto r4 = runOnce( ctx4 );
    CHECK( r4.alreadyPublished );
    CHECK( r4.tilesComputed == 0 );
    CHECK( r4.publishedAssetId == assetA );

    // --- E2E-5 flavor: the artifact disappears (catalog record flagged
    // non-Ready) and the SAME bytes come back. The marker hit must
    // converge the record to Ready under the same Asset ID — the usable
    // state a full run produces — not serve the stale record.
    const QString restored = dir.filePath( "restore.tif" );
    writeRaster( restored, 0.25f );
    REQUIRE( QFile::remove( stableOut ) );
    REQUIRE( static_cast<bool>( catalog.notifyExternalContentChange( *assetIdA ) ) );
    REQUIRE( catalog.asset( *assetIdA )->state() != sicnu::data::AssetState::Ready );
    REQUIRE( QFile::copy( restored, stableOut ) );

    RSOperatorContext ctx5( workDir.toStdString() );
    const auto r5 = runOnce( ctx5 );
    CHECK( r5.alreadyPublished );
    CHECK( r5.tilesComputed == 0 );
    CHECK( r5.publishedAssetId == assetA );
    const auto recovered = catalog.asset( *assetIdA );
    REQUIRE( recovered.has_value() );
    CHECK( recovered->state() == sicnu::data::AssetState::Ready );
    CHECK( catalog.provenance( *assetIdA ).has_value() );
}
