/***************************************************************************
  tests/test_swarm_fix_data.cpp
  swarm-review 2026-11-04 — DAT-1..DAT-7 regression anchors (data + dataset).
  ---------------------------
  Each case pins one finding from SWARM_REVIEW.md against the owning module.
  Fixtures are generated at runtime (repo convention: no committed geodata);
  driver-gated cases self-skip with a logged reason.

    DAT-1  a check switched off by a non-positive threshold is dropped from
           auditedChecks — the default config no longer reports the spatial
           distance/buffer checks as "audited and clean" (per fold too).
    DAT-2  the provider's VRT publish goes through the single sidecar write
           authority: byte-complete artifact, no staging residue, and a
           restore regenerates a torn file in place.
    DAT-3  PatchGenerator refuses rotated / degenerate geotransforms at the
           entry instead of silently mis-placing anchor windows and swallowing
           the footprint refusal into an empty WKT string.
    DAT-4  GroupKFold gives ungrouped samples their own groups (keyTable
           fallback) instead of collapsing them into one never-crossing
           pseudo-group.
    DAT-5  a kind↔payload mismatch serializes as an empty payload plus a
           typed diagnostic instead of throwing std::bad_variant_access.
    DAT-6  GovernanceStore fails closed when the schema-version probe errors
           (an unreadable store is an error, never a blank one that gets
           re-stamped with '1').
    DAT-7  ArtifactStore: the version head is derived INSIDE the write
           transaction (concurrent registrations of one logical key take
           distinct versions) and closed-store calls stay clean.

  Not anchored here (by design):
    DAT-7's read/write lifecycle lock is a concurrency invariant — the
           deterministic part is that every member call fails cleanly on a
           closed store; the race itself is covered by the pattern the three
           sibling stores already carry (and by TSan runs).
***************************************************************************/

#include "data/artifact_store.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/governance/governance_store.h"
#include "data/providers/virtual_raster_source_provider.h"
#include "data/source_descriptor.h"
#include "dataset/dataset_qa_report.h"
#include "dataset/fold_audit.h"
#include "dataset/leakage_audit.h"
#include "dataset/patch_generator.h"
#include "dataset/sample.h"
#include "dataset/split.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <atomic>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <cpl_conv.h>
#include <sqlite3.h>

using namespace sicnu::dataset;

namespace
{

// ---------------------------------------------------------------------------
// shared fixtures
// ---------------------------------------------------------------------------

SplitInput splitInput( const QString &id, const QString &groupId = QString() )
{
    SplitInput input;
    input.sampleId = id;
    input.groupId = groupId;
    input.classCode = QStringLiteral( "a" );
    return input;
}

AuditSample spacedSample( const QString &id, SplitRole role, double minX, double minY,
                          double maxX, double maxY )
{
    AuditSample sample;
    sample.input.sampleId = id;
    sample.role = role;
    sample.input.validBounds = true;
    sample.input.minX = minX;
    sample.input.minY = minY;
    sample.input.maxX = maxX;
    sample.input.maxY = maxY;
    return sample;
}

/// Two samples 5 ground units apart — a distance/buffer finding the moment
/// either check is armed.
QVector<AuditSample> nearPair()
{
    return { spacedSample( QStringLiteral( "near-a" ), SplitRole::Train, 100.0, 100.0,
                           110.0, 110.0 ),
             spacedSample( QStringLiteral( "near-b" ), SplitRole::Test, 105.0, 100.0,
                           115.0, 110.0 ) };
}

SampleRecord validPatchRecord( const QString &versionId )
{
    SampleRecord sample;
    sample.setSampleId( QStringLiteral( "patch-1" ) );
    sample.setDatasetVersionId( versionId );
    sample.setKind( SampleKind::Patch );
    PatchSample payload;
    payload.window = PixelWindow{ 0, 0, 32, 32 };
    payload.groundFootprintWkt = QStringLiteral( "POLYGON((0 0, 320 0, 320 320, 0 320, 0 0))" );
    payload.borderPolicy = BorderPolicy::Drop;
    payload.noDataMode = NoDataMode::KeepWithFlag;
    sample.payload() = payload;
    return sample;
}

/// A small north-up GeoTIFF written at runtime (no committed geodata).
QString syntheticRaster( const QString &dir, const QString &name )
{
    const QString path = QDir( dir ).filePath( name );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( driver == nullptr )
        return QString();
    constexpr int W = 16;
    constexpr int H = 16;
    GDALDatasetH ds =
        GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    if ( ds == nullptr )
        return QString();
    double gt[6] = { 500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection(
        ds,
        "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> line( W, 1.0f );
    for ( int row = 0; row < H; ++row )
        GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// DAT-1 — auditedChecks never claims a switched-off check
// ---------------------------------------------------------------------------

TEST_CASE( "DAT-1: the default leakage config drops threshold-off spatial checks "
           "from the audited set",
           "[dataset][leakage][swarm][dat1]" )
{
    // Default config: distanceThreshold = bufferDistance = 0 → both spatial
    // pair checks are switched off below. The pair below WOULD be a finding
    // the moment either threshold is armed, so a report still listing them as
    // audited claims evidence that never ran.
    LeakageAuditConfig config;
    const auto report = LeakageAuditor::audit( QStringLiteral( "version-x" ),
                                               QStringLiteral( "split-x" ), nearPair(),
                                               config );
    REQUIRE( report.has_value() );
    CHECK( report->findings().isEmpty() );
    CHECK_FALSE( report->auditedChecks().contains( QStringLiteral( "distance_below_threshold" ) ) );
    CHECK_FALSE( report->auditedChecks().contains( QStringLiteral( "buffer_overlap" ) ) );
    // The evidence-supported checks that DID run are still claimed.
    CHECK( report->auditedChecks().contains( QStringLiteral( "exact_duplicate" ) ) );
    CHECK_FALSE( report->auditedChecks().isEmpty() );

    // Naming a switched-off check explicitly does not resurrect it: the
    // request is honoured by dropping the claim (and the execution), never by
    // running it on a zero threshold.
    LeakageAuditConfig named = config;
    named.checks = QStringList{ QStringLiteral( "distance_below_threshold" ),
                                QStringLiteral( "buffer_overlap" ) };
    const auto namedReport = LeakageAuditor::audit( QStringLiteral( "version-x" ),
                                                    QStringLiteral( "split-x" ), nearPair(),
                                                    named );
    REQUIRE( namedReport.has_value() );
    CHECK( namedReport->auditedChecks().isEmpty() );
    CHECK( namedReport->findings().isEmpty() );
}

TEST_CASE( "DAT-1: an armed threshold claims the check and reports the planted pair",
           "[dataset][leakage][swarm][dat1]" )
{
    // Positive control: with a threshold the check runs, is claimed, and
    // finds the pair — the drop above is the switched-off case, not a
    // blanket removal.
    LeakageAuditConfig config;
    config.distanceThreshold = 50.0;
    const auto report = LeakageAuditor::audit( QStringLiteral( "version-x" ),
                                               QStringLiteral( "split-x" ), nearPair(),
                                               config );
    REQUIRE( report.has_value() );
    CHECK( report->auditedChecks().contains( QStringLiteral( "distance_below_threshold" ) ) );
    CHECK_FALSE( report->auditedChecks().contains( QStringLiteral( "buffer_overlap" ) ) );
    REQUIRE( report->findings().size() == 1 );
    CHECK( report->findings().first().kind == LeakageKind::DistanceBelowThreshold );
}

TEST_CASE( "DAT-1: fold audits under the default config make the same honest claim",
           "[dataset][fold][leakage][swarm][dat1]" )
{
    // FoldAuditor re-runs the auditor per materialization with the caller's
    // config; the default config used to claim both spatial checks per fold.
    SplitConfig config;
    config.method = SplitMethod::GroupKFold;
    config.seed = 7;
    config.foldCount = 3;
    QVector<SplitInput> inputs;
    for ( int i = 0; i < 9; ++i )
        inputs.append( splitInput( QStringLiteral( "s-%1" ).arg( i ),
                                   QStringLiteral( "g-%1" ).arg( i % 3 ) ) );
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "version-x" ), inputs );
    REQUIRE( manifest.has_value() );

    const auto summary = FoldAuditor::auditFolds( manifest.value(), inputs );
    REQUIRE( summary.has_value() );
    REQUIRE( summary.value().folds.size() == 3 );
    for ( const FoldAuditItem &fold : summary.value().folds )
    {
        INFO( "fold " << fold.fold );
        CHECK_FALSE( fold.report.auditedChecks().contains( QStringLiteral( "distance_below_threshold" ) ) );
        CHECK_FALSE( fold.report.auditedChecks().contains( QStringLiteral( "buffer_overlap" ) ) );
        CHECK( fold.report.auditedChecks().contains( QStringLiteral( "exact_duplicate" ) ) );
        // The verdict surface reads Pass only off the checks that actually
        // ran — the claim set and the run set are the same set again.
        CHECK( verdictFromLeakageReport( fold.report ) == AuditVerdict::Pass );
    }
}

// ---------------------------------------------------------------------------
// DAT-3 — patch generator refuses rotated / degenerate geotransforms
// ---------------------------------------------------------------------------

TEST_CASE( "DAT-3: patch generation refuses rotated and degenerate geotransforms",
           "[dataset][patch][swarm][dat3]" )
{
    PatchGeneratorConfig config;
    config.strategy = PatchStrategy::FixedGrid;
    config.windowWidth = 32;
    config.windowHeight = 32;
    config.strideX = 32;
    config.strideY = 32;

    GeoTransform rotated;
    rotated.values = { 500000.0, 10.0, 0.5, 4000000.0, 0.25, -10.0 };
    const auto rotatedResult =
        PatchGenerator::generate( config, 100, 100, rotated,
                                  PatchGenerator::ValidFractionReader() );
    REQUIRE_FALSE( rotatedResult.has_value() );
    CHECK( rotatedResult.diagnostics().first().code ==
           QStringLiteral( "dataset.transform_rotated" ) );

    // Degenerate pixel size: the anchor strategies used to divide by 1.0 and
    // place every window at a nonsense position.
    GeoTransform degenerate;
    degenerate.values = { 500000.0, 0.0, 0.0, 4000000.0, 0.0, -10.0 };
    const auto degenerateResult =
        PatchGenerator::generate( config, 100, 100, degenerate,
                                  PatchGenerator::ValidFractionReader() );
    REQUIRE_FALSE( degenerateResult.has_value() );
    CHECK( degenerateResult.diagnostics().first().code ==
           QStringLiteral( "dataset.patch_config_invalid" ) );

    // The same refusal covers the anchor strategies (their ground→pixel
    // division is what silently mis-placed the windows).
    PatchGeneratorConfig anchored = config;
    anchored.strategy = PatchStrategy::RoiCentered;
    PatchAnchor anchor;
    anchor.sampleId = QStringLiteral( "roi-1" );
    anchor.centerX = 500100.0;
    anchor.centerY = 3999900.0;
    const auto anchorResult = PatchGenerator::generate( anchored, 100, 100, degenerate,
                                                        PatchGenerator::ValidFractionReader(),
                                                        QVector<PatchAnchor>{ anchor } );
    REQUIRE_FALSE( anchorResult.has_value() );
    CHECK( anchorResult.diagnostics().first().code ==
           QStringLiteral( "dataset.patch_config_invalid" ) );
}

TEST_CASE( "DAT-3: a valid north-up transform still produces footprint-bearing patches",
           "[dataset][patch][swarm][dat3]" )
{
    PatchGeneratorConfig config;
    config.strategy = PatchStrategy::FixedGrid;
    config.windowWidth = 32;
    config.windowHeight = 32;
    config.strideX = 32;
    config.strideY = 32;

    GeoTransform transform;
    transform.values = { 500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };
    const auto patches = PatchGenerator::generate(
        config, 100, 100, transform, PatchGenerator::ValidFractionReader() );
    REQUIRE( patches.has_value() );
    int kept = 0;
    for ( const GeneratedPatch &patch : patches.value() )
    {
        if ( patch.dropped )
            continue; // border-dropped specs carry no window, hence no footprint
        ++kept;
        // No kept patch may carry an empty footprint: the refusal path that
        // used to flatten groundFootprintForWindow's error into "" is gone.
        CHECK_FALSE( patch.groundFootprintWkt.isEmpty() );
    }
    CHECK( kept == 9 ); // 3x3 full windows inside the 100x100 extent
}

// ---------------------------------------------------------------------------
// DAT-4 — GroupKFold does not collapse empty groupIds into one pseudo-group
// ---------------------------------------------------------------------------

TEST_CASE( "DAT-4: GroupKFold gives ungrouped samples their own groups",
           "[dataset][split][swarm][dat4]" )
{
    SplitConfig config;
    config.method = SplitMethod::GroupKFold;
    config.seed = 11;
    config.foldCount = 3;

    QVector<SplitInput> inputs;
    for ( int i = 0; i < 9; ++i )
        inputs.append( splitInput( QStringLiteral( "u-%1" ).arg( i ) ) ); // no groupId

    const auto manifest = SplitEngine::generate( config, QStringLiteral( "version-x" ), inputs );
    REQUIRE( manifest.has_value() );
    QMap<int, int> perFold;
    for ( const SplitAssignment &assignment : manifest.value().assignments() )
        perFold[assignment.fold]++;
    // Every fold must hold samples: a single "" pseudo-group would have put
    // all nine in one fold and left the others empty.
    REQUIRE( perFold.size() == 3 );
    for ( int fold = 0; fold < 3; ++fold )
    {
        INFO( "fold " << fold );
        CHECK( perFold.value( fold ) > 0 );
    }

    // Mixed evidence: grouped samples keep their group atomicity, ungrouped
    // ones are splittable (the keyTable fallback), and the group count used
    // by the "at least as many groups as folds" gate counts them.
    QVector<SplitInput> mixed;
    mixed.append( splitInput( QStringLiteral( "m-0" ), QStringLiteral( "grp-a" ) ) );
    mixed.append( splitInput( QStringLiteral( "m-1" ), QStringLiteral( "grp-a" ) ) );
    mixed.append( splitInput( QStringLiteral( "m-2" ), QStringLiteral( "grp-b" ) ) );
    mixed.append( splitInput( QStringLiteral( "m-3" ) ) );
    mixed.append( splitInput( QStringLiteral( "m-4" ) ) );
    mixed.append( splitInput( QStringLiteral( "m-5" ) ) );
    const auto mixedManifest =
        SplitEngine::generate( config, QStringLiteral( "version-x" ), mixed );
    REQUIRE( mixedManifest.has_value() );
    const auto foldOf = [ &mixedManifest ]( const QString &id ) {
        for ( const SplitAssignment &assignment : mixedManifest.value().assignments() )
            if ( assignment.sampleId == id )
                return assignment.fold;
        return -1;
    };
    CHECK( foldOf( QStringLiteral( "m-0" ) ) == foldOf( QStringLiteral( "m-1" ) ) ); // same group
    bool sawUngroupedSplit = false;
    const QVector<QString> ungrouped{ QStringLiteral( "m-3" ), QStringLiteral( "m-4" ),
                                      QStringLiteral( "m-5" ) };
    for ( int i = 0; i < ungrouped.size(); ++i )
        for ( int j = i + 1; j < ungrouped.size(); ++j )
            if ( foldOf( ungrouped.at( i ) ) != foldOf( ungrouped.at( j ) ) )
                sawUngroupedSplit = true;
    CHECK( sawUngroupedSplit );
}

// ---------------------------------------------------------------------------
// DAT-5 — kind↔payload mismatch is reported, never thrown
// ---------------------------------------------------------------------------

TEST_CASE( "DAT-5: a kind/payload mismatch serializes as an empty payload plus a diagnostic",
           "[dataset][sample][swarm][dat5]" )
{
    // A default-constructed record is kind=Point over variant=monostate;
    // toJson() used to throw std::bad_variant_access here.
    SampleRecord sample;
    sample.setSampleId( QStringLiteral( "s-1" ) );
    sample.setDatasetVersionId( QStringLiteral( "version-1" ) );
    const QJsonObject json = sample.toJson();
    CHECK( json.value( QStringLiteral( "kind" ) ).toString() == QStringLiteral( "point" ) );
    CHECK( json.value( QStringLiteral( "payload" ) ).toObject().isEmpty() );
    const QJsonArray errors = json.value( QStringLiteral( "payload_errors" ) ).toArray();
    REQUIRE( errors.size() == 1 );
    CHECK( errors.first().toObject().value( QStringLiteral( "code" ) ).toString() ==
           QStringLiteral( "dataset.sample_invalid" ) );

    // Same for a mismatched kind over a valid payload.
    SampleRecord mismatched = validPatchRecord( QStringLiteral( "version-1" ) );
    mismatched.setKind( SampleKind::Pair );
    const QJsonObject mismatchJson = mismatched.toJson();
    CHECK( mismatchJson.value( QStringLiteral( "payload" ) ).toObject().isEmpty() );
    CHECK( mismatchJson.value( QStringLiteral( "payload_errors" ) ).toArray().size() == 1 );

    // Valid records are unchanged: no error key, and the round-trip still
    // reproduces the record byte-for-byte.
    SampleRecord good = validPatchRecord( QStringLiteral( "version-1" ) );
    REQUIRE( validateSample( good ).has_value() );
    const QJsonObject goodJson = good.toJson();
    CHECK_FALSE( goodJson.contains( QStringLiteral( "payload_errors" ) ) );
    const auto parsed = SampleRecord::fromJson( goodJson );
    REQUIRE( parsed.has_value() );
    CHECK( parsed.value() == good );
}

// ---------------------------------------------------------------------------
// DAT-2 — the VRT publish goes through the sidecar write authority
// ---------------------------------------------------------------------------

TEST_CASE( "DAT-2: the virtual raster artifact is published byte-complete through the "
           "sidecar authority",
           "[data][virtual_raster][swarm][dat2]" )
{
    GDALAllRegister();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString rasterPath = syntheticRaster( dir.path(), QStringLiteral( "input.tif" ) );
    REQUIRE_FALSE( rasterPath.isEmpty() );

    sicnu::data::DataManager manager;
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = rasterPath;
    sicnu::data::RegisterRequest request;
    request.source = source;
    const sicnu::data::RegisterResult registered = manager.registerSource( request );
    REQUIRE_FALSE( registered.assetId.isNull() );

    sicnu::data::VirtualRasterRecipe recipe;
    recipe.inputs = { sicnu::data::BandRef{ registered.assetId, 1 } };
    const sicnu::data::Result<sicnu::data::AssetId> created =
        manager.createVirtualRaster( recipe );
    REQUIRE( created.has_value() );

    const std::optional<sicnu::data::AssetSnapshot> snapshot = manager.asset( created.value() );
    REQUIRE( snapshot.has_value() );
    const QString vrtPath = snapshot->source().canonicalSource;
    CHECK( vrtPath.endsWith( QStringLiteral( ".vrt" ) ) );

    // The published bytes are exactly what the generator produced — a
    // truncating QFile write (or a torn one) could not guarantee that.
    const std::optional<sicnu::data::AssetSnapshot> inputSnapshot =
        manager.asset( registered.assetId );
    REQUIRE( inputSnapshot.has_value() );
    const QString expectedXml = sicnu::data::providers::buildVirtualRasterXml(
        recipe, QVector<sicnu::data::AssetSnapshot>{ *inputSnapshot } );
    REQUIRE_FALSE( expectedXml.isEmpty() );

    QFile vrt( vrtPath );
    REQUIRE( vrt.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const QString published = QString::fromUtf8( vrt.readAll() );
    vrt.close();
    CHECK( published == expectedXml );

    // No staging residue: the sidecar authority cleans up its temp file, and
    // a hand-rolled writer's ".tmp" leftovers would show up here.
    const QStringList entries = QDir( QFileInfo( vrtPath ).absolutePath() ).entryList( QDir::Files );
    for ( const QString &entry : entries )
        CHECK_FALSE( entry.contains( QStringLiteral( ".tmp" ) ) );

    // A torn artifact is REGENERATED in place: unload the virtual asset and
    // restore it from the same recipe. The restore resolves through the
    // provider, which re-publishes the .vrt at the deterministic path through
    // the sidecar authority — a truncating writer would have left the torn
    // bytes on disk for the next reader.
    {
        QFile truncate( vrtPath );
        REQUIRE( truncate.open( QIODevice::WriteOnly | QIODevice::Text ) );
        truncate.write( "<VRTDataset rasterXSize=\"16\" rasterYSize=\"8\">" );
        truncate.close();
    }
    const auto plan = manager.planUnload( created.value() );
    REQUIRE( plan.canUnload() );
    REQUIRE( manager.unload( plan ).has_value() );

    sicnu::data::RestoreVirtualRasterRequest restore;
    restore.id = created.value();
    restore.recipe = recipe;
    const sicnu::data::Result<sicnu::data::AssetId> restored =
        manager.restoreVirtualRaster( restore );
    REQUIRE( restored.has_value() );

    QFile reread( vrtPath );
    REQUIRE( reread.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const QString republished = QString::fromUtf8( reread.readAll() );
    reread.close();
    CHECK( republished == expectedXml );
}

// ---------------------------------------------------------------------------
// DAT-6 — an unreadable governance schema fails closed
// ---------------------------------------------------------------------------

TEST_CASE( "DAT-6: GovernanceStore fails closed when the schema version cannot be read",
           "[data][governance][swarm][dat6]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString dbPath = dir.filePath( QStringLiteral( "governance.sqlite" ) );

    // Baseline: a fresh store opens and stamps its schema version.
    {
        sicnu::workspace::GovernanceStore store;
        QString err;
        REQUIRE( store.open( dbPath, &err ) );
        CHECK( store.schemaVersion() == QLatin1String( "1" ) );
    }
    // A newer stamp still opens read-only (forward tolerance preserved).
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                                  nullptr ) == SQLITE_OK );
        char *err = nullptr;
        REQUIRE( sqlite3_exec( raw, "UPDATE gov_meta SET value='2' WHERE key='schema_version'",
                               nullptr, nullptr, &err ) == SQLITE_OK );
        sqlite3_free( err );
        sqlite3_close( raw );
    }
    {
        sicnu::workspace::GovernanceStore store;
        QString err;
        REQUIRE( store.open( dbPath, &err ) );
        CHECK( store.isReadOnly() );
        CHECK( store.schemaVersion() == QLatin1String( "2" ) );
    }

    // Now make the schema probe fail: zero the gov_meta data page so the
    // SELECT errors instead of returning a row. The old code treated that as
    // "old database" and createSchema's INSERT OR REPLACE stamped '1' over a
    // store whose version it could not read — the store must refuse instead.
    int rootPage = 0;
    int pageSize = 0;
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                                  nullptr ) == SQLITE_OK );
        sqlite3_stmt *stmt = nullptr;
        REQUIRE( sqlite3_prepare_v2( raw, "SELECT rootpage FROM sqlite_master WHERE type='table'"
                                          " AND name='gov_meta'", -1, &stmt, nullptr ) ==
                 SQLITE_OK );
        if ( sqlite3_step( stmt ) == SQLITE_ROW )
            rootPage = sqlite3_column_int( stmt, 0 );
        sqlite3_finalize( stmt );
        REQUIRE( sqlite3_prepare_v2( raw, "PRAGMA page_size", -1, &stmt, nullptr ) == SQLITE_OK );
        if ( sqlite3_step( stmt ) == SQLITE_ROW )
            pageSize = sqlite3_column_int( stmt, 0 );
        sqlite3_finalize( stmt );
        sqlite3_close( raw );
    }
    REQUIRE( rootPage > 1 );
    REQUIRE( pageSize > 0 );
    {
        QFile db( dbPath );
        REQUIRE( db.open( QIODevice::ReadWrite ) );
        REQUIRE( db.seek( static_cast<qint64>( rootPage - 1 ) * pageSize ) );
        const QByteArray zeroed( pageSize, '\0' );
        REQUIRE( db.write( zeroed ) == zeroed.size() );
        db.close();
    }

    sicnu::workspace::GovernanceStore store;
    QString err;
    CHECK_FALSE( store.open( dbPath, &err ) );
    CHECK( !err.isEmpty() );
    // Whatever phase failed, the store must not come back up claiming a
    // version it never read.
    CHECK_FALSE( store.isOpen() );
    CHECK( store.schemaVersion().isEmpty() );
}

// ---------------------------------------------------------------------------
// DAT-7 — artifact store: in-transaction version head + clean closed calls
// ---------------------------------------------------------------------------

TEST_CASE( "DAT-7: concurrent registrations of one logical key take distinct versions",
           "[data][artifact_store][swarm][dat7]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString payload = dir.filePath( QStringLiteral( "payload.tif" ) );
    {
        QFile f( payload );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "payload-bytes" );
    }
    const QString dbPath = dir.filePath( QStringLiteral( "artifacts.sqlite" ) );

    // Two independent stores on the same database: the per-store mutex cannot
    // serialize them, so the MAX(version) head must be derived INSIDE the
    // write transaction or both registrations compute the same version and
    // one dies on PRIMARY KEY(logical_key, version).
    sicnu::data::ArtifactStore storeA;
    sicnu::data::ArtifactStore storeB;
    QString err;
    REQUIRE( storeA.open( dbPath, &err ) );
    REQUIRE( storeB.open( dbPath, &err ) );

    const auto registration = [ &payload ] {
        sicnu::data::ArtifactRegistration reg;
        reg.logicalKey = QStringLiteral( "concurrent/key" );
        reg.storagePath = payload;
        reg.kind = QStringLiteral( "raster" );
        return reg;
    };

    std::atomic<bool> go{ false };
    std::atomic<int> failures{ 0 };
    sicnu::data::ArtifactRecord recordA;
    sicnu::data::ArtifactRecord recordB;
    auto worker = [ &go, &failures, &registration ]( sicnu::data::ArtifactStore &store,
                                                     sicnu::data::ArtifactRecord &out ) {
        while ( !go.load() )
            std::this_thread::yield();
        const auto result = store.registerArtifact( registration() );
        if ( !result )
        {
            ++failures;
            return;
        }
        out = result.value();
    };
    std::thread threadA( worker, std::ref( storeA ), std::ref( recordA ) );
    std::thread threadB( worker, std::ref( storeB ), std::ref( recordB ) );
    go.store( true );
    threadA.join();
    threadB.join();

    CHECK( failures.load() == 0 );
    CHECK( recordA.version != recordB.version );
    CHECK( recordA.version + recordB.version == 3 ); // versions 1 and 2
    CHECK( storeA.versionsByLogicalKey( QStringLiteral( "concurrent/key" ) ).size() == 2 );
    CHECK( storeB.latestByLogicalKey( QStringLiteral( "concurrent/key" ) )->version == 2 );
}

TEST_CASE( "DAT-7: artifact-store member calls on a closed store stay clean",
           "[data][artifact_store][swarm][dat7]" )
{
    sicnu::data::ArtifactStore store;
    CHECK_FALSE( store.isOpen() );
    CHECK_FALSE( store.artifactById( QStringLiteral( "x" ) ).has_value() );
    CHECK_FALSE( store.latestByLogicalKey( QStringLiteral( "x" ) ).has_value() );
    CHECK( store.versionsByLogicalKey( QStringLiteral( "x" ) ).isEmpty() );
    CHECK( store.refCount( QStringLiteral( "x" ) ) == 0 );
    CHECK( store.refsOf( QStringLiteral( "x" ) ).isEmpty() );
    CHECK( store.reapable( 0 ).isEmpty() );
    CHECK( store.count() == 0 );
    CHECK( store.schemaVersion().isEmpty() );
    CHECK_FALSE( store.stepFailed() );
    CHECK_FALSE( store.touch( QStringLiteral( "x" ) ) );
    CHECK_FALSE( store.registerArtifact( sicnu::data::ArtifactRegistration{} ).has_value() );

    // Open, exercise, then close: the lifecycle lock makes the transition
    // exclusive, so no member can observe a half-closed connection.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString payload = dir.filePath( QStringLiteral( "payload.tif" ) );
    {
        QFile f( payload );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        f.write( "payload-bytes" );
    }
    QString err;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "artifacts.sqlite" ) ), &err ) );
    CHECK( store.isOpen() );
    sicnu::data::ArtifactRegistration reg;
    reg.logicalKey = QStringLiteral( "k" );
    reg.storagePath = payload;
    reg.kind = QStringLiteral( "raster" );
    const auto registered = store.registerArtifact( reg );
    REQUIRE( registered.has_value() );
    store.close();
    CHECK_FALSE( store.isOpen() );
    CHECK( store.count() == 0 );
    CHECK_FALSE( store.artifactById( registered.value().artifactId ).has_value() );
}
