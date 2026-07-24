#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFileInfo>
#include <QString>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/internal/source_provider.h"
#include "data/internal/source_provider_registry.h"
#include "data/providers/gdal_raster_source_provider.h"
#include "data/providers/ogr_vector_source_provider.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetCapability;
using sicnu::data::AssetKind;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;
using sicnu::data::RegisterRequest;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::providers::GdalRasterSourceProvider;
using sicnu::data::providers::OgrVectorSourceProvider;

namespace
{

/// Resolve a fixture path relative to this source file (tests/ -> ../data).
QString fixturePath( const QString &relative )
{
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

SourceDescriptor gdalDescriptor( const QString &path )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "gdal" );
  descriptor.canonicalSource = path;
  return descriptor;
}

SourceDescriptor ogrDescriptor( const QString &path )
{
  SourceDescriptor descriptor;
  descriptor.providerKey = QStringLiteral( "ogr" );
  descriptor.canonicalSource = path;
  return descriptor;
}

} // namespace

TEST_CASE( "GeoTIFF raster resolves structural metadata and capabilities",
           "[data_source_providers]" )
{
  const GdalRasterSourceProvider provider;
  const SourceDescriptor source =
    gdalDescriptor( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );

  REQUIRE( provider.supports( source ) );
  const auto resolved = provider.resolve( source );
  REQUIRE( resolved );

  const ResolvedSource &value = resolved.value();
  CHECK( value.kind == AssetKind::Raster );
  CHECK( value.state == AssetState::Ready );
  CHECK( value.storageKind == StorageKind::File );
  CHECK( value.capabilities.testFlag( AssetCapability::Renderable ) );
  CHECK( value.capabilities.testFlag( AssetCapability::ReadablePixels ) );
  CHECK( value.capabilities.testFlag( AssetCapability::BandMetadata ) );
  CHECK( value.capabilities.testFlag( AssetCapability::BandStatistics ) );
  CHECK( value.capabilities.testFlag( AssetCapability::Relocatable ) );
}

TEST_CASE( "OGR vector resolves structural metadata and capabilities",
           "[data_source_providers]" )
{
  const OgrVectorSourceProvider provider;
  const SourceDescriptor source =
    ogrDescriptor( fixturePath( QStringLiteral( "test_vectors.geojson" ) ) );

  REQUIRE( provider.supports( source ) );
  const auto resolved = provider.resolve( source );
  REQUIRE( resolved );

  const ResolvedSource &value = resolved.value();
  CHECK( value.kind == AssetKind::Vector );
  CHECK( value.state == AssetState::Ready );
  CHECK( value.storageKind == StorageKind::File );
  CHECK( value.capabilities.testFlag( AssetCapability::Renderable ) );
  CHECK( value.capabilities.testFlag( AssetCapability::QueryableFeatures ) );
  CHECK( value.capabilities.testFlag( AssetCapability::EditableFeatures ) );
  CHECK_FALSE( value.displayName.isEmpty() );
}

TEST_CASE( "Providers produce a normalized canonical SourceKey", "[data_source_providers]" )
{
  const GdalRasterSourceProvider provider;
  const QString realPath = fixturePath( QStringLiteral( "samples/dem_sample.tif" ) );

  const auto resolved = provider.resolve( gdalDescriptor( realPath ) );
  REQUIRE( resolved );

  const QString canonical = resolved.value().canonicalSource;
  CHECK_FALSE( canonical.isEmpty() );
  // The canonical identity is the symlink-resolved absolute path, not the raw
  // input spelling.
  CHECK( canonical == QFileInfo( realPath ).canonicalFilePath() );
}

TEST_CASE( "Same source reached through different path spellings deduplicates",
           "[data_source_providers]" )
{
  DataManager manager;
  const QString realPath = fixturePath( QStringLiteral( "samples/dem_sample.tif" ) );
  const QString dottedPath = QDir( QFileInfo( realPath ).absolutePath() ).filePath(
    QStringLiteral( "../" ) + QFileInfo( realPath ).dir().dirName() + QStringLiteral( "/" ) +
    QFileInfo( realPath ).fileName() );

  RegisterRequest first;
  first.source = gdalDescriptor( realPath );
  const auto firstResult = manager.registerSource( first );
  REQUIRE_FALSE( firstResult.assetId.isNull() );

  RegisterRequest second;
  second.source = gdalDescriptor( dottedPath );
  const auto secondResult = manager.registerSource( second );

  // The two spellings normalize to the same canonical path, so the asset is
  // reused rather than duplicated.
  CHECK( secondResult.assetId == firstResult.assetId );
  CHECK( secondResult.reusedExisting );
  CHECK( manager.assets().size() == 1 );
}

TEST_CASE( "ENVI header and its paired binary resolve to one SourceKey",
           "[data_source_providers]" )
{
  const GdalRasterSourceProvider provider;
  const QString headerPath = fixturePath( QStringLiteral( "dem.hdr" ) );
  const QString binaryPath = fixturePath( QStringLiteral( "dem.dat" ) );

  REQUIRE( QFileInfo( headerPath ).exists() );
  REQUIRE( QFileInfo( binaryPath ).exists() );

  const auto fromHeader = provider.resolve( gdalDescriptor( headerPath ) );
  const auto fromBinary = provider.resolve( gdalDescriptor( binaryPath ) );
  REQUIRE( fromHeader );
  REQUIRE( fromBinary );

  // Both spellings collapse onto the binary data file as the canonical identity.
  CHECK( fromHeader.value().canonicalSource == fromBinary.value().canonicalSource );
  CHECK( QFileInfo( fromHeader.value().canonicalSource ).fileName() ==
         QStringLiteral( "dem.dat" ) );
}

TEST_CASE( "A missing source registers as Missing rather than disappearing",
           "[data_source_providers]" )
{
  DataManager manager;
  const SourceDescriptor missing = gdalDescriptor( fixturePath( QStringLiteral( "does-not-exist.tif" ) ) );

  const auto result = manager.registerSource( RegisterRequest{ missing } );
  REQUIRE_FALSE( result.assetId.isNull() );

  const auto asset = manager.asset( result.assetId );
  REQUIRE( asset.has_value() );
  CHECK( asset->state() == AssetState::Missing );
  CHECK( asset->kind() == AssetKind::Raster );
  // The asset remains in the catalog despite the missing source.
  CHECK( manager.assets().size() == 1 );
}

TEST_CASE( "Provider results carry no renderer state or credentials",
           "[data_source_providers]" )
{
  const GdalRasterSourceProvider rasterProvider;
  SourceDescriptor source =
    gdalDescriptor( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) );
  // A caller may attach an auth binding; it must not leak into the resolved
  // structural metadata.
  source.authConfigId = QStringLiteral( "secret-auth-id" );

  const auto resolved = rasterProvider.resolve( source );
  REQUIRE( resolved );
  const ResolvedSource &value = resolved.value();

  // ResolvedSource has no renderer/credentials fields by construction. The
  // capability set stays data-only, and the canonical identity is the resolved
  // path with no embedded credential.
  CHECK( value.capabilities.testFlag( AssetCapability::Renderable ) );
  CHECK_FALSE( value.canonicalSource.contains( QStringLiteral( "secret" ) ) );
  CHECK_FALSE( value.canonicalSource.contains( source.authConfigId ) );

  // And via the manager: the same source registered twice with different auth
  // bindings still deduplicates (auth is not part of SourceKey).
  DataManager manager;
  RegisterRequest first;
  first.source = source;
  const auto firstResult = manager.registerSource( first );

  RegisterRequest second;
  second.source = source;
  second.source.authConfigId = QStringLiteral( "different-secret-auth-id" );
  const auto secondResult = manager.registerSource( second );

  CHECK( secondResult.assetId == firstResult.assetId );
  CHECK( secondResult.reusedExisting );
}

TEST_CASE( "Providers resolve only structural metadata, not statistics",
           "[data_source_providers]" )
{
  const GdalRasterSourceProvider provider;
  const auto resolved = provider.resolve(
    gdalDescriptor( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ) ) );
  REQUIRE( resolved );

  // Structural resolution must not emit diagnostics about expensive metadata
  // (histograms/percentiles). The resolve either succeeds cleanly or reports an
  // open error — never a statistics-computation message.
  for ( const auto &diagnostic : resolved.diagnostics() )
  {
    CHECK_FALSE( diagnostic.code.contains( QStringLiteral( "statistic" ) ) );
    CHECK_FALSE( diagnostic.code.contains( QStringLiteral( "histogram" ) ) );
  }
}
