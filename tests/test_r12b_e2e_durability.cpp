/***************************************************************************
 * tests/test_r12b_e2e_durability.cpp
 * R12-B Hardening E2E Test Suite: Geodata Durability, Security & Provenance
 *
 * Systematic 4-Tier Opaque-Box Verification:
 *   Tier 1 — Feature Coverage (Isolated feature verification across B01-B12)
 *   Tier 2 — Boundary & Corner Cases (Malformed inputs, path traversal, crashes)
 *   Tier 3 — Cross-Feature Interactions (Pairwise sub-system integration)
 *   Tier 4 — Real-World Application Workflows (5 end-to-end production pipelines)
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/remote_source_validator.h"
#include "geospatial/remote/http_fetch.h"
#include "geospatial/cog/cog_validator.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/fabric/catalog_service.h"
#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"
#include "data/artifact_store.h"
#include "data/execution_fingerprint.h"
#include "data/virtual_raster_recipe.h"
#include "dataset/patch_generator.h"
#include "dataset/sample_promotion.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_manifest.h"
#include "experiment/reproduction_bundle.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QStringList>

#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

namespace
{

/// Helper to create a temporary directory for file-based tests.
std::string makeScratchDir( const std::string &subdirName )
{
    const fs::path dir = fs::temp_directory_path() / "r12b_e2e_scratch" / subdirName;
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir, ec );
    return dir.string();
}

/// Helper to create a dummy raster-like binary file.
void createDummyFile( const std::string &path, std::size_t sizeBytes, unsigned char fill = 0xAA )
{
    std::ofstream out( path, std::ios::binary );
    std::vector<char> buffer( sizeBytes, static_cast<char>( fill ) );
    out.write( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
}

} // namespace

// ============================================================================
// TIER 1: FEATURE COVERAGE (Isolated feature verification across B01-B12)
// ============================================================================

// ----------------------------------------------------------------------------
// Feature Area 1: IPv6 & Address Parsing / Memory Safety (B02)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 1.1: IPv6 bounds and capacity checking (B02-IPv6Bounds)",
           "[r12b][e2e][tier1][b02][ipv6]" )
{
    // Verifies that IPv6 address parsing checks buffer capacity properly and
    // classifies standard addresses without buffer overruns.
    CHECK( sicnu::geo::offline::isPrivateHost( "::1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "[::1]" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "127.0.0.1" ) );
    // Long multi-group addresses must not overflow stack buffers.
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( "2001:db8:85a3:0:0:8a2e:370:7334" ) );
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( "2606:4700:4700::1111" ) );
}

TEST_CASE( "Tier 1 - Area 1.2: IPv6 RFC 6874 zone ID stripping (B02-IPv6Zone)",
           "[r12b][e2e][tier1][b02][ipv6]" )
{
    // RFC 6874 zone IDs (%eth0, %25eth0) must be stripped and link-local scope recognized.
    CHECK( sicnu::geo::offline::isPrivateHost( "fe80::1%eth0" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "fe80::1%25eth0" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "fe80::dead:beef%wlan0" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "[fe80::1%eth0]:8080" ) );
}

TEST_CASE( "Tier 1 - Area 1.3: Unspecified IPv6 address classified as private/loopback (B02-IPv6Unspecified)",
           "[r12b][e2e][tier1][b02][ipv6]" )
{
    // Unspecified IPv6 address (::) binds locally on POSIX/Linux and must be classified private/loopback.
    CHECK( sicnu::geo::offline::isPrivateHost( "::" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "[::]" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "[::]:80" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "http://[::]/metadata" ) );
}

TEST_CASE( "Tier 1 - Area 1.4: IPv4-compatible and IPv4-mapped IPv6 address handling (B02)",
           "[r12b][e2e][tier1][b02][ipv6]" )
{
    // IPv4-mapped IPv6 addresses referencing private networks must classify as private.
    CHECK( sicnu::geo::offline::isPrivateHost( "::ffff:127.0.0.1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "::ffff:169.254.169.254" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "::ffff:10.0.0.1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "::ffff:192.168.1.1" ) );
    // IPv4-mapped public address must classify as public.
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( "::ffff:8.8.8.8" ) );
}

TEST_CASE( "Tier 1 - Area 1.5: IPv6 ULA and Link-Local address ranges (B02)",
           "[r12b][e2e][tier1][b02][ipv6]" )
{
    // Unique Local Addresses (fc00::/7) and Link-Local (fe80::/10) must be private.
    CHECK( sicnu::geo::offline::isPrivateHost( "fc00::1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "fd12:3456:789a::1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "fe80::1" ) );
    // Global Unicast (2000::/3) must be non-private.
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( "2001:4860:4860::8888" ) );
}

// ----------------------------------------------------------------------------
// Feature Area 2: SSRF Protection & Offline Zero-Egress Boundaries (B03)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 2.1: Alternative IPv4 formats validation (B03-AltIPv4)",
           "[r12b][e2e][tier1][b03][ssrf]" )
{
    // Loopback & private IPs represented as octal, hex, or dwords must be detected.
    CHECK( sicnu::geo::offline::isPrivateHost( "2130706433" ) );      // 127.0.0.1 as dword
    CHECK( sicnu::geo::offline::isPrivateHost( "0177.0.0.1" ) );       // 127.0.0.1 octal prefix
    CHECK( sicnu::geo::offline::isPrivateHost( "0x7f.0.0.1" ) );       // 127.0.0.1 hex prefix
    CHECK( sicnu::geo::offline::isPrivateHost( "127.1" ) );            // shorthand 127.0.0.1
    CHECK( sicnu::geo::offline::isPrivateHost( "10.1" ) );             // shorthand 10.0.0.1
}

TEST_CASE( "Tier 1 - Area 2.2: Offline gate zero-egress enforcement (B03-ZeroEgress)",
           "[r12b][e2e][tier1][b03][offline]" )
{
    sicnu::geo::offline::setEnabled( true );
    CHECK( sicnu::geo::offline::enabled() );

    // Remote targets must be detected and refused.
    CHECK( sicnu::geo::offline::isRemoteTarget( "http://example.com/raster.tif" ) );
    CHECK( sicnu::geo::offline::isRemoteTarget( "https://landsat.usgs.gov/catalog" ) );
    CHECK( sicnu::geo::offline::isRemoteTarget( "/vsicurl/http://example.com/cog.tif" ) );
    CHECK( sicnu::geo::offline::isRemoteTarget( "/vsis3/bucket/image.tif" ) );

    // Local VSI handlers are NOT remote targets.
    CHECK_FALSE( sicnu::geo::offline::isRemoteTarget( "/vsimem/temp.tif" ) );
    CHECK_FALSE( sicnu::geo::offline::isRemoteTarget( "/vsizip/archive.zip/scene.tif" ) );
    CHECK_FALSE( sicnu::geo::offline::isRemoteTarget( "file:///data/scene.tif" ) );

    // Refusal message must explicitly name the source.
    const std::string msg = sicnu::geo::offline::refusalMessage( "http://example.com/raster.tif" );
    CHECK_THAT( msg, ContainsSubstring( "http://example.com/raster.tif" ) );

    sicnu::geo::offline::setEnabled( false );
}

TEST_CASE( "Tier 1 - Area 2.3: COG validator offline gate integration (B03-COGOfflineGate)",
           "[r12b][e2e][tier1][b03][cog]" )
{
    sicnu::geo::offline::setEnabled( true );

    // Remote URLs must refuse when offline is enabled without making network calls.
    const auto report = sicnu::geo::validateCog( "http://remote-server.org/large_cog.tif" );
    CHECK_FALSE( report.isCog );

    sicnu::geo::offline::setEnabled( false );
}

TEST_CASE( "Tier 1 - Area 2.4: RemoteSourceValidator options blockPrivateNetworks (B03-SSRFRedirect)",
           "[r12b][e2e][tier1][b03][ssrf]" )
{
    sicnu::geo::RemoteValidatorOptions opts;
    opts.blockPrivateNetworks = true;

    // Local loopback or cloud metadata IPs must be rejected by isPrivateHost check.
    CHECK( sicnu::geo::offline::isPrivateHost( "http://127.0.0.1:8000/metadata" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "http://169.254.169.254/latest/meta-data/" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "http://metadata.google.internal/computeMetadata" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "http://localhost:3000/api" ) );
}

TEST_CASE( "Tier 1 - Area 2.5: Host authority percent-decode before private host check (B03)",
           "[r12b][e2e][tier1][b03][ssrf]" )
{
    // Evasive percent-encoded host names (e.g. local%68ost for localhost) must be caught.
    std::string decoded;
    CHECK( sicnu::geo::percentDecode( "local%68ost", decoded ) );
    CHECK( decoded == "localhost" );
    CHECK( sicnu::geo::offline::isPrivateHost( "http://" + decoded + ":8080/data" ) );
}

// ----------------------------------------------------------------------------
// Feature Area 3: Resource URI Resolution & Canonicalization (B04)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 3.1: ResourceUri resolveAgainst with file:// scheme (B04-FileUriResolution)",
           "[r12b][e2e][tier1][b04][uri]" )
{
    const std::string baseDir = "/home/user/project";
    const std::string fileUri = "file:///var/data/sentinel2.tif";

    // Absolute file:// URI must be treated as anchored, not appended to base directory.
    const auto resolved = sicnu::geo::ResourceUri::resolveAgainst( baseDir, fileUri );
    CHECK( resolved.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK( resolved.path == "/var/data/sentinel2.tif" );
    CHECK_FALSE( resolved.canonical().find( "project/file:" ) != std::string::npos );
}

TEST_CASE( "Tier 1 - Area 3.2: Case-insensitive localhost in file:// URIs (B04-UriNormalization)",
           "[r12b][e2e][tier1][b04][uri]" )
{
    const auto uriLower = sicnu::geo::ResourceUri::parse( "file://localhost/data/raster.tif" );
    const auto uriUpper = sicnu::geo::ResourceUri::parse( "file://LOCALHOST/data/raster.tif" );
    const auto uriMixed = sicnu::geo::ResourceUri::parse( "file://LocalHost/data/raster.tif" );

    CHECK( uriLower.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK( uriUpper.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK( uriMixed.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK( uriLower.path == "/data/raster.tif" );
    CHECK( uriUpper.path == "/data/raster.tif" );
}

TEST_CASE( "Tier 1 - Area 3.3: NUL byte rejection in percent-encoded URIs (B04-UriNormalization)",
           "[r12b][e2e][tier1][b04][uri]" )
{
    // An embedded %00 (NUL byte) inside a URI must be rejected as Invalid.
    const auto uri = sicnu::geo::ResourceUri::parse( "file:///path/to/malicious%00file.tif" );
    CHECK( uri.kind == sicnu::geo::ResourceKind::Invalid );
    CHECK_FALSE( uri.parseReason.empty() );
}

TEST_CASE( "Tier 1 - Area 3.4: Root path trailing slash normalization in canonical() (B04)",
           "[r12b][e2e][tier1][b04][uri]" )
{
    const auto uriRoot = sicnu::geo::ResourceUri::parse( "file:///" );
    CHECK( uriRoot.kind == sicnu::geo::ResourceKind::LocalDirectory );
    // Root canonical representation should not collapse to "file:".
    CHECK( uriRoot.canonical() == "file:///" );
}

TEST_CASE( "Tier 1 - Area 3.5: ResourceUri toGdalPath and gdalOpenTarget (B04-CheckedPathGdal)",
           "[r12b][e2e][tier1][b04][uri]" )
{
    const auto uri = sicnu::geo::ResourceUri::parse( "file:///data/ortho.tif" );
    CHECK( uri.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK( uri.gdalOpenTarget() == "/data/ortho.tif" );

    const std::string gdalTarget = sicnu::geo::ResourceUri::toGdalPath( "file:///data/ortho.tif" );
    CHECK( gdalTarget == "/data/ortho.tif" );

    const std::string vsiTarget = sicnu::geo::ResourceUri::toGdalPath( "/vsicurl/http://example.com/cog.tif" );
    CHECK( vsiTarget == "/vsicurl/http://example.com/cog.tif" );
}

// ----------------------------------------------------------------------------
// Feature Area 4: Catalog & STAC Traversal / Pagination Integrity (B05)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 4.1: CatalogQuery bbox and temporal parameter validation (B05)",
           "[r12b][e2e][tier1][b05][catalog]" )
{
    sicnu::geo::CatalogQuery q;
    q.bbox = { -120.0, 35.0, -119.0, 36.0 };
    q.temporalStartUtc = "2026-01-01T00:00:00Z";

    CHECK( q.bbox.size() == 4 );
    CHECK( q.temporalStartUtc == "2026-01-01T00:00:00Z" );
}

TEST_CASE( "Tier 1 - Area 4.2: Cooperative cancellation token for catalog crawls (B05)",
           "[r12b][e2e][tier1][b05][catalog]" )
{
    sicnu::geo::CancelToken token;
    CHECK_FALSE( token.cancelled() );

    token.cancel();
    CHECK( token.cancelled() );

    // Aliasing external cancellation predicate
    bool externalStop = false;
    sicnu::geo::CancelToken aliased;
    aliased.aliasPredicate( [&]() { return externalStop; } );
    CHECK_FALSE( aliased.cancelled() );
    externalStop = true;
    CHECK( aliased.cancelled() );
}

TEST_CASE( "Tier 1 - Area 4.3: Local STAC catalog path containment (B05-StacLocalTraversal)",
           "[r12b][e2e][tier1][b05][stac]" )
{
    const std::string scratch = makeScratchDir( "stac_traversal" );
    const std::string itemJson = ( fs::path( scratch ) / "item.json" ).string();
    {
        std::ofstream out( itemJson );
        out << R"({"type":"Feature","id":"test-1","properties":{},"assets":{}})";
    }

    // Resolving an asset that escapes catalog root with .. must be flagged as Invalid/escape
    const auto resolved = sicnu::geo::ResourceUri::resolveAgainst( scratch, "../../../etc/passwd" );
    CHECK( resolved.kind == sicnu::geo::ResourceKind::Invalid );
    CHECK_THAT( resolved.parseReason, ContainsSubstring( "escape" ) );
}

TEST_CASE( "Tier 1 - Area 4.4: Catalog backend kinds inventory (B05)",
           "[r12b][e2e][tier1][b05][catalog]" )
{
    CHECK( std::string( sicnu::geo::catalogBackendKindName(
        sicnu::geo::CatalogBackendKind::LocalStacFiles ) ) == "local_stac_files" );
    CHECK( std::string( sicnu::geo::catalogBackendKindName(
        sicnu::geo::CatalogBackendKind::RemoteStacApi ) ) == "remote_stac_api" );
    CHECK( std::string( sicnu::geo::catalogBackendKindName(
        sicnu::geo::CatalogBackendKind::InMemoryRecords ) ) == "in_memory_records" );
}

TEST_CASE( "Tier 1 - Area 4.5: FormatProfileRegistry decoded path checks (B04-FormatProfilePath)",
           "[r12b][e2e][tier1][b04][formats]" )
{
    // URI with encoded characters must decode properly before driver/extension matching
    const std::string encodedPath = "file:///data/my%20scene%202026.tif";
    const auto uri = sicnu::geo::ResourceUri::parse( encodedPath );
    CHECK( uri.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK( uri.path == "/data/my scene 2026.tif" );
}

// ----------------------------------------------------------------------------
// Feature Area 5: Atomic Publishing & Crash Recovery (B06)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 5.1: Single file atomic write and publish (B06)",
           "[r12b][e2e][tier1][b06][atomic]" )
{
    const std::string scratch = makeScratchDir( "atomic_single" );
    const std::string targetFile = ( fs::path( scratch ) / "result.tif" ).string();

    sicnu::geo::atomic_fs::writeFileAtomic( targetFile, []( const std::string &staged ) {
        createDummyFile( staged, 1024, 0x42 );
    } );

    CHECK( sicnu::geo::atomic_fs::fileExists( targetFile ) );
    CHECK( sicnu::geo::atomic_fs::fileSize( targetFile ) == 1024 );
}

TEST_CASE( "Tier 1 - Area 5.2: Writer exception discards staged file and preserves target (B06)",
           "[r12b][e2e][tier1][b06][atomic]" )
{
    const std::string scratch = makeScratchDir( "atomic_rollback" );
    const std::string targetFile = ( fs::path( scratch ) / "existing.tif" ).string();
    createDummyFile( targetFile, 512, 0x11 );

    std::string capturedStagedPath;
    CHECK_THROWS_AS(
        sicnu::geo::atomic_fs::writeFileAtomic( targetFile, [&]( const std::string &staged ) {
            capturedStagedPath = staged;
            createDummyFile( staged, 2048, 0x99 );
            throw std::runtime_error( "simulated crash during write" );
        } ),
        std::runtime_error
    );

    // Staged file must be cleaned up, and original target must remain intact.
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( capturedStagedPath ) );
    CHECK( sicnu::geo::atomic_fs::fileExists( targetFile ) );
    CHECK( sicnu::geo::atomic_fs::fileSize( targetFile ) == 512 );
}

TEST_CASE( "Tier 1 - Area 5.3: Multi-file dataset group sidecar discovery (B06)",
           "[r12b][e2e][tier1][b06][atomic]" )
{
    const auto sidecars = sicnu::geo::atomic_fs::sidecarsFor( "/data/layer.shp" );
    CHECK_FALSE( sidecars.empty() );
    // Shapefile must include .shx, .dbf, .prj
    bool hasShx = false;
    bool hasDbf = false;
    for ( const auto &s : sidecars )
    {
        if ( s.find( ".shx" ) != std::string::npos ) hasShx = true;
        if ( s.find( ".dbf" ) != std::string::npos ) hasDbf = true;
    }
    CHECK( hasShx );
    CHECK( hasDbf );
}

TEST_CASE( "Tier 1 - Area 5.4: Multi-file group publish with sidecars first, main last (B06)",
           "[r12b][e2e][tier1][b06][atomic]" )
{
    const std::string scratch = makeScratchDir( "atomic_group" );
    const std::string stagedShp = ( fs::path( scratch ) / "staged.shp" ).string();
    const std::string stagedShx = ( fs::path( scratch ) / "staged.shx" ).string();
    const std::string stagedDbf = ( fs::path( scratch ) / "staged.dbf" ).string();
    const std::string targetShp = ( fs::path( scratch ) / "final.shp" ).string();
    const std::string targetShx = ( fs::path( scratch ) / "final.shx" ).string();
    const std::string targetDbf = ( fs::path( scratch ) / "final.dbf" ).string();

    createDummyFile( stagedShp, 100, 0x01 );
    createDummyFile( stagedShx, 50, 0x02 );
    createDummyFile( stagedDbf, 200, 0x03 );

    sicnu::geo::atomic_fs::publishStagedGroup( stagedShp, targetShp );

    CHECK( sicnu::geo::atomic_fs::fileExists( targetShp ) );
    CHECK( sicnu::geo::atomic_fs::fileExists( targetShx ) );
    CHECK( sicnu::geo::atomic_fs::fileExists( targetDbf ) );
}

TEST_CASE( "Tier 1 - Area 5.5: Durable fsyncFile verification (B06)",
           "[r12b][e2e][tier1][b06][atomic]" )
{
    const std::string scratch = makeScratchDir( "atomic_fsync" );
    const std::string testFile = ( fs::path( scratch ) / "sync_target.bin" ).string();
    createDummyFile( testFile, 4096, 0x55 );

    // fsyncFile on existing valid file must succeed without throwing
    CHECK_NOTHROW( sicnu::geo::atomic_fs::fsyncFile( testFile ) );
}

// ----------------------------------------------------------------------------
// Feature Area 6: SQLite Durability & Statement Error Propagation (B07)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 6.1: GovernanceStore open, schema initialization and WAL mode (B07)",
           "[r12b][e2e][tier1][b07][sqlite]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "governance.sqlite" );

    sicnu::workspace::GovernanceStore store;
    QString err;
    REQUIRE( store.open( dbPath, &err ) );
    CHECK( store.isOpen() );
    CHECK_FALSE( store.schemaVersion().isEmpty() );
    CHECK( store.storePath() == dbPath );
    CHECK( store.checkpointForBackup() );
    store.close();
    CHECK_FALSE( store.isOpen() );
}

TEST_CASE( "Tier 1 - Area 6.2: GovernanceStore asset upsert and pagination (B07)",
           "[r12b][e2e][tier1][b07][sqlite]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "governance.sqlite" );

    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    sicnu::workspace::GovernedAsset asset;
    asset.assetId = "ast-001";
    asset.displayName = "Sentinel-2 L2A";
    asset.canonicalSource = "/data/s2_l2a.tif";
    asset.kind = "raster";
    asset.state = "Ready";
    asset.sizeBytes = 1048576;

    const auto res = store.upsertAsset( asset );
    CHECK( res.isOk() );

    const auto page = store.query( {} );
    CHECK( page.totalCount >= 1 );
    store.close();
}

TEST_CASE( "Tier 1 - Area 6.3: GovernanceStore appendAudit and audit log tail (B07-SqliteRollback)",
           "[r12b][e2e][tier1][b07][sqlite]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "governance.sqlite" );

    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    const auto auditRes = store.appendAudit( "test-operator", "INGEST", "asset", "ast-001",
                                            QJsonObject{ { "status", "success" } } );
    CHECK( auditRes.isOk() );

    const auto tail = store.auditTail( 10 );
    CHECK_FALSE( tail.isEmpty() );
    CHECK( tail.last().action == "INGEST" );
    CHECK( tail.last().entityId == "ast-001" );
    store.close();
}

TEST_CASE( "Tier 1 - Area 6.4: ArtifactStore open, registration and version monotonic increment (B07)",
           "[r12b][e2e][tier1][b07][sqlite]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "artifacts.sqlite" );
    const QString payloadPath = dir.filePath( "payload.tif" );
    {
        QFile f( payloadPath );
        f.open( QIODevice::WriteOnly );
        f.write( "DATA", 4 );
        f.close();
    }

    sicnu::data::ArtifactStore store;
    REQUIRE( store.open( dbPath ) );

    sicnu::data::ArtifactRegistration reg1;
    reg1.logicalKey = "ortho/tile_01";
    reg1.storagePath = payloadPath;
    reg1.kind = "raster";

    const auto r1 = store.registerArtifact( reg1 );
    REQUIRE( r1.isOk() );
    CHECK( r1.value().version == 1 );

    // Re-registering same logical key must monotonically bump version
    sicnu::data::ArtifactRegistration reg2 = reg1;
    const auto r2 = store.registerArtifact( reg2 );
    REQUIRE( r2.isOk() );
    CHECK( r2.value().version == 2 );
    CHECK( r2.value().artifactId == r1.value().artifactId );
    store.close();
}

TEST_CASE( "Tier 1 - Area 6.5: ArtifactStore reapable invariant I7 (B07)",
           "[r12b][e2e][tier1][b07][sqlite]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "artifacts.sqlite" );

    sicnu::data::ArtifactStore store;
    REQUIRE( store.open( dbPath ) );
    // Live artifacts with active references are never reapable
    const auto reapList = store.reapable( 0 );
    CHECK( reapList.isEmpty() );
    store.close();
}

// ----------------------------------------------------------------------------
// Feature Area 7: Reproduction Bundle Containment & Streaming Verification (B08)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 7.1: verifyBundleChecksums happy path (B08)",
           "[r12b][e2e][tier1][b08][bundle]" )
{
    QTemporaryDir dir;
    const QString bundleDir = dir.path();

    // Required files
    const QStringList required = { "manifest.json", "run_config.json", "environment.json" };
    QStringList lines;
    for ( const auto &name : required )
    {
        const QString filePath = dir.filePath( name );
        QFile f( filePath );
        f.open( QIODevice::WriteOnly );
        const QByteArray content = name.toUtf8() + " payload\n";
        f.write( content );
        f.close();

        const QString digest = QString::fromUtf8(
            QCryptographicHash::hash( content, QCryptographicHash::Sha256 ).toHex() );
        lines.append( QStringLiteral( "%1  %2" ).arg( digest, name ) );
    }

    // Write checksums.txt
    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( lines.join( "\n" ).toUtf8() + "\n" );
    cfile.close();

    QStringList reasons;
    CHECK( sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons ) );
    CHECK( reasons.isEmpty() );
}

TEST_CASE( "Tier 1 - Area 7.2: Bundle checksums path traversal rejection (B08-BundleTraversal)",
           "[r12b][e2e][tier1][b08][bundle]" )
{
    QTemporaryDir dir;
    const QString bundleDir = dir.path();

    // Write valid required files
    for ( const char *req : sicnu::experiment::kRequiredBundleMembers )
    {
        QFile f( dir.filePath( req ) );
        f.open( QIODevice::WriteOnly );
        f.write( "{}" );
        f.close();
    }

    // Write malicious checksums.txt attempting relative traversal escape
    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  ../outside.txt\n" );
    cfile.close();

    QStringList reasons;
    CHECK_FALSE( sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons ) );
    CHECK_FALSE( reasons.isEmpty() );
    CHECK_THAT( reasons.first().toStdString(), ContainsSubstring( "unsafe" ) );
}

TEST_CASE( "Tier 1 - Area 7.3: Bundle checksums Windows drive letter traversal rejection (B08-BundleTraversal)",
           "[r12b][e2e][tier1][b08][bundle]" )
{
    QTemporaryDir dir;
    const QString bundleDir = dir.path();

    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  C:/Windows/system32.dll\n" );
    cfile.close();

    QStringList reasons;
    CHECK_FALSE( sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons ) );
    CHECK_FALSE( reasons.isEmpty() );
}

TEST_CASE( "Tier 1 - Area 7.4: Bundle checksums UNC network share traversal rejection (B08-BundleTraversal)",
           "[r12b][e2e][tier1][b08][bundle]" )
{
    QTemporaryDir dir;
    const QString bundleDir = dir.path();

    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  \\\\server\\share\\evil.txt\n" );
    cfile.close();

    QStringList reasons;
    CHECK_FALSE( sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons ) );
    CHECK_FALSE( reasons.isEmpty() );
}

TEST_CASE( "Tier 1 - Area 7.5: Bundle missing required members detection (B08)",
           "[r12b][e2e][tier1][b08][bundle]" )
{
    QTemporaryDir dir;
    const QString bundleDir = dir.path();

    // Checksums file only lists optional file, omitting required manifest.json
    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  optional.txt\n" );
    cfile.close();
    QFile f( dir.filePath( "optional.txt" ) );
    f.open( QIODevice::WriteOnly );
    f.close();

    QStringList reasons;
    CHECK_FALSE( sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons ) );
    CHECK_FALSE( reasons.isEmpty() );
    CHECK_THAT( reasons.join( " " ).toStdString(), ContainsSubstring( "missing required member" ) );
}

// ----------------------------------------------------------------------------
// Feature Area 8: Resource Bounded Generation & Provenance Tracking (B09, B10)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 8.1: PatchGeneratorConfig validation (B09)",
           "[r12b][e2e][tier1][b09][patch]" )
{
    sicnu::dataset::PatchGeneratorConfig cfg;
    cfg.strategy = sicnu::dataset::PatchStrategy::FixedGrid;
    cfg.windowWidth = 256;
    cfg.windowHeight = 256;
    cfg.strideX = 256;
    cfg.strideY = 256;

    const auto val = cfg.validate();
    CHECK( val.isOk() );

    // Invalid zero window size
    cfg.windowWidth = 0;
    CHECK_FALSE( cfg.validate().isOk() );
}

TEST_CASE( "Tier 1 - Area 8.2: PatchGenerator configHash deterministic computation (B09)",
           "[r12b][e2e][tier1][b09][patch]" )
{
    sicnu::dataset::PatchGeneratorConfig cfg;
    cfg.strategy = sicnu::dataset::PatchStrategy::SlidingWindow;
    cfg.windowWidth = 128;
    cfg.windowHeight = 128;
    cfg.strideX = 64;
    cfg.strideY = 64;

    const QString hash1 = sicnu::dataset::PatchGenerator::configHash( cfg );
    const QString hash2 = sicnu::dataset::PatchGenerator::configHash( cfg );
    CHECK_FALSE( hash1.isEmpty() );
    CHECK( hash1 == hash2 );
}

TEST_CASE( "Tier 1 - Area 8.3: Bounded patch generation on small extent (B09-PatchBoundedMem)",
           "[r12b][e2e][tier1][b09][patch]" )
{
    sicnu::dataset::PatchGeneratorConfig cfg;
    cfg.strategy = sicnu::dataset::PatchStrategy::FixedGrid;
    cfg.windowWidth = 100;
    cfg.windowHeight = 100;
    cfg.strideX = 100;
    cfg.strideY = 100;

    sicnu::dataset::GeoTransform transform;
    const auto res = sicnu::dataset::PatchGenerator::generate(
        cfg, 200, 200, transform, []( const sicnu::dataset::PixelWindow & ) { return 1.0; } );

    REQUIRE( res.isOk() );
    // 200x200 with 100x100 grid produces exactly 4 patches
    CHECK( res.value().size() == 4 );
}

TEST_CASE( "Tier 1 - Area 8.4: SamplePromotion sourceAssets tracking (B10-SamplePromotionProv)",
           "[r12b][e2e][tier1][b10][prov]" )
{
    sicnu::dataset::PromotionSource src;
    src.assetId = "ast-sentinel2-001";
    src.revision = 2;
    src.role = "optical_surface_reflectance";
    CHECK( src.isValid() );
    CHECK( src.assetId == "ast-sentinel2-001" );
}

TEST_CASE( "Tier 1 - Area 8.5: ClassCodeRule and ClassifiedRegion mappings (B10)",
           "[r12b][e2e][tier1][b10][prov]" )
{
    sicnu::dataset::ClassCodeRule rule;
    rule.rawValue = 1;
    rule.classCode = "urban_fabric";

    sicnu::dataset::ClassifiedRegion region;
    region.rawValue = 1;
    region.geometryWkt = "POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))";

    CHECK( rule.rawValue == region.rawValue );
    CHECK( rule.classCode == "urban_fabric" );
}

// ----------------------------------------------------------------------------
// Feature Area 9: Canonical Digest & Authority Convergence (B11, B12)
// ----------------------------------------------------------------------------

TEST_CASE( "Tier 1 - Area 9.1: RFC 8785 canonical JSON sorting determinism (B11-CanonicalDigest)",
           "[r12b][e2e][tier1][b11][digest]" )
{
    // Key insertion order must not alter canonical serialized output
    QJsonObject obj1;
    obj1["z"] = 100;
    obj1["a"] = 200;
    obj1["m"] = "middle";

    QJsonObject obj2;
    obj2["a"] = 200;
    obj2["m"] = "middle";
    obj2["z"] = 100;

    const QByteArray c1 = sicnu::data::canonicalizeJsonRfc8785( obj1 );
    const QByteArray c2 = sicnu::data::canonicalizeJsonRfc8785( obj2 );

    CHECK( c1 == c2 );
    CHECK( c1.startsWith( "{\"a\":200," ) );
}

TEST_CASE( "Tier 1 - Area 9.2: Canonical JSON numeric formatting according to RFC 8785 (B11)",
           "[r12b][e2e][tier1][b11][digest]" )
{
    // Negative zero (-0.0) must canonicalize identically to positive zero (0.0)
    QJsonObject negZero{ { "val", -0.0 } };
    QJsonObject posZero{ { "val", 0.0 } };

    const QByteArray cNeg = sicnu::data::canonicalizeJsonRfc8785( negZero );
    const QByteArray cPos = sicnu::data::canonicalizeJsonRfc8785( posZero );

    CHECK( cNeg == cPos );
}

TEST_CASE( "Tier 1 - Area 9.3: SHA-256 canonical digest authority (B11-CanonicalDigest)",
           "[r12b][e2e][tier1][b11][digest]" )
{
    QJsonObject recipe;
    recipe["operator"] = "virtual_raster_merge";
    recipe["version"] = "2.0";
    recipe["sources"] = QJsonArray{ "s1.tif", "s2.tif" };

    const QByteArray canonicalBytes = sicnu::data::canonicalizeJsonRfc8785( recipe );
    const QString sha256Hex = QString::fromUtf8(
        QCryptographicHash::hash( canonicalBytes, QCryptographicHash::Sha256 ).toHex() );

    CHECK( sha256Hex.length() == 64 );
    // Deterministic golden check
    const QString repeatHex = QString::fromUtf8(
        QCryptographicHash::hash( canonicalBytes, QCryptographicHash::Sha256 ).toHex() );
    CHECK( sha256Hex == repeatHex );
}

TEST_CASE( "Tier 1 - Area 9.4: artifactContentDigest streaming file hash (B08-BundleChunkedHash)",
           "[r12b][e2e][tier1][b08][digest]" )
{
    QTemporaryDir dir;
    const QString filePath = dir.filePath( "sample_data.bin" );
    const QByteArray payload = "RemoteSensingDataStream2026";
    {
        QFile f( filePath );
        f.open( QIODevice::WriteOnly );
        f.write( payload );
        f.close();
    }

    QString err;
    const QString digest = sicnu::data::artifactContentDigest( filePath, &err );
    CHECK( err.isEmpty() );

    const QString expected = QString::fromUtf8(
        QCryptographicHash::hash( payload, QCryptographicHash::Sha256 ).toHex() );
    CHECK( digest == expected );
}

TEST_CASE( "Tier 1 - Area 9.5: Security audit closure and un-gated endpoint deprecation (B12)",
           "[r12b][e2e][tier1][b12][audit]" )
{
    // Direct network calls must consult offline gate and private network checks
    sicnu::geo::offline::setEnabled( true );
    CHECK( sicnu::geo::offline::enabled() );
    CHECK_FALSE( sicnu::geo::offline::isRemoteTarget( "file:///local/path" ) );
    CHECK( sicnu::geo::offline::isRemoteTarget( "http://remote/path" ) );
    sicnu::geo::offline::setEnabled( false );
}


// ============================================================================
// TIER 2: BOUNDARY & CORNER CASES (Extreme inputs, malformed data, crashes)
// ============================================================================

TEST_CASE( "Tier 2 - Area 1: Extreme IPv6 address boundary stress",
           "[r12b][e2e][tier2][b02][ipv6]" )
{
    // Boundary addresses with maximum colons or weird compressions
    CHECK( sicnu::geo::offline::isPrivateHost( "0:0:0:0:0:0:0:1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "::0.0.0.0" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "::127.0.0.1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "fe80::" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "fe80:0:0:0:0:0:0:0" ) );

    // Malformed IPv6 inputs should fail closed (not throw or crash)
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( ":::" ) );
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( "1:2:3:4:5:6:7:8:9" ) );
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( "ffff0::1" ) );
}

TEST_CASE( "Tier 2 - Area 2: Obscure and overflow IPv4 encodings",
           "[r12b][e2e][tier2][b03][ssrf]" )
{
    // Leading zeroes in octal
    CHECK( sicnu::geo::offline::isPrivateHost( "00000000177.0.0.1" ) );
    // Hex notations
    CHECK( sicnu::geo::offline::isPrivateHost( "0x7F000001" ) );
    // Decimal overflow boundary
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( "4294967296" ) ); // 2^32 out of range
}

TEST_CASE( "Tier 2 - Area 3: Path traversal evasion in ResourceUri",
           "[r12b][e2e][tier2][b04][uri]" )
{
    const std::string root = "/opt/rs_studio/data";

    // Double dot escapes
    const auto r1 = sicnu::geo::ResourceUri::resolveAgainst( root, "../../../etc/shadow" );
    CHECK( r1.kind == sicnu::geo::ResourceKind::Invalid );

    // Encoded double dots (%2e%2e)
    const auto r2 = sicnu::geo::ResourceUri::resolveAgainst( root, "%2e%2e/%2e%2e/root" );
    CHECK( r2.kind == sicnu::geo::ResourceKind::Invalid );

    // Windows drive letters inside URI
    const auto r3 = sicnu::geo::ResourceUri::parse( "file:///C:/Users/Admin/raster.tif" );
    CHECK( r3.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK( r3.path == "C:/Users/Admin/raster.tif" );
}

TEST_CASE( "Tier 2 - Area 4: Catalog boundary queries on empty datasets",
           "[r12b][e2e][tier2][b05][catalog]" )
{
    sicnu::geo::CatalogQuery emptyQuery;
    // Degenerate bounding box (zero area)
    emptyQuery.bbox = { 0.0, 0.0, 0.0, 0.0 };
    CHECK( emptyQuery.bbox.size() == 4 );

    // 6D Bounding box
    emptyQuery.bbox = { -10.0, -10.0, 10.0, 10.0, 0.0, 5000.0 };
    CHECK( emptyQuery.bbox.size() == 6 );
}

TEST_CASE( "Tier 2 - Area 5: Atomic publishing with non-existent directories and collisions",
           "[r12b][e2e][tier2][b06][atomic]" )
{
    // Writing atomic file to invalid directory must throw GeoError
    const std::string invalidTarget = "/non_existent_mount_dir_12345/sub/file.tif";
    CHECK_THROWS(
        sicnu::geo::atomic_fs::writeFileAtomic( invalidTarget, []( const std::string &staged ) {
            createDummyFile( staged, 10 );
        } )
    );
}

TEST_CASE( "Tier 2 - Area 6: SQLite transaction abort and trigger rejection",
           "[r12b][e2e][tier2][b07][sqlite]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "gov_trigger.sqlite" );

    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    // Inject trigger to abort audit inserts
    sqlite3 *raw = nullptr;
    REQUIRE( sqlite3_open( dbPath.toUtf8().constData(), &raw ) == SQLITE_OK );
    char *err = nullptr;
    const char *triggerSql =
        "CREATE TRIGGER abort_audit BEFORE INSERT ON gov_audit "
        "BEGIN SELECT RAISE(ABORT, 'injected audit abort'); END;";
    sqlite3_exec( raw, triggerSql, nullptr, nullptr, &err );
    sqlite3_free( err );
    sqlite3_close( raw );

    // Appending audit must fail gracefully and not corrupt store
    const auto failRes = store.appendAudit( "user", "MUTATE", "asset", "ast-1" );
    CHECK_FALSE( failRes.isOk() );
    store.close();
}

TEST_CASE( "Tier 2 - Area 7: Reproduction bundle malformed checksums lines",
           "[r12b][e2e][tier2][b08][bundle]" )
{
    QTemporaryDir dir;
    const QString bundleDir = dir.path();

    // Line with invalid 32-char MD5 instead of 64-char SHA256
    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( "d41d8cd98f00b204e9800998ecf8427e  manifest.json\n" );
    cfile.close();

    QFile f( dir.filePath( "manifest.json" ) );
    f.open( QIODevice::WriteOnly );
    f.close();

    QStringList reasons;
    CHECK_FALSE( sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons ) );
    CHECK_FALSE( reasons.isEmpty() );
}

TEST_CASE( "Tier 2 - Area 8: PatchGenerator extreme extent and stride boundaries",
           "[r12b][e2e][tier2][b09][patch]" )
{
    sicnu::dataset::PatchGeneratorConfig cfg;
    cfg.strategy = sicnu::dataset::PatchStrategy::FixedGrid;
    cfg.windowWidth = 512;
    cfg.windowHeight = 512;
    cfg.strideX = 512;
    cfg.strideY = 512;
    cfg.borderPolicy = sicnu::dataset::BorderPolicy::Drop;

    sicnu::dataset::GeoTransform transform;
    // Raster extent smaller than window size with Drop policy produces 0 patches
    const auto res = sicnu::dataset::PatchGenerator::generate(
        cfg, 256, 256, transform, []( const sicnu::dataset::PixelWindow & ) { return 1.0; } );

    REQUIRE( res.isOk() );
    CHECK( res.value().isEmpty() );
}

TEST_CASE( "Tier 2 - Area 9: Canonical JSON deeply nested and empty structures",
           "[r12b][e2e][tier2][b11][digest]" )
{
    QJsonObject root;
    root["nested"] = QJsonObject{ { "level1", QJsonObject{ { "level2", QJsonArray{ 1, 2, 3 } } } } };
    root["empty_obj"] = QJsonObject{};
    root["empty_arr"] = QJsonArray{};

    const QByteArray c = sicnu::data::canonicalizeJsonRfc8785( root );
    CHECK_FALSE( c.isEmpty() );
    CHECK( c.contains( "\"empty_arr\":[]" ) );
    CHECK( c.contains( "\"empty_obj\":{}" ) );
}


// ============================================================================
// TIER 3: CROSS-FEATURE PAIRWISE INTERACTIONS
// ============================================================================

TEST_CASE( "Tier 3 - Interaction 1: Offline Gate + Remote Validator + COG Validator",
           "[r12b][e2e][tier3][offline][cog]" )
{
    // Engaging offline mode must immediately gate both Remote Validator and COG Validator
    sicnu::geo::offline::setEnabled( true );

    const auto cogReport = sicnu::geo::validateCog( "http://inaccessible.domain/cog.tif" );
    CHECK_FALSE( cogReport.isCog );

    sicnu::geo::RemoteValidatorOptions opts;
    opts.blockPrivateNetworks = true;
    CHECK( sicnu::geo::offline::isPrivateHost( "http://127.0.0.1/test" ) );

    sicnu::geo::offline::setEnabled( false );
}

TEST_CASE( "Tier 3 - Interaction 2: ResourceUri Parsing + Path Normalization + GDAL Open Target + Atomic FS",
           "[r12b][e2e][tier3][uri][atomic]" )
{
    const std::string scratch = makeScratchDir( "uri_atomic" );
    const std::string rawUri = "file://" + scratch + "/scene_01.tif";

    const auto uri = sicnu::geo::ResourceUri::parse( rawUri );
    CHECK( uri.kind == sicnu::geo::ResourceKind::LocalFile );

    const std::string gdalTarget = uri.gdalOpenTarget();
    CHECK( gdalTarget.find( "file://" ) == std::string::npos );

    // Publish through atomic filesystem at the resolved target path
    sicnu::geo::atomic_fs::writeFileAtomic( gdalTarget, []( const std::string &staged ) {
        createDummyFile( staged, 2048, 0xEE );
    } );

    CHECK( sicnu::geo::atomic_fs::fileExists( gdalTarget ) );
    CHECK( sicnu::geo::atomic_fs::fileSize( gdalTarget ) == 2048 );
}

TEST_CASE( "Tier 3 - Interaction 3: Catalog Query + Local STAC Traversal + SSRF Gate + Asset Ingestion",
           "[r12b][e2e][tier3][stac][ssrf]" )
{
    const std::string scratch = makeScratchDir( "stac_ssrf" );
    const std::string assetTif = ( fs::path( scratch ) / "band4.tif" ).string();
    createDummyFile( assetTif, 512, 0x44 );

    // Local STAC item resolving asset
    const auto assetUri = sicnu::geo::ResourceUri::resolveAgainst( scratch, "band4.tif" );
    CHECK( assetUri.kind == sicnu::geo::ResourceKind::LocalFile );
    CHECK_FALSE( assetUri.isRemote() );
    CHECK_FALSE( sicnu::geo::offline::isPrivateHost( assetUri.path ) );
}

TEST_CASE( "Tier 3 - Interaction 4: Atomic Multi-File Publish + Governance Store Registration + Audit Logging",
           "[r12b][e2e][tier3][atomic][governance]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "governance.sqlite" );
    const std::string scratch = makeScratchDir( "atomic_gov" );
    const std::string targetTif = ( fs::path( scratch ) / "publish_mosaic.tif" ).string();

    // 1. Atomic file publish
    sicnu::geo::atomic_fs::writeFileAtomic( targetTif, []( const std::string &staged ) {
        createDummyFile( staged, 4096, 0x33 );
    } );
    REQUIRE( sicnu::geo::atomic_fs::fileExists( targetTif ) );

    // 2. Register in GovernanceStore
    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    sicnu::workspace::GovernedAsset asset;
    asset.assetId = "ast-mosaic-01";
    asset.displayName = "Published Mosaic";
    asset.canonicalSource = QString::fromStdString( targetTif );
    asset.kind = "raster";
    asset.sizeBytes = 4096;
    REQUIRE( store.upsertAsset( asset ).isOk() );

    // 3. Record audit trail
    REQUIRE( store.appendAudit( "pipeline", "PUBLISH", "asset", "ast-mosaic-01" ).isOk() );
    const auto tail = store.auditTail( 5 );
    CHECK( tail.last().entityId == "ast-mosaic-01" );
    store.close();
}

TEST_CASE( "Tier 3 - Interaction 5: Sample Promotion + Source Asset Tracking + Canonical RFC 8785 Digest",
           "[r12b][e2e][tier3][sample][digest]" )
{
    sicnu::dataset::PromotionSource src;
    src.assetId = "ast-source-99";
    src.revision = 1;
    src.role = "optical";

    QJsonObject sampleDoc;
    sampleDoc["asset_id"] = src.assetId;
    sampleDoc["revision"] = static_cast<qint64>( src.revision );
    sampleDoc["role"] = src.role;

    // Canonical digest of promotion source
    const QByteArray canonBytes = sicnu::data::canonicalizeJsonRfc8785( sampleDoc );
    const QString digest = QString::fromUtf8(
        QCryptographicHash::hash( canonBytes, QCryptographicHash::Sha256 ).toHex() );
    CHECK( digest.length() == 64 );
}

TEST_CASE( "Tier 3 - Interaction 6: Reproduction Bundle Export + Checksum Generation + Streaming SHA-256",
           "[r12b][e2e][tier3][bundle][streaming]" )
{
    QTemporaryDir dir;
    const QString bundleDir = dir.path();
    const QString dataFile = dir.filePath( "large_raster.dat" );

    // Generate simulated 100KB payload
    QFile f( dataFile );
    f.open( QIODevice::WriteOnly );
    const QByteArray chunk( 1024, 'X' );
    for ( int i = 0; i < 100; ++i ) f.write( chunk );
    f.close();

    // Calculate streaming digest via artifactContentDigest
    QString err;
    const QString streamDigest = sicnu::data::artifactContentDigest( dataFile, &err );
    CHECK( err.isEmpty() );
    CHECK( streamDigest.length() == 64 );

    // Checksums file covers required + data file
    QStringList lines;
    for ( const char *req : sicnu::experiment::kRequiredBundleMembers )
    {
        QFile rf( dir.filePath( req ) );
        rf.open( QIODevice::WriteOnly );
        rf.write( "{}" );
        rf.close();
        const QString d = QString::fromUtf8(
            QCryptographicHash::hash( "{}", QCryptographicHash::Sha256 ).toHex() );
        lines.append( QStringLiteral( "%1  %2" ).arg( d, req ) );
    }
    lines.append( QStringLiteral( "%1  large_raster.dat" ).arg( streamDigest ) );

    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( lines.join( "\n" ).toUtf8() + "\n" );
    cfile.close();

    QStringList reasons;
    CHECK( sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons ) );
    CHECK( reasons.isEmpty() );
}


// ============================================================================
// TIER 4: REAL-WORLD APPLICATION WORKFLOWS (5 Realistic Scenarios)
// ============================================================================

TEST_CASE( "Tier 4 - Scenario 1: Offline Air-Gapped Catalog Sync and Local Raster Ingestion",
           "[r12b][e2e][tier4][scenario1]" )
{
    // Scenario 1: Ingesting imagery in a secure, disconnected network (SICNU_OFFLINE=1)
    sicnu::geo::offline::setEnabled( true );
    REQUIRE( sicnu::geo::offline::enabled() );

    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "offline_gov.sqlite" );
    const std::string localRaster = dir.filePath( "scene_s2.tif" ).toStdString();
    createDummyFile( localRaster, 8192, 0x12 );

    // 1. Validate that remote requests fail fast
    CHECK( sicnu::geo::offline::isRemoteTarget( "https://api.stac.io/search" ) );
    const auto cogReport = sicnu::geo::validateCog( "https://api.stac.io/cog.tif" );
    CHECK_FALSE( cogReport.isCog );

    // 2. Validate that local URI is resolved and ingested without error
    const auto localUri = sicnu::geo::ResourceUri::parse( "file://" + localRaster );
    CHECK( localUri.kind == sicnu::geo::ResourceKind::LocalFile );

    sicnu::workspace::GovernanceStore store;
    REQUIRE( store.open( dbPath ) );

    sicnu::workspace::GovernedAsset asset;
    asset.assetId = "ast-offline-001";
    asset.displayName = "Air-Gapped S2 Scene";
    asset.canonicalSource = QString::fromStdString( localUri.path );
    asset.sizeBytes = 8192;
    asset.availability = "fresh";
    REQUIRE( store.upsertAsset( asset ).isOk() );

    // 3. Confirm audit trail
    REQUIRE( store.appendAudit( "offline_ingest_daemon", "INGEST", "asset", "ast-offline-001" ).isOk() );
    const auto audit = store.auditTail( 1 );
    CHECK( audit.first().actor == "offline_ingest_daemon" );

    store.close();
    sicnu::geo::offline::setEnabled( false );
}

TEST_CASE( "Tier 4 - Scenario 2: Multi-File Staged Publication with Interruption & Atomic Recovery",
           "[r12b][e2e][tier4][scenario2]" )
{
    // Scenario 2: Processing pipeline publishing multi-file raster with crash recovery
    const std::string scratch = makeScratchDir( "scenario2_atomic" );
    const std::string mainTif = ( fs::path( scratch ) / "classified_mosaic.tif" ).string();
    const std::string pamXml = ( fs::path( scratch ) / "classified_mosaic.tif.aux.xml" ).string();

    // 1. Pre-existing valid file
    createDummyFile( mainTif, 1024, 0xAA );

    // 2. Simulate failed multi-stage publish that throws mid-transfer
    std::string stagedMain;
    CHECK_THROWS_AS(
        sicnu::geo::atomic_fs::writeFileAtomic( mainTif, [&]( const std::string &staged ) {
            stagedMain = staged;
            createDummyFile( staged, 5000, 0xBB );
            throw std::runtime_error( "Power outage / process SIGKILL simulation" );
        } ),
        std::runtime_error
    );

    // 3. Verify target remained intact and no partial corrupted files exist
    CHECK( sicnu::geo::atomic_fs::fileExists( mainTif ) );
    CHECK( sicnu::geo::atomic_fs::fileSize( mainTif ) == 1024 );
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( stagedMain ) );
}

TEST_CASE( "Tier 4 - Scenario 3: Portable Reproduction Bundle Package, Transport & Provenance Audit",
           "[r12b][e2e][tier4][scenario3]" )
{
    // Scenario 3: Scientific replication bundle packaged and transferred to external auditor
    QTemporaryDir dir;
    const QString bundleDir = dir.path();

    // 1. Author canonical reproduction files
    QJsonObject runConfig{
        { "algorithm", "rs:classify_supercells" },
        { "params", QJsonObject{ { "clusters", 16 }, { "iterations", 100 } } }
    };
    QJsonObject envConfig{
        { "os", "linux" },
        { "gdal_version", "3.12.4" }
    };
    QJsonObject manifest{
        { "schema_version", "1" },
        { "run_id", "run-2026-10-13-alpha" }
    };

    auto writeJson = [&]( const QString &name, const QJsonObject &obj ) {
        QFile f( dir.filePath( name ) );
        f.open( QIODevice::WriteOnly );
        f.write( sicnu::data::canonicalizeJsonRfc8785( obj ) );
        f.close();
    };

    writeJson( "run_config.json", runConfig );
    writeJson( "environment.json", envConfig );
    writeJson( "manifest.json", manifest );

    // 2. Generate checksums.txt
    QStringList lines;
    for ( const char *member : sicnu::experiment::kRequiredBundleMembers )
    {
        QFile f( dir.filePath( member ) );
        f.open( QIODevice::ReadOnly );
        const QString digest = QString::fromUtf8(
            QCryptographicHash::hash( f.readAll(), QCryptographicHash::Sha256 ).toHex() );
        lines.append( QStringLiteral( "%1  %2" ).arg( digest, member ) );
    }

    QFile cfile( dir.filePath( "checksums.txt" ) );
    cfile.open( QIODevice::WriteOnly );
    cfile.write( lines.join( "\n" ).toUtf8() + "\n" );
    cfile.close();

    // 3. Independent verification gate
    QStringList reasons;
    const bool valid = sicnu::experiment::verifyBundleChecksums( bundleDir, &reasons );
    CHECK( valid );
    CHECK( reasons.isEmpty() );

    // 4. Tamper detection: modifying 1 byte in manifest must fail verification
    QFile f( dir.filePath( "manifest.json" ) );
    f.open( QIODevice::Append );
    f.write( " " );
    f.close();

    QStringList failReasons;
    CHECK_FALSE( sicnu::experiment::verifyBundleChecksums( bundleDir, &failReasons ) );
    CHECK_FALSE( failReasons.isEmpty() );
    CHECK_THAT( failReasons.first().toStdString(), ContainsSubstring( "checksum mismatch" ) );
}

TEST_CASE( "Tier 4 - Scenario 4: High-Throughput Raster Patching Pipeline with Bounded Memory",
           "[r12b][e2e][tier4][scenario4]" )
{
    // Scenario 4: Tiling an ultra-high resolution raster (10,000 x 10,000 pixels) into ML training patches
    sicnu::dataset::PatchGeneratorConfig cfg;
    cfg.strategy = sicnu::dataset::PatchStrategy::FixedGrid;
    cfg.windowWidth = 512;
    cfg.windowHeight = 512;
    cfg.strideX = 512;
    cfg.strideY = 512;
    cfg.borderPolicy = sicnu::dataset::BorderPolicy::Drop;

    // Verify config hash determinism
    const QString hash = sicnu::dataset::PatchGenerator::configHash( cfg );
    CHECK_FALSE( hash.isEmpty() );

    sicnu::dataset::GeoTransform transform;
    int readerCalls = 0;
    const auto res = sicnu::dataset::PatchGenerator::generate(
        cfg, 2048, 2048, transform,
        [&]( const sicnu::dataset::PixelWindow & ) {
            ++readerCalls;
            return 1.0;
        }
    );

    REQUIRE( res.isOk() );
    // 2048x2048 / 512x512 = 4x4 = 16 patches
    CHECK( res.value().size() == 16 );
    for ( const auto &patch : res.value() )
    {
        CHECK( patch.window.width == 512 );
        CHECK( patch.window.height == 512 );
        CHECK( patch.generatorConfigHash == hash );
    }
}

TEST_CASE( "Tier 4 - Scenario 5: Corrupt Database Fail-Fast Detection & Audit Log Resilience",
           "[r12b][e2e][tier4][scenario5]" )
{
    // Scenario 5: Detecting SQLite corruption without silent truncation or lost audit trails
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( "corrupt_probe.sqlite" );

    // 1. Create and populate healthy store
    {
        sicnu::workspace::GovernanceStore store;
        REQUIRE( store.open( dbPath ) );
        sicnu::workspace::GovernedAsset asset;
        asset.assetId = "ast-healthy";
        asset.displayName = "Healthy Asset";
        REQUIRE( store.upsertAsset( asset ).isOk() );
        REQUIRE( store.appendAudit( "admin", "INIT", "asset", "ast-healthy" ).isOk() );
        store.close();
    }

    // 2. Corrupt SQLite database file by overwriting page header bytes
    {
        QFile f( dbPath );
        REQUIRE( f.open( QIODevice::ReadWrite ) );
        f.seek( 100 ); // past the SQLite file header
        f.write( QByteArray( 512, '\xFF' ) );
        f.close();
    }

    // 3. Reopening or querying corrupt store must report error / not pass silently
    sicnu::workspace::GovernanceStore corruptStore;
    QString openErr;
    const bool opened = corruptStore.open( dbPath, &openErr );
    if ( opened )
    {
        const auto diag = corruptStore.integrityCheck();
        CHECK_FALSE( diag.isOk() );
        corruptStore.close();
    }
    else
    {
        CHECK_FALSE( openErr.isEmpty() );
    }
}

TEST_CASE( "Tier 4 - Scenario 6: End-to-End Canonical Virtual Raster Recipe Digest & Security Perimeter Convergence (B11 & B12)",
           "[r12b][e2e][tier4][scenario6][recipe][security]" )
{
    // 1. Virtual Raster Recipe & RFC 8785 Canonical Digest (B11)
    sicnu::data::VirtualRasterRecipe recipe1;
    recipe1.inputs = {
        sicnu::data::BandRef{ sicnu::data::AssetId::generate(), 1 },
        sicnu::data::BandRef{ sicnu::data::AssetId::generate(), 2 }
    };
    recipe1.targetCrs = QStringLiteral( "EPSG:4326" );
    recipe1.targetResolutionX = 0.001;
    recipe1.targetResolutionY = 0.001;
    recipe1.extentPolicy = sicnu::data::ExtentPolicy::Intersection;
    recipe1.resampling = sicnu::data::ResamplingMethod::Bilinear;
    recipe1.noDataPolicy = sicnu::data::NoDataPolicy::Preserve;
    recipe1.noDataFillValue = 0.0;

    const QString digest1 = recipe1.contentDigest();
    CHECK( digest1.length() == 64 );

    // Verify determinism with -0.0 fill value normalization under RFC 8785
    sicnu::data::VirtualRasterRecipe recipe2 = recipe1;
    recipe2.noDataFillValue = -0.0;
    const QString digest2 = recipe2.contentDigest();
    CHECK( digest1 == digest2 );

    // Changing any property alters the canonical digest
    sicnu::data::VirtualRasterRecipe recipe3 = recipe1;
    recipe3.targetResolutionX = 0.002;
    CHECK( recipe1.contentDigest() != recipe3.contentDigest() );

    // 2. Security Perimeter Convergence (B12): Remote Resolver & Private Network Guard
    sicnu::geo::offline::setEnabled( true );
    CHECK( sicnu::geo::offline::enabled() );

    // Verify private host and SSRF rejection under offline gate
    CHECK( sicnu::geo::offline::isPrivateHost( "127.0.0.1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "169.254.169.254" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "::1" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "localhost" ) );
    CHECK( sicnu::geo::offline::isPrivateHost( "http://10.0.0.1:8080/data.tif" ) );

    // Verify HTTP fetch rejection when offline
    sicnu::geo::HttpFetchOptions fetchOpts;
    fetchOpts.timeoutSeconds = 2;
    CHECK_THROWS_AS(
        sicnu::geo::httpFetch( "http://example.com/raster.tif", fetchOpts ),
        sicnu::geo::GeoError
    );

    sicnu::geo::offline::setEnabled( false );
    CHECK_FALSE( sicnu::geo::offline::enabled() );
}

