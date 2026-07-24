#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/data_result.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/processing_asset_resolver.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetCapability;
using sicnu::data::AssetId;
using sicnu::data::AssetKind;
using sicnu::data::AssetRef;
using sicnu::data::AssetRevision;
using sicnu::data::AssetState;
using sicnu::data::AssetUse;
using sicnu::data::DataManager;
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::LeaseKind;
using sicnu::data::PersistencePolicy;
using sicnu::data::ProcessingAssetResolver;
using sicnu::data::RegisterRequest;
using sicnu::data::RelocateRequest;
using sicnu::data::Result;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::internal::SourceProvider;
using sicnu::data::internal::SourceProviderRegistry;

namespace
{

class InMemorySourceProvider final : public SourceProvider
{
  public:
    bool supports( const SourceDescriptor &source ) const override
    {
      return source.providerKey.startsWith( QStringLiteral( "memory-" ) );
    }

    Result<ResolvedSource> resolve( const SourceDescriptor &source ) const override
    {
      if ( source.providerKey == QStringLiteral( "memory-vector" ) )
      {
        return Result<ResolvedSource>::success(
          ResolvedSource{ AssetKind::Vector,
                          AssetState::Ready,
                          AssetCapability::Renderable | AssetCapability::QueryableFeatures,
                          StorageKind::Memory,
                          QStringLiteral( "Test vector" ) } );
      }

      if ( source.providerKey == QStringLiteral( "memory-missing" ) )
      {
        return Result<ResolvedSource>::success(
          ResolvedSource{ AssetKind::Raster,
                          AssetState::Missing,
                          AssetCapability::Relocatable,
                          StorageKind::File,
                          QStringLiteral( "Missing raster" ) } );
      }

      // Plain raster with readable pixels.
      return Result<ResolvedSource>::success(
        ResolvedSource{ AssetKind::Raster,
                        AssetState::Ready,
                        AssetCapability::Renderable | AssetCapability::ReadablePixels |
                          AssetCapability::BandMetadata,
                        StorageKind::Memory,
                        QStringLiteral( "Test raster" ) } );
    }
};

std::unique_ptr<DataManager> makeDataManager()
{
  SourceProviderRegistry providers;
  providers.add( std::make_unique<InMemorySourceProvider>() );
  return providers.createDataManager();
}

SourceDescriptor memorySource( const QString &providerKey, const QString &source )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = providerKey;
  descriptor.canonicalSource = source;
  return descriptor;
}

AssetId registerRaster( DataManager *manager, const QString &source )
{
  RegisterRequest request;
  request.source = memorySource( QStringLiteral( "memory-raster" ), source );
  const auto registered = manager->registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  return registered.assetId;
}

} // namespace

TEST_CASE( "Resolving a valid AssetRef returns an immutable snapshot and holds a Task lease",
           "[processing_asset_resolver]" )
{
  const auto manager = makeDataManager();
  const ProcessingAssetResolver resolver( manager.get() );
  const AssetId id = registerRaster( manager.get(), QStringLiteral( "scene-a" ) );

  // A View lease (e.g. a Display Layer) already pins the asset.
  auto viewLease =
    manager->acquire( AssetRef{ id }, sicnu::data::AssetUse{ LeaseKind::View } ).take();
  REQUIRE( viewLease.isValid() );
  const auto before = manager->leaseCount( id );

  auto resolved = resolver.resolve( AssetRef{ id, AssetRevision::initial() },
                                    QStringLiteral( "ndvi input" ) );

  REQUIRE( resolved );
  CHECK( resolved.value().isValid() );
  CHECK( resolved.value().snapshot().id() == id );
  CHECK( resolved.value().snapshot().revision() == AssetRevision::initial() );
  CHECK( resolved.value().snapshot().kind() == AssetKind::Raster );
  CHECK( resolved.value().snapshot().capabilities().testFlag(
    AssetCapability::ReadablePixels ) );
  CHECK_FALSE( resolved.value().snapshot().sourceLocation().isEmpty() );

  // The Task lease is held for the life of the handle, counted separately from
  // the View lease.
  CHECK( manager->leaseCount( id ) == before + 1 );
  const auto leases = manager->leases( id );
  REQUIRE( leases.size() == 2 );
  int taskLeases = 0;
  int viewLeases = 0;
  for ( const auto &lease : leases )
  {
    if ( lease.kind == LeaseKind::Task )
      ++taskLeases;
    else if ( lease.kind == LeaseKind::View )
      ++viewLeases;
  }
  CHECK( taskLeases == 1 );
  CHECK( viewLeases == 1 );

  // Releasing the handle releases the Task lease but leaves the View lease.
  {
    sicnu::data::ResolvedAsset handle = resolved.take();
    CHECK( handle.isValid() );
    CHECK( manager->leaseCount( id ) == before + 1 );
  }
  CHECK( manager->leaseCount( id ) == before );
  CHECK( viewLease.isValid() );
}

TEST_CASE( "A stale expected revision is rejected; the current revision resolves",
           "[processing_asset_resolver]" )
{
  const auto manager = makeDataManager();
  const ProcessingAssetResolver resolver( manager.get() );
  const AssetId id = registerRaster( manager.get(), QStringLiteral( "scene-a" ) );

  // Advance the asset revision via relocation to a new source.
  RelocateRequest relocate;
  relocate.id = id;
  relocate.replacement = memorySource( QStringLiteral( "memory-raster" ),
                                       QStringLiteral( "scene-b" ) );
  REQUIRE( manager->relocate( relocate ) );
  REQUIRE( manager->asset( id )->revision() == AssetRevision::initial().next() );

  // A request pinned to the old revision is rejected.
  const auto stale = resolver.resolve( AssetRef{ id, AssetRevision::initial() },
                                       QStringLiteral( "stale input" ) );
  REQUIRE_FALSE( stale );
  REQUIRE( stale.diagnostics().size() == 1 );
  CHECK( stale.diagnostics().first().code ==
         QStringLiteral( "asset.stale_revision" ) );

  // A request at the current revision resolves.
  const auto current = resolver.resolve(
    AssetRef{ id, AssetRevision::initial().next() }, QStringLiteral( "current input" ) );
  REQUIRE( current );
}

TEST_CASE( "A missing or unresolvable asset is rejected before execution",
           "[processing_asset_resolver]" )
{
  const auto manager = makeDataManager();
  const ProcessingAssetResolver resolver( manager.get() );

  // Unknown asset id.
  const auto unknown = resolver.resolve( AssetRef{ AssetId::generate() },
                                         QStringLiteral( "unknown input" ) );
  REQUIRE_FALSE( unknown );
  CHECK( unknown.diagnostics().first().code == QStringLiteral( "asset.unknown" ) );

  // Registered but Missing asset.
  RegisterRequest missingRequest;
  missingRequest.source = memorySource( QStringLiteral( "memory-missing" ),
                                        QStringLiteral( "gone" ) );
  const auto missingRegistered = manager->registerSource( missingRequest );
  REQUIRE_FALSE( missingRegistered.assetId.isNull() );

  const auto missing = resolver.resolve( AssetRef{ missingRegistered.assetId },
                                         QStringLiteral( "missing input" ) );
  REQUIRE_FALSE( missing );
  REQUIRE( missing.diagnostics().size() == 1 );
  CHECK( missing.diagnostics().first().code ==
         QStringLiteral( "asset.not_resolvable" ) );

  // No lease is pinned by the rejected resolutions.
  CHECK( manager->leaseCount( missingRegistered.assetId ) == 0 );
}

TEST_CASE( "An asset lacking the required capability is rejected",
           "[processing_asset_resolver]" )
{
  const auto manager = makeDataManager();
  const ProcessingAssetResolver resolver( manager.get() );

  // A vector asset does not declare ReadablePixels.
  RegisterRequest vectorRequest;
  vectorRequest.source = memorySource( QStringLiteral( "memory-vector" ),
                                       QStringLiteral( "vector" ) );
  const auto vectorRegistered = manager->registerSource( vectorRequest );
  REQUIRE_FALSE( vectorRegistered.assetId.isNull() );

  const auto rejected = resolver.resolve( AssetRef{ vectorRegistered.assetId },
                                          QStringLiteral( "pixel read" ),
                                          AssetCapability::ReadablePixels );
  REQUIRE_FALSE( rejected );
  REQUIRE( rejected.diagnostics().size() == 1 );
  CHECK( rejected.diagnostics().first().code ==
         QStringLiteral( "asset.capability_missing" ) );

  // The same asset resolves when the required capability is one it declares.
  const auto accepted = resolver.resolve( AssetRef{ vectorRegistered.assetId },
                                          QStringLiteral( "feature query" ),
                                          AssetCapability::QueryableFeatures );
  REQUIRE( accepted );
}
