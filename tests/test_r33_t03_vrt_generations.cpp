/***************************************************************************
  tests/test_r33_t03_vrt_generations.cpp — R33-T03 WP06/WP08: managed
  retention of a published virtual raster's warp-intermediate generation.
  The post-publish sweep used to delete every `<vrt>.warp*` file outside the
  new live set, so a managed reader of the previous generation lost the
  files its lazily-opened sources still needed. A generation lease now pins
  those files for the reader's lifetime; the last release performs the
  deferred deletion.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/data_result.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/processing_asset_resolver.h"
#include "data/providers/virtual_raster_generation_lease.h"
#include "data/providers/virtual_raster_source_provider.h"
#include "data/source_descriptor.h"

using sicnu::data::acquireVirtualRasterGenerationLease;
using sicnu::data::AssetCapability;
using sicnu::data::AssetId;
using sicnu::data::AssetKind;
using sicnu::data::AssetRef;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::data::ProcessingAssetResolver;
using sicnu::data::providers::sweepStaleWarpIntermediates;
using sicnu::data::RegisterRequest;
using sicnu::data::Result;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::virtualRasterGenerationLeaseCount;
using sicnu::data::virtualRasterGenerationRetained;
using sicnu::data::virtualRasterIntermediateGenerationToken;
using sicnu::data::virtualRasterReferencedIntermediates;
using sicnu::data::VirtualRasterGenerationLease;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::internal::SourceProvider;
using sicnu::data::internal::SourceProviderRegistry;

namespace
{

/// A minimal but real fixture directory: a published VRT whose XML
/// references one warp intermediate of `generation`, plus optional sibling
/// intermediates of other generations and one foreign `warp`-prefixed file.
struct VrtFixture
{
    QTemporaryDir dir;
    QString vrtPath;
    QString genA = QStringLiteral( "aaaaaaaa" );
    QString genB = QStringLiteral( "bbbbbbbb" );
    QString intermediateA;   // referenced by the CURRENT vrt text
    QString intermediateB;   // sibling of another generation
    QString foreign;         // provider never staged this spelling

    VrtFixture()
    {
        vrtPath = dir.path() + QStringLiteral( "/cube.vrt" );
        intermediateA = dir.path() + QStringLiteral( "/cube.vrt.warp-%1-r1-x.tif" ).arg( genA );
        intermediateB = dir.path() + QStringLiteral( "/cube.vrt.warp-%1-r1-x.tif" ).arg( genB );
        foreign = dir.path() + QStringLiteral( "/cube.vrt.warp-not-a-token.tif" );
        QFile a( intermediateA );
        REQUIRE( a.open( QIODevice::WriteOnly ) );
        REQUIRE( a.write( "gen-a bytes" ) > 0 );
        QFile b( intermediateB );
        REQUIRE( b.open( QIODevice::WriteOnly ) );
        REQUIRE( b.write( "gen-b bytes" ) > 0 );
        QFile f( foreign );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        REQUIRE( f.write( "foreign bytes" ) > 0 );
        publishReferencing( genA );
    }

    /// Rewrites the VRT bytes the way a new publish would: the XML lists the
    /// referenced intermediate filenames (relativeToVRT spelling).
    void publishReferencing( const QString &generation )
    {
        const QString name = QStringLiteral( "cube.vrt.warp-%1-r1-x.tif" ).arg( generation );
        QFile vrt( vrtPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        vrt.write( ( QStringLiteral( "<VRTDataset><VRTRasterBand><SimpleSource>"
                                     "<SourceFilename relativeToVRT=\"1\">" ) +
                     name + QStringLiteral( "</SourceFilename></SimpleSource>"
                                             "</VRTRasterBand></VRTDataset>" ) )
                     .toUtf8() );
    }
};

bool fileExists( const QString &path )
{
    return QFileInfo::exists( path );
}

} // namespace

TEST_CASE( "generation leases retain intermediates across a publish sweep and defer deletion",
           "[data][vrt][generations][r33t03]" )
{
    VrtFixture fx;

    // Token parsing: staged spellings parse, foreign ones do not.
    CHECK( virtualRasterIntermediateGenerationToken( fx.vrtPath,
                                                     QStringLiteral( "cube.vrt.warp-cccccccc-r2-y.tif" ) )
             == QStringLiteral( "cccccccc" ) );
    CHECK( virtualRasterIntermediateGenerationToken(
             fx.vrtPath, QStringLiteral( "cube.vrt.warp-not-a-token.tif" ) )
             .isEmpty() );

    // The current VRT references exactly its own generation's intermediate.
    const QStringList referenced = virtualRasterReferencedIntermediates( fx.vrtPath );
    REQUIRE( referenced.size() == 1 );
    CHECK( referenced.front() == QStringLiteral( "cube.vrt.warp-aaaaaaaa-r1-x.tif" ) );

    // --- Managed reader of generation A acquires retention ---------------
    VirtualRasterGenerationLease lease = acquireVirtualRasterGenerationLease( fx.vrtPath );
    REQUIRE( lease.isValid() );
    CHECK( lease.generationToken() == fx.genA );
    CHECK( virtualRasterGenerationRetained( fx.vrtPath, fx.genA ) );
    CHECK( virtualRasterGenerationLeaseCount( fx.vrtPath ) == 1 );

    // --- Publish B: new VRT text, post-publish sweep with B's live set ---
    fx.publishReferencing( fx.genB );
    sweepStaleWarpIntermediates( fx.vrtPath, { QStringLiteral( "cube.vrt.warp-bbbbbbbb-r1-x.tif" ) } );

    // A's files survive the sweep (leased) AND the lazy-open oracle: the
    // file the generation-A reader would still read exists and is readable.
    CHECK( fileExists( fx.intermediateA ) );
    {
        QFile reader( fx.intermediateA );
        CHECK( reader.open( QIODevice::ReadOnly ) );
        CHECK( reader.readAll() == QByteArrayLiteral( "gen-a bytes" ) );
    }
    // The foreign spelling was never retainable: swept as before.
    CHECK_FALSE( fileExists( fx.foreign ) );

    // --- A second lease stacks; one release must NOT trigger deletion ----
    {
        VirtualRasterGenerationLease second = acquireVirtualRasterGenerationLease( fx.vrtPath );
        // B is current now: the new reader retains generation B.
        REQUIRE( second.isValid() );
        CHECK( second.generationToken() == fx.genB );
        CHECK( virtualRasterGenerationLeaseCount( fx.vrtPath ) == 2 );
    }
    // `second` destroyed: B is still current (referenced), A still leased.
    CHECK( virtualRasterGenerationLeaseCount( fx.vrtPath ) == 1 );
    CHECK( fileExists( fx.intermediateA ) );
    CHECK( fileExists( fx.intermediateB ) );

    // --- Last A release: deferred deletion of the unreferenced generation -
    lease = {};   // release
    CHECK( virtualRasterGenerationLeaseCount( fx.vrtPath ) == 0 );
    CHECK_FALSE( virtualRasterGenerationRetained( fx.vrtPath, fx.genA ) );
    CHECK_FALSE( fileExists( fx.intermediateA ) );   // stale now, deleted
    CHECK( fileExists( fx.intermediateB ) );         // current publish's live set

    // Sweep without any lease behaves exactly as before (master contract).
    sweepStaleWarpIntermediates( fx.vrtPath, { QStringLiteral( "cube.vrt.warp-bbbbbbbb-r1-x.tif" ) } );
    CHECK( fileExists( fx.intermediateB ) );
}

TEST_CASE( "non-warp VRTs retain nothing and degraded leases never block reads",
           "[data][vrt][generations][r33t03]" )
{
    QTemporaryDir dir;
    const QString vrtPath = dir.path() + QStringLiteral( "/plain.vrt" );
    {
        QFile vrt( vrtPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly ) );
        vrt.write( "<VRTDataset/>" );
    }
    // A plain composition (no warp intermediates): acquire is an honest
    // no-retention invalid lease, never an error.
    VirtualRasterGenerationLease lease = acquireVirtualRasterGenerationLease( vrtPath );
    CHECK_FALSE( lease.isValid() );
    CHECK( virtualRasterGenerationLeaseCount( vrtPath ) == 0 );

    // A missing VRT degrades the same way.
    CHECK_FALSE(
      acquireVirtualRasterGenerationLease( dir.path() + QStringLiteral( "/absent.vrt" ) ).isValid() );

    // Move semantics: a moved-from lease retains nothing, the moved-to
    // lease keeps the retention and releases exactly once.
    QFile staged( dir.path() + QStringLiteral( "/moved.vrt.warp-1234abcd-r1-x.tif" ) );
    REQUIRE( staged.open( QIODevice::WriteOnly ) );
    staged.write( "x" );
    {
        QFile vrt2( dir.path() + QStringLiteral( "/moved.vrt" ) );
        REQUIRE( vrt2.open( QIODevice::WriteOnly ) );
        vrt2.write( "<SourceFilename relativeToVRT=\"1\">moved.vrt.warp-1234abcd-r1-x.tif"
                    "</SourceFilename>" );
    }
    VirtualRasterGenerationLease first = acquireVirtualRasterGenerationLease(
      dir.path() + QStringLiteral( "/moved.vrt" ) );
    REQUIRE( first.isValid() );
    VirtualRasterGenerationLease moved = std::move( first );
    CHECK_FALSE( first.isValid() );
    CHECK( virtualRasterGenerationLeaseCount( dir.path() + QStringLiteral( "/moved.vrt" ) ) == 1 );
    moved = {};   // release once
    CHECK( virtualRasterGenerationLeaseCount( dir.path() + QStringLiteral( "/moved.vrt" ) ) == 0 );
}

namespace
{

/// Resolves Virtual Raster assets from a fixture VRT file on disk (the
/// same seam the real provider uses; identity plumbing is irrelevant to
/// the retention contract under test).
class FixtureVrtSourceProvider final : public SourceProvider
{
  public:
    bool supports( const SourceDescriptor &source ) const override
    {
        return source.providerKey == QStringLiteral( "vrt" );
    }

    Result<ResolvedSource> resolve( const SourceDescriptor &source ) const override
    {
        ResolvedSource resolved;
        resolved.kind = AssetKind::VirtualRaster;
        resolved.state = AssetState::Ready;
        resolved.storageKind = StorageKind::File;
        resolved.capabilities = AssetCapability::Renderable |
                                AssetCapability::ReadablePixels;
        resolved.canonicalSource = source.canonicalSource;
        resolved.canonicalProviderKey = QStringLiteral( "vrt" );
        resolved.displayName = QStringLiteral( "VRT(fixture)" );
        return Result<ResolvedSource>::success( resolved );
    }
};

} // namespace

TEST_CASE( "the managed resolver entry holds the generation lease for the reader's lifetime",
           "[data][vrt][generations][resolver][r33t03]" )
{
    VrtFixture fx;

    SourceProviderRegistry providers;
    providers.add( std::make_unique<FixtureVrtSourceProvider>() );
    const std::unique_ptr<DataManager> manager = providers.createDataManager();

    RegisterRequest request;
    request.source.providerKey = QStringLiteral( "vrt" );
    request.source.canonicalSource = fx.vrtPath;
    const auto registered = manager->registerSource( request );
    REQUIRE_FALSE( registered.assetId.isNull() );

    const ProcessingAssetResolver resolver( manager.get() );
    auto resolved = resolver.resolve( AssetRef{ registered.assetId },
                                      QStringLiteral( "warp window read" ) );
    REQUIRE( resolved );
    REQUIRE( resolved.value().isValid() );

    // The ResolvedAsset — the handle a real reader owner holds for the
    // whole read — pins the CURRENT generation's intermediates.
    CHECK( virtualRasterGenerationLeaseCount( fx.vrtPath ) == 1 );
    CHECK( virtualRasterGenerationRetained( fx.vrtPath, fx.genA ) );

    // Publish B underneath the open reader: its generation-A files survive.
    fx.publishReferencing( fx.genB );
    sweepStaleWarpIntermediates( fx.vrtPath,
                                 { QStringLiteral( "cube.vrt.warp-bbbbbbbb-r1-x.tif" ) } );
    CHECK( fileExists( fx.intermediateA ) );

    // Destroying the handle (last reader owner gone) releases retention and
    // performs the deferred deletion of the now-unreferenced generation.
    {
        auto handle = resolved.take();   // the move-only ResolvedAsset
    }
    CHECK( virtualRasterGenerationLeaseCount( fx.vrtPath ) == 0 );
    CHECK_FALSE( fileExists( fx.intermediateA ) );
    CHECK( fileExists( fx.intermediateB ) );
}
