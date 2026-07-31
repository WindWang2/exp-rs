#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <type_traits>
#include <QFile>
#include <QTemporaryFile>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/data_result.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetId;
using sicnu::data::AssetLease;
using sicnu::data::AssetCapability;
using sicnu::data::AssetCapabilities;
using sicnu::data::AssetKind;
using sicnu::data::AssetQuery;
using sicnu::data::AssetRef;
using sicnu::data::AssetRevision;
using sicnu::data::AssetState;
using sicnu::data::AssetUse;
using sicnu::data::DataManager;
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::LeaseImpact;
using sicnu::data::LeaseKind;
using sicnu::data::LeaseOutcome;
using sicnu::data::LeaseRef;
using sicnu::data::PersistencePolicy;
using sicnu::data::RasterStructure;
using sicnu::data::RegisterRequest;
using sicnu::data::RegisterResult;
using sicnu::data::RelocateRequest;
using sicnu::data::Result;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::UnloadPlan;
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
      if ( source.providerKey == QStringLiteral( "memory-error" ) )
      {
        return Result<ResolvedSource>::failure(
          QVector<Diagnostic>{
            Diagnostic{ QStringLiteral( "source.invalid" ),
                        QStringLiteral( "The source descriptor is invalid" ),
                        DiagnosticSeverity::Error },
            Diagnostic{ QStringLiteral( "source.check-uri" ),
                        QStringLiteral( "Check the source URI" ),
                        DiagnosticSeverity::Info } } );
      }

      if ( source.providerKey == QStringLiteral( "memory-vector" ) )
      {
        return Result<ResolvedSource>::success(
          ResolvedSource{ AssetKind::Vector,
                          AssetState::Ready,
                          AssetCapability::Renderable | AssetCapability::QueryableFeatures,
                          StorageKind::Memory,
                          QStringLiteral( "Test vector" ) } );
      }

      if ( source.providerKey == QStringLiteral( "memory-remote" ) )
      {
        return Result<ResolvedSource>::success(
          ResolvedSource{ AssetKind::RemoteMap,
                          AssetState::Offline,
                          AssetCapability::Renderable | AssetCapability::OfflineCacheable,
                          StorageKind::Remote,
                          QStringLiteral( "Test remote map" ) } );
      }

      // Structured raster used by relocation tests. The shape is keyed off the
      // source string so two spellings with the same shape are compatible, while
      // a "-wide" spelling reports a wider raster and is therefore incompatible.
      if ( source.providerKey == QStringLiteral( "memory-structured-raster" ) )
      {
        RasterStructure structure;
        structure.driverName = QStringLiteral( "GTiff" );
        structure.width = source.canonicalSource.endsWith( QStringLiteral( "-wide" ) ) ? 200 : 100;
        structure.height = 50;
        structure.bandCount = 3;

        return Result<ResolvedSource>::success(
          ResolvedSource{ AssetKind::Raster,
                          AssetState::Ready,
                          AssetCapability::Renderable | AssetCapability::ReadablePixels,
                          StorageKind::Memory,
                          QStringLiteral( "Structured raster" ),
                          QString(),
                          QString(),
                          structure } );
      }

      return Result<ResolvedSource>::success(
        ResolvedSource{ AssetKind::Raster,
                        AssetState::Ready,
                        AssetCapability::Renderable | AssetCapability::ReadablePixels,
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

SourceDescriptor memoryRaster( const QString &source )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "memory-raster" );
  descriptor.canonicalSource = source;
  return descriptor;
}

SourceDescriptor memorySource( const QString &providerKey, const QString &source )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = providerKey;
  descriptor.canonicalSource = source;
  return descriptor;
}

} // namespace

TEST_CASE( "Asset IDs survive project serialization", "[data_manager]" )
{
  const AssetId original = AssetId::generate();

  REQUIRE_FALSE( original.isNull() );

  const auto restored = AssetId::fromString( original.toString() );
  REQUIRE( restored.has_value() );
  CHECK( *restored == original );
  CHECK_FALSE( AssetId::fromString( QStringLiteral( "not-an-asset-id" ) ).has_value() );
}

TEST_CASE( "Asset revisions distinguish unresolved from first resolved data", "[data_manager]" )
{
  const AssetRevision unresolved;
  const AssetRevision first = AssetRevision::initial();

  CHECK_FALSE( unresolved.isValid() );
  CHECK( first.isValid() );
  CHECK( first.value() == 1 );
}

TEST_CASE( "Source identity excludes authentication binding but includes data options",
           "[data_manager]" )
{
  SourceDescriptor first;
  first.providerKey = QStringLiteral( "gdal" );
  first.canonicalSource = QStringLiteral( "/data/scene.tif" );
  first.dataOptions.insert( QStringLiteral( "interpretation" ), QStringLiteral( "raw" ) );
  first.authConfigId = QStringLiteral( "auth-on-this-machine" );

  SourceDescriptor sameData = first;
  sameData.authConfigId = QStringLiteral( "auth-on-another-machine" );
  CHECK( sameData.sourceKey() == first.sourceKey() );

  SourceDescriptor differentInterpretation = first;
  differentInterpretation.dataOptions[QStringLiteral( "interpretation" )] =
    QStringLiteral( "scaled" );
  CHECK_FALSE( differentInterpretation.sourceKey() == first.sourceKey() );
}

TEST_CASE( "Asset capabilities compose without implying unsupported operations",
           "[data_manager]" )
{
  const AssetCapabilities rasterCapabilities =
    AssetCapability::Renderable | AssetCapability::ReadablePixels |
    AssetCapability::BandMetadata;

  CHECK( rasterCapabilities.testFlag( AssetCapability::Renderable ) );
  CHECK( rasterCapabilities.testFlag( AssetCapability::ReadablePixels ) );
  CHECK_FALSE( rasterCapabilities.testFlag( AssetCapability::EditableFeatures ) );
}

TEST_CASE( "Data errors are returned as structured diagnostics", "[data_manager]" )
{
  const Result<int> failed = Result<int>::failure(
    Diagnostic{ QStringLiteral( "source.missing" ),
                QStringLiteral( "The source cannot be found" ),
                DiagnosticSeverity::Error } );

  REQUIRE_FALSE( failed );
  REQUIRE( failed.diagnostics().size() == 1 );
  CHECK( failed.diagnostics().first().code == QStringLiteral( "source.missing" ) );
  CHECK( failed.diagnostics().first().severity == DiagnosticSeverity::Error );

  const Result<int> succeeded = Result<int>::success( 42 );
  REQUIRE( succeeded );
  CHECK( succeeded.value() == 42 );
}

TEST_CASE( "Registering a source makes an asset retrievable by its ID", "[data_manager]" )
{
  const auto manager = makeDataManager();
  RegisterRequest request;
  request.source = memoryRaster( QStringLiteral( "scene-a" ) );
  request.persistence = PersistencePolicy::ProjectPersistent;

  const auto registered = manager->registerSource( request );

  REQUIRE_FALSE( registered.assetId.isNull() );
  CHECK_FALSE( registered.reusedExisting );
  REQUIRE( registered.diagnostics.isEmpty() );

  const auto asset = manager->asset( registered.assetId );
  REQUIRE( asset.has_value() );
  CHECK( asset->id() == registered.assetId );
  CHECK( asset->revision() == AssetRevision::initial() );
  CHECK( asset->kind() == AssetKind::Raster );
  CHECK( asset->state() == AssetState::Ready );
  CHECK( asset->storageKind() == StorageKind::Memory );
  CHECK( asset->persistence() == PersistencePolicy::ProjectPersistent );
  CHECK( asset->displayName() == QStringLiteral( "Test raster" ) );

  SourceDescriptor callerCopy = asset->source();
  callerCopy.canonicalSource = QStringLiteral( "caller-only-change" );
  const auto unchanged = manager->asset( registered.assetId );
  REQUIRE( unchanged.has_value() );
  CHECK( unchanged->source().canonicalSource == QStringLiteral( "scene-a" ) );
}

TEST_CASE( "Registering the same source reuses one asset and emits one addition",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  int additions = 0;
  AssetId addedId;
  QObject::connect( manager.get(), &DataManager::assetAdded, [&]( AssetId id ) {
    ++additions;
    addedId = id;
  } );

  RegisterRequest firstRequest;
  firstRequest.source = memoryRaster( QStringLiteral( "scene-a" ) );
  const auto first = manager->registerSource( firstRequest );

  RegisterRequest repeatedRequest = firstRequest;
  repeatedRequest.source.authConfigId = QStringLiteral( "different-machine-auth" );
  repeatedRequest.persistence = PersistencePolicy::SessionTemporary;
  const auto repeated = manager->registerSource( repeatedRequest );

  REQUIRE_FALSE( first.assetId.isNull() );
  CHECK( repeated.assetId == first.assetId );
  CHECK( repeated.reusedExisting );
  CHECK( additions == 1 );
  CHECK( addedId == first.assetId );
  CHECK( manager->assets().size() == 1 );
}

TEST_CASE( "Asset queries filter data kind state and persistence", "[data_manager]" )
{
  const auto manager = makeDataManager();

  RegisterRequest raster;
  raster.source = memoryRaster( QStringLiteral( "raster" ) );
  raster.persistence = PersistencePolicy::ProjectPersistent;
  REQUIRE_FALSE( manager->registerSource( raster ).assetId.isNull() );

  RegisterRequest vector;
  vector.source =
    memorySource( QStringLiteral( "memory-vector" ), QStringLiteral( "vector" ) );
  vector.persistence = PersistencePolicy::TaskTemporary;
  REQUIRE_FALSE( manager->registerSource( vector ).assetId.isNull() );

  RegisterRequest remote;
  remote.source =
    memorySource( QStringLiteral( "memory-remote" ), QStringLiteral( "remote" ) );
  remote.persistence = PersistencePolicy::SessionTemporary;
  REQUIRE_FALSE( manager->registerSource( remote ).assetId.isNull() );

  AssetQuery vectors;
  vectors.kind = AssetKind::Vector;
  REQUIRE( manager->assets( vectors ).size() == 1 );
  CHECK( manager->assets( vectors ).first().displayName() ==
         QStringLiteral( "Test vector" ) );

  AssetQuery offline;
  offline.state = AssetState::Offline;
  REQUIRE( manager->assets( offline ).size() == 1 );
  CHECK( manager->assets( offline ).first().kind() == AssetKind::RemoteMap );

  AssetQuery temporaryRasters;
  temporaryRasters.kind = AssetKind::Raster;
  temporaryRasters.persistence = PersistencePolicy::TaskTemporary;
  CHECK( manager->assets( temporaryRasters ).isEmpty() );
}

TEST_CASE( "Provider resolution errors return diagnostics without catalog side effects",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  int additions = 0;
  QObject::connect( manager.get(), &DataManager::assetAdded, [&]( AssetId ) {
    ++additions;
  } );

  RegisterRequest request;
  request.source =
    memorySource( QStringLiteral( "memory-error" ), QStringLiteral( "invalid" ) );
  const auto result = manager->registerSource( request );

  CHECK( result.assetId.isNull() );
  CHECK_FALSE( result.reusedExisting );
  REQUIRE( result.diagnostics.size() == 2 );
  CHECK( result.diagnostics.at( 0 ).code == QStringLiteral( "source.invalid" ) );
  CHECK( result.diagnostics.at( 0 ).severity == DiagnosticSeverity::Error );
  CHECK( result.diagnostics.at( 1 ).code == QStringLiteral( "source.check-uri" ) );
  CHECK( result.diagnostics.at( 1 ).severity == DiagnosticSeverity::Info );
  CHECK( additions == 0 );
  CHECK( manager->assets().isEmpty() );
}

TEST_CASE( "Catalog mutations are rejected outside the Data Manager owning thread",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  RegisterResult result;

  std::thread worker( [&] {
    RegisterRequest request;
    request.source = memoryRaster( QStringLiteral( "worker-thread-source" ) );
    result = manager->registerSource( request );
  } );
  worker.join();

  CHECK( result.assetId.isNull() );
  REQUIRE( result.diagnostics.size() == 1 );
  CHECK( result.diagnostics.first().code == QStringLiteral( "data.wrong_thread" ) );
  CHECK( manager->assets().isEmpty() );
}

namespace
{

AssetId registerOneRaster( const DataManager *manager, DataManager *nonConstManager )
{
  RegisterRequest request;
  request.source = memoryRaster( QStringLiteral( "scene" ) );
  const auto registered = nonConstManager->registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  ( void ) manager;
  return registered.assetId;
}

} // namespace

TEST_CASE( "Acquiring a view task or edit lease counts against the asset", "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneRaster( manager.get(), manager.get() );

  const auto view =
    manager->acquire( AssetRef{ id, AssetRevision::initial() }, AssetUse{ LeaseKind::View } );
  REQUIRE( view );
  CHECK( view.value().isValid() );
  CHECK( view.value().assetId() == id );
  CHECK( view.value().kind() == LeaseKind::View );
  CHECK( manager->leaseCount( id ) == 1 );

  const auto task = manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Task } );
  REQUIRE( task );
  CHECK( manager->leaseCount( id ) == 2 );

  const auto edit = manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } );
  REQUIRE( edit );
  CHECK( manager->leaseCount( id ) == 3 );

  const QVector<LeaseRef> refs = manager->leases( id );
  REQUIRE( refs.size() == 3 );
  CHECK( refs.at( 0 ).kind == LeaseKind::View );
  CHECK( refs.at( 1 ).kind == LeaseKind::Task );
  CHECK( refs.at( 2 ).kind == LeaseKind::Edit );
}

TEST_CASE( "An asset lease is move-only and releases itself through RAII",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneRaster( manager.get(), manager.get() );

  AssetLease moved = manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::View } ).take();
  CHECK( moved.isValid() );
  CHECK( manager->leaseCount( id ) == 1 );

  AssetLease reassigned;
  reassigned = std::move( moved );
  CHECK( reassigned.isValid() );
  CHECK( manager->leaseCount( id ) == 1 );

  // Releasing explicitly returns Released and drops the only record.
  CHECK( reassigned.release() == LeaseOutcome::Released );
  CHECK_FALSE( reassigned.isValid() );
  CHECK( manager->leaseCount( id ) == 0 );

  // A second release on an already-released lease is a safe no-op.
  CHECK( reassigned.release() == LeaseOutcome::Invalid );

  AssetLease empty;
  CHECK_FALSE( empty.isValid() );
  CHECK( empty.release() == LeaseOutcome::Invalid );
}

TEST_CASE( "Unloading a leased asset reports every active lease and is rejected",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneRaster( manager.get(), manager.get() );

  AssetLease view =
    manager->acquire( AssetRef{ id },
                      AssetUse{ LeaseKind::View, QStringLiteral( "main view" ) } )
      .take();
  REQUIRE( view.isValid() );
  const auto task =
    manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Task, QStringLiteral( "ndvi" ) } );
  REQUIRE( task );

  const UnloadPlan plan = manager->planUnload( id );
  CHECK( plan.assetId() == id );
  CHECK( plan.revision() == AssetRevision::initial() );
  CHECK( plan.activeLeases().size() == 2 );
  CHECK_FALSE( plan.canUnload() );

  const Result<void> unloadResult = manager->unload( plan );
  CHECK_FALSE( unloadResult );
  REQUIRE( unloadResult.diagnostics().size() >= 3 );
  CHECK( unloadResult.diagnostics().first().code == QStringLiteral( "unload.leased" ) );
  CHECK( manager->leaseCount( id ) == 2 );
  REQUIRE( manager->asset( id ).has_value() );
}

TEST_CASE( "Unload plans are read-only impact snapshots", "[data_manager]" )
{
  STATIC_CHECK_FALSE( std::is_aggregate_v<UnloadPlan> );

  const auto manager = makeDataManager();
  const AssetId id = registerOneRaster( manager.get(), manager.get() );
  const auto view =
    manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::View, QStringLiteral( "main view" ) } );
  REQUIRE( view );

  const UnloadPlan plan = manager->planUnload( id );
  CHECK( plan.assetId() == id );
  CHECK( plan.revision() == AssetRevision::initial() );
  REQUIRE( plan.activeLeases().size() == 1 );
  CHECK( plan.activeLeases().first().purpose == QStringLiteral( "main view" ) );
  CHECK_FALSE( plan.cascade() );
}

TEST_CASE( "A confirmed cascade unload revokes leases and removes the asset once",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneRaster( manager.get(), manager.get() );

  AssetLease view =
    manager->acquire( AssetRef{ id },
                      AssetUse{ LeaseKind::View, QStringLiteral( "main view" ) } )
      .take();
  REQUIRE( view.isValid() );

  int aboutToUnload = 0;
  int removed = 0;
  AssetId unloadedId;
  AssetId removedId;
  QObject::connect( manager.get(), &DataManager::assetAboutToUnload,
                    [&]( AssetId emitted ) {
                      ++aboutToUnload;
                      unloadedId = emitted;
                    } );
  QObject::connect( manager.get(), &DataManager::assetRemoved, [&]( AssetId emitted ) {
    ++removed;
    removedId = emitted;
  } );

  const UnloadPlan plan = manager->planUnload( id ).confirmedCascade();
  CHECK( plan.cascade() );
  CHECK( plan.canUnload() );

  const Result<void> unloadResult = manager->unload( plan );
  REQUIRE( unloadResult );
  CHECK( aboutToUnload == 1 );
  CHECK( unloadedId == id );
  CHECK( removed == 1 );
  CHECK( removedId == id );
  CHECK_FALSE( manager->asset( id ).has_value() );
  CHECK( manager->leaseCount( id ) == 0 );
  CHECK_FALSE( view.isValid() );
  CHECK( view.release() == LeaseOutcome::Invalid );
}

TEST_CASE( "A stale unload plan is rejected after the catalog changes",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId firstId = registerOneRaster( manager.get(), manager.get() );

  const UnloadPlan stalePlan = manager->planUnload( firstId );

  RegisterRequest second;
  second.source = memoryRaster( QStringLiteral( "scene-b" ) );
  REQUIRE_FALSE( manager->registerSource( second ).assetId.isNull() );

  const Result<void> unloadResult = manager->unload( stalePlan );
  CHECK_FALSE( unloadResult );
  REQUIRE( unloadResult.diagnostics().size() == 1 );
  CHECK( unloadResult.diagnostics().first().code == QStringLiteral( "unload.stale_plan" ) );
  REQUIRE( manager->asset( firstId ).has_value() );
}

TEST_CASE( "Ordinary unload has no source-deletion side effect",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneRaster( manager.get(), manager.get() );

  const UnloadPlan plan = manager->planUnload( id );
  CHECK( plan.canUnload() );

  const Result<void> unloadResult = manager->unload( plan );
  REQUIRE( unloadResult );

  // The catalog no longer tracks the asset, but the underlying source string
  // (the simulated file) is untouched and can be re-registered.
  CHECK_FALSE( manager->asset( id ).has_value() );

  RegisterRequest again;
  again.source = memoryRaster( QStringLiteral( "scene" ) );
  const auto reregistered = manager->registerSource( again );
  CHECK_FALSE( reregistered.assetId.isNull() );
  CHECK_FALSE( reregistered.reusedExisting );
}

namespace
{

AssetId registerStructuredRaster( DataManager *manager, const QString &source )
{
  RegisterRequest request;
  request.source = memorySource( QStringLiteral( "memory-structured-raster" ), source );
  const auto registered = manager->registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  return registered.assetId;
}

} // namespace

TEST_CASE( "Relocating an asset preserves its ID and advances the revision once",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerStructuredRaster( manager.get(), QStringLiteral( "scene-a" ) );

  int changeEvents = 0;
  AssetId changedId;
  QObject::connect( manager.get(), &DataManager::assetChanged, [&]( AssetId emitted ) {
    ++changeEvents;
    changedId = emitted;
  } );

  RelocateRequest relocate;
  relocate.id = id;
  relocate.replacement =
    memorySource( QStringLiteral( "memory-structured-raster" ), QStringLiteral( "scene-b" ) );
  const auto result = manager->relocate( relocate );

  REQUIRE( result );
  CHECK( result.value().assetId == id );
  CHECK( result.value().revision == AssetRevision::initial().next() );

  const auto asset = manager->asset( id );
  REQUIRE( asset.has_value() );
  CHECK( asset->id() == id );
  CHECK( asset->revision() == AssetRevision::initial().next() );
  CHECK( asset->source().canonicalSource == QStringLiteral( "scene-b" ) );
  CHECK( asset->state() == AssetState::Ready );

  CHECK( changeEvents == 1 );
  CHECK( changedId == id );

  // The relocation recomputed the SourceKey index in place: no second asset.
  CHECK( manager->assets().size() == 1 );
}

TEST_CASE( "Relocating to the moved source's new spelling deduplicates the identity",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerStructuredRaster( manager.get(), QStringLiteral( "scene-a" ) );

  RelocateRequest relocate;
  relocate.id = id;
  relocate.replacement =
    memorySource( QStringLiteral( "memory-structured-raster" ), QStringLiteral( "scene-b" ) );
  REQUIRE( manager->relocate( relocate ) );

  // Re-registering the relocated source now reuses the same Asset ID, proving
  // the SourceKey index was recomputed rather than duplicated.
  RegisterRequest again;
  again.source =
    memorySource( QStringLiteral( "memory-structured-raster" ), QStringLiteral( "scene-b" ) );
  const auto reregistered = manager->registerSource( again );
  CHECK( reregistered.assetId == id );
  CHECK( reregistered.reusedExisting );
  CHECK( manager->assets().size() == 1 );
}

TEST_CASE( "Relocation validates structure and rejects an incompatible replacement",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerStructuredRaster( manager.get(), QStringLiteral( "scene-a" ) );
  const AssetRevision originalRevision = manager->asset( id )->revision();

  int changeEvents = 0;
  QObject::connect( manager.get(), &DataManager::assetChanged, [&]( AssetId ) {
    ++changeEvents;
  } );

  RelocateRequest relocate;
  relocate.id = id;
  relocate.replacement = memorySource( QStringLiteral( "memory-structured-raster" ),
                                       QStringLiteral( "scene-wide" ) );
  const auto result = manager->relocate( relocate );

  REQUIRE_FALSE( result );
  REQUIRE( result.diagnostics().size() == 1 );
  CHECK( result.diagnostics().first().code ==
         QStringLiteral( "relocate.structure_mismatch" ) );

  // A rejected relocation mutates nothing and emits no change event.
  const auto asset = manager->asset( id );
  REQUIRE( asset.has_value() );
  CHECK( asset->revision() == originalRevision );
  CHECK( asset->source().canonicalSource == QStringLiteral( "scene-a" ) );
  CHECK( changeEvents == 0 );
}

TEST_CASE( "Relocation rejects a replacement of a different data kind",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerStructuredRaster( manager.get(), QStringLiteral( "scene-a" ) );

  RelocateRequest relocate;
  relocate.id = id;
  relocate.replacement =
    memorySource( QStringLiteral( "memory-vector" ), QStringLiteral( "scene-a" ) );
  const auto result = manager->relocate( relocate );

  REQUIRE_FALSE( result );
  REQUIRE( result.diagnostics().size() == 1 );
  CHECK( result.diagnostics().first().code == QStringLiteral( "relocate.kind_mismatch" ) );
  CHECK( manager->asset( id )->source().canonicalSource == QStringLiteral( "scene-a" ) );
}

TEST_CASE( "Relocation is rejected when the replacement belongs to another asset",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId first = registerStructuredRaster( manager.get(), QStringLiteral( "scene-a" ) );
  const AssetId second = registerStructuredRaster( manager.get(), QStringLiteral( "scene-b" ) );
  REQUIRE( first != second );

  RelocateRequest relocate;
  relocate.id = first;
  relocate.replacement =
    memorySource( QStringLiteral( "memory-structured-raster" ), QStringLiteral( "scene-b" ) );
  const auto result = manager->relocate( relocate );

  REQUIRE_FALSE( result );
  REQUIRE( result.diagnostics().size() == 1 );
  CHECK( result.diagnostics().first().code == QStringLiteral( "relocate.source_conflict" ) );
  CHECK( manager->assets().size() == 2 );
}

TEST_CASE( "Relocating an unknown asset is rejected without side effects",
           "[data_manager]" )
{
  const auto manager = makeDataManager();

  RelocateRequest relocate;
  relocate.id = AssetId::generate();
  relocate.replacement =
    memorySource( QStringLiteral( "memory-structured-raster" ), QStringLiteral( "scene" ) );
  const auto result = manager->relocate( relocate );

  REQUIRE_FALSE( result );
  REQUIRE( result.diagnostics().size() == 1 );
  CHECK( result.diagnostics().first().code == QStringLiteral( "relocate.unknown_asset" ) );
  CHECK( manager->assets().isEmpty() );
}

namespace
{

AssetId registerOneVector( DataManager *manager )
{
  RegisterRequest request;
  request.source = memorySource( QStringLiteral( "memory-vector" ), QStringLiteral( "vector" ) );
  const auto registered = manager->registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  return registered.assetId;
}

} // namespace

TEST_CASE( "Only one Edit Lease may exist per Vector Asset", "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneVector( manager.get() );

  auto firstEdit = manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } );
  REQUIRE( firstEdit );

  // A second Edit Lease on the same asset is rejected.
  const auto secondEdit = manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } );
  REQUIRE_FALSE( secondEdit );
  REQUIRE( secondEdit.diagnostics().size() == 1 );
  CHECK( secondEdit.diagnostics().first().code ==
         QStringLiteral( "asset.edit_lease_conflict" ) );

  // View and Task leases are unconstrained by the Edit exclusivity.
  CHECK( manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::View } ) );
  CHECK( manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Task } ) );

  // After the Edit Lease is released, editing can begin again.
  AssetLease released = firstEdit.take();
  CHECK( released.release() == LeaseOutcome::Released );
  CHECK( manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } ) );
}

TEST_CASE( "Committing an edit advances the revision and emits one change event",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneVector( manager.get() );

  int changeEvents = 0;
  AssetId changedId;
  QObject::connect( manager.get(), &DataManager::assetChanged, [&]( AssetId emitted ) {
    ++changeEvents;
    changedId = emitted;
  } );

  AssetLease editLease = manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } ).take();
  CHECK( manager->leases( id ).size() == 1 );

  const auto committed = manager->commitEdit( id );
  REQUIRE( committed );

  // The revision advanced and the Edit Lease was released.
  const auto asset = manager->asset( id );
  REQUIRE( asset.has_value() );
  CHECK( asset->revision() == AssetRevision::initial().next() );
  CHECK( manager->leases( id ).isEmpty() );

  CHECK( changeEvents == 1 );
  CHECK( changedId == id );

  // Editing can begin again after the commit released the lease.
  CHECK( manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } ) );
}

TEST_CASE( "Rolling back an edit releases the lease without advancing the revision",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneVector( manager.get() );

  int changeEvents = 0;
  QObject::connect( manager.get(), &DataManager::assetChanged, [&]( AssetId ) {
    ++changeEvents;
  } );

  AssetLease editLease = manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } ).take();
  const auto rolledBack = manager->rollbackEdit( id );
  REQUIRE( rolledBack );

  const auto asset = manager->asset( id );
  REQUIRE( asset.has_value() );
  CHECK( asset->revision() == AssetRevision::initial() );
  CHECK( manager->leases( id ).isEmpty() );
  CHECK( changeEvents == 0 );

  // Editing can begin again after the rollback released the lease.
  CHECK( manager->acquire( AssetRef{ id }, AssetUse{ LeaseKind::Edit } ) );
}

TEST_CASE( "Commit or rollback without an active Edit Lease is rejected",
           "[data_manager]" )
{
  const auto manager = makeDataManager();
  const AssetId id = registerOneVector( manager.get() );

  const auto committed = manager->commitEdit( id );
  REQUIRE_FALSE( committed );
  CHECK( committed.diagnostics().first().code == QStringLiteral( "asset.no_edit_lease" ) );

  const auto rolledBack = manager->rollbackEdit( id );
  REQUIRE_FALSE( rolledBack );
  CHECK( rolledBack.diagnostics().first().code == QStringLiteral( "asset.no_edit_lease" ) );

  // The revision is untouched by the rejected operations.
  CHECK( manager->asset( id )->revision() == AssetRevision::initial() );
}

TEST_CASE( "DataManager reap deletes file for DeletableSource temporary asset", "[data_manager][reap]" )
{
  QTemporaryFile tempFile;
  REQUIRE( tempFile.open() );
  const QString tempFilePath = tempFile.fileName();
  tempFile.close();
  REQUIRE( QFile::exists( tempFilePath ) );

  const auto manager = makeDataManager();

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "memory-raster" );
  source.canonicalSource = tempFilePath;

  RegisterRequest req;
  req.source = source;
  req.persistence = PersistencePolicy::SessionTemporary;
  req.additionalCapabilities = AssetCapability::DeletableSource;

  const auto regResult = manager->registerSource( req );
  const AssetId id = regResult.assetId;
  REQUIRE_FALSE( id.isNull() );

  int unloadSignalCount = 0;
  QObject::connect( manager.get(), &DataManager::assetAboutToUnload,
                    [&]( AssetId targetId ) {
                      if ( targetId == id )
                        unloadSignalCount++;
                    } );

  const auto reapRes = manager->reap( { id } );
  REQUIRE( reapRes.unloaded );
  REQUIRE( reapRes.sourceDeleted );
  CHECK( unloadSignalCount == 1 );
  CHECK_FALSE( manager->asset( id ).has_value() );
  CHECK_FALSE( QFile::exists( tempFilePath ) );
}

TEST_CASE( "DataManager reap refuses ProjectPersistent and leased assets", "[data_manager][reap]" )
{
  const auto manager = makeDataManager();

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "memory-raster" );
  source.canonicalSource = QStringLiteral( "/tmp/fake_persistent.tif" );

  RegisterRequest req;
  req.source = source;
  req.persistence = PersistencePolicy::ProjectPersistent;
  req.additionalCapabilities = AssetCapability::DeletableSource;

  const auto regResult = manager->registerSource( req );
  const AssetId persistentId = regResult.assetId;

  // Persistent asset refusal
  const auto persistentReap = manager->reap( { persistentId } );
  REQUIRE_FALSE( persistentReap.unloaded );
  REQUIRE_FALSE( persistentReap.sourceDeleted );
  REQUIRE_FALSE( persistentReap.diagnostics.isEmpty() );
  CHECK( persistentReap.diagnostics.first().code == QStringLiteral( "reap.persistent" ) );
  CHECK( manager->asset( persistentId ).has_value() );

  // Leased temporary refusal
  RegisterRequest tempReq;
  tempReq.source = source;
  tempReq.persistence = PersistencePolicy::SessionTemporary;
  tempReq.additionalCapabilities = AssetCapability::DeletableSource;
  const AssetId tempId = manager->registerSource( tempReq ).assetId;

  auto lease = manager->acquire( AssetRef{ tempId }, AssetUse{ LeaseKind::View } ).take();
  const auto leasedReap = manager->reap( { tempId } );
  REQUIRE_FALSE( leasedReap.unloaded );
  REQUIRE_FALSE( leasedReap.sourceDeleted );
  CHECK( manager->asset( tempId ).has_value() );
}

TEST_CASE( "DataManager promote flips policy to ProjectPersistent and preserves identity", "[data_manager][promote]" )
{
  const auto manager = makeDataManager();

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "memory-raster" );
  source.canonicalSource = QStringLiteral( "/tmp/fake_temp.tif" );

  RegisterRequest req;
  req.source = source;
  req.persistence = PersistencePolicy::SessionTemporary;

  const AssetId id = manager->registerSource( req ).assetId;
  REQUIRE( manager->asset( id )->persistence() == PersistencePolicy::SessionTemporary );

  int changeSignalCount = 0;
  QObject::connect( manager.get(), &DataManager::assetChanged,
                    [&]( AssetId targetId ) {
                      if ( targetId == id )
                        changeSignalCount++;
                    } );

  const auto promoteRes = manager->promote( id );
  REQUIRE( promoteRes );
  CHECK( manager->asset( id )->persistence() == PersistencePolicy::ProjectPersistent );
  CHECK( changeSignalCount == 1 );

  // Promoting again is no-op success
  const auto promoteAgain = manager->promote( id );
  REQUIRE( promoteAgain );
  CHECK( changeSignalCount == 1 );
}

TEST_CASE( "DataManager reapSessionTemporaries sweeps idle temporary assets", "[data_manager][reap_sweep]" )
{
  const auto manager = makeDataManager();

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "memory-raster" );
  source.canonicalSource = QStringLiteral( "/tmp/fake_temp_sweep1.tif" );

  SourceDescriptor source2;
  source2.providerKey = QStringLiteral( "memory-raster" );
  source2.canonicalSource = QStringLiteral( "/tmp/fake_temp_sweep2.tif" );

  RegisterRequest req1;
  req1.source = source;
  req1.persistence = PersistencePolicy::SessionTemporary;
  const AssetId id1 = manager->registerSource( req1 ).assetId;

  RegisterRequest req2;
  req2.source = source2;
  req2.persistence = PersistencePolicy::SessionTemporary;
  const AssetId id2 = manager->registerSource( req2 ).assetId;

  // Lease id2 so it is skipped during sweep
  auto lease2 = manager->acquire( AssetRef{ id2 }, AssetUse{ LeaseKind::View } ).take();

  const auto sweepRes = manager->reapSessionTemporaries();
  CHECK( sweepRes.reapedCount == 1 );
  CHECK( sweepRes.skippedLeased.contains( id2 ) );
  CHECK_FALSE( manager->asset( id1 ).has_value() );
  CHECK( manager->asset( id2 ).has_value() );
}

TEST_CASE( "DataManager reapTaskTemporaries sweeps idle TaskTemporary assets and leaves SessionTemporary and ProjectPersistent assets", "[data_manager][reap_sweep]" )
{
  const auto manager = makeDataManager();

  SourceDescriptor source1;
  source1.providerKey = QStringLiteral( "memory-raster" );
  source1.canonicalSource = QStringLiteral( "/tmp/fake_task_temp.tif" );

  SourceDescriptor source2;
  source2.providerKey = QStringLiteral( "memory-raster" );
  source2.canonicalSource = QStringLiteral( "/tmp/fake_session_temp.tif" );

  SourceDescriptor source3;
  source3.providerKey = QStringLiteral( "memory-raster" );
  source3.canonicalSource = QStringLiteral( "/tmp/fake_persistent_temp.tif" );

  RegisterRequest req1;
  req1.source = source1;
  req1.persistence = PersistencePolicy::TaskTemporary;
  const AssetId taskId = manager->registerSource( req1 ).assetId;

  RegisterRequest req2;
  req2.source = source2;
  req2.persistence = PersistencePolicy::SessionTemporary;
  const AssetId sessionId = manager->registerSource( req2 ).assetId;

  RegisterRequest req3;
  req3.source = source3;
  req3.persistence = PersistencePolicy::ProjectPersistent;
  const AssetId persistentId = manager->registerSource( req3 ).assetId;

  const auto sweepRes = manager->reapTaskTemporaries();
  CHECK( sweepRes.reapedCount == 1 );
  CHECK_FALSE( manager->asset( taskId ).has_value() );
  CHECK( manager->asset( sessionId ).has_value() );
  CHECK( manager->asset( persistentId ).has_value() );
}
