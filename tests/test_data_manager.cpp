#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/data_result.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetId;
using sicnu::data::AssetCapability;
using sicnu::data::AssetCapabilities;
using sicnu::data::AssetKind;
using sicnu::data::AssetQuery;
using sicnu::data::AssetRevision;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::PersistencePolicy;
using sicnu::data::RegisterRequest;
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
