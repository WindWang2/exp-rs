#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "vector_test_fixtures.h"
#include <QMap>
#include <QString>
#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal.h>

#include <vector>

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
using sicnu::data::RasterStructure;
using sicnu::data::SourceDescriptor;
using sicnu::data::StorageKind;
using sicnu::data::VectorStructure;
using sicnu::data::internal::ResolvedSource;
using sicnu::data::providers::GdalRasterSourceProvider;
using sicnu::data::providers::OgrVectorSourceProvider;

namespace
{

/// Synthesise a small GeoTIFF per distinct `relative` path and cache them (plus
/// the holding temp dir) for the process lifetime, so tests do not depend on a
/// committed sample raster under data/samples/. Distinct relative paths yield
/// distinct files so the Data Manager does not dedup them by SourceKey. The
/// raster mirrors the committed dem_sample.tif geometry asserted below
/// (256x256, WGS 84, one Float32 band, geotransform origin 116/40, 0.001 px).
QString syntheticSample( const QString &relative )
{
  static QTemporaryDir dir;
  static QMap<QString, QString> cache;
  auto it = cache.constFind( relative );
  if ( it != cache.constEnd() )
    return it.value();

  GDALAllRegister();
  const QString path = dir.path() + QLatin1Char( '/' ) +
                       QString::number( cache.size() ) + QStringLiteral( ".tif" );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 256, H = 256;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 116.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection(
    ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.0f );
  for ( int row = 0; row < H; ++row )
  {
    CPLErr err = GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
    Q_UNUSED( err );
  }
  GDALClose( ds );
  cache.insert( relative, path );
  return path;
}

QString syntheticEnviSample( const QString &relative )
{
  static QTemporaryDir dir;
  static QString hdrPath;
  static QString datPath;
  if ( hdrPath.isEmpty() )
  {
    GDALAllRegister();
    hdrPath = dir.path() + QStringLiteral( "/dem.hdr" );
    datPath = dir.path() + QStringLiteral( "/dem.dat" );
    GDALDriverH driver = GDALGetDriverByName( "ENVI" );
    if ( driver )
    {
      constexpr int W = 16, H = 16;
      GDALDatasetH ds = GDALCreate( driver, datPath.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
      if ( ds )
      {
        double gt[6] = { 116.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
        GDALSetGeoTransform( ds, gt );
        std::vector<float> line( W, 1.0f );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        for ( int r = 0; r < H; ++r )
        {
          CPLErr err = GDALRasterIO( band, GF_Write, 0, r, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
          Q_UNUSED( err );
        }
        GDALClose( ds );
      }
    }
  }
  return relative.endsWith( QLatin1String( ".hdr" ) ) ? hdrPath : datPath;
}

/// Resolve a fixture path relative to this source file (tests/ -> ../data).
/// Sample rasters under data/samples/ (and the legacy phr_xs.tif) are no longer
/// committed; redirect those to a synthesised sample (one per distinct path).
/// Other paths (e.g. does-not-exist.tif) resolve to the real data tree so they
/// stay missing.
QString fixturePath( const QString &relative )
{
  if ( relative.startsWith( QLatin1String( "samples/" ) ) ||
       relative == QLatin1String( "phr_xs.tif" ) )
  {
    return syntheticSample( relative );
  }
  if ( relative.startsWith( QLatin1String( "dem." ) ) )
  {
    return syntheticEnviSample( relative );
  }
  if ( relative == QLatin1String( "test_vectors.geojson" ) )
  {
    return vector_test_fixtures::syntheticGeoJsonPath();
  }
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

  const auto *structure = std::get_if<RasterStructure>( &value.structure );
  REQUIRE( structure != nullptr );
  CHECK( structure->driverName == QStringLiteral( "GTiff" ) );
  CHECK( structure->width == 256 );
  CHECK( structure->height == 256 );
  CHECK( structure->bandCount == 1 );
  REQUIRE( structure->bands.size() == 1 );
  CHECK( structure->bands.first().dataType == QStringLiteral( "Float32" ) );
  CHECK( structure->bands.first().colorInterpretation == QStringLiteral( "Gray" ) );
  CHECK_FALSE( structure->bands.first().noDataValue.has_value() );
  CHECK( structure->crsWkt.contains( QStringLiteral( "WGS 84" ) ) );
  CHECK( structure->hasGeoTransform );
  CHECK( structure->geoTransform.at( 0 ) == Catch::Approx( 116.0 ) );
  CHECK( structure->geoTransform.at( 1 ) == Catch::Approx( 0.001 ) );
  CHECK( structure->geoTransform.at( 3 ) == Catch::Approx( 40.0 ) );
  CHECK( structure->geoTransform.at( 5 ) == Catch::Approx( -0.001 ) );
  CHECK( structure->extent.minimumX == Catch::Approx( 116.0 ) );
  CHECK( structure->extent.maximumX == Catch::Approx( 116.256 ) );
  CHECK( structure->extent.minimumY == Catch::Approx( 39.744 ) );
  CHECK( structure->extent.maximumY == Catch::Approx( 40.0 ) );
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

  const auto *structure = std::get_if<VectorStructure>( &value.structure );
  REQUIRE( structure != nullptr );
  CHECK( structure->driverName == QStringLiteral( "GeoJSON" ) );
  CHECK( structure->layerCount == 1 );
  REQUIRE( structure->layers.size() == 1 );
  const auto &layer = structure->layers.first();
  CHECK( layer.name == QStringLiteral( "test_points" ) );
  CHECK( layer.featureCount == 3 );
  CHECK_FALSE( layer.geometryType.isEmpty() );
  CHECK( layer.crsWkt.contains( QStringLiteral( "WGS 84" ) ) );
  CHECK( layer.extent.valid );
  CHECK( layer.extent.minimumX == Catch::Approx( 5.0 ) );
  CHECK( layer.extent.minimumY == Catch::Approx( 5.0 ) );
  CHECK( layer.extent.maximumX == Catch::Approx( 20.0 ) );
  CHECK( layer.extent.maximumY == Catch::Approx( 20.0 ) );
}

TEST_CASE( "Read-only vector sources do not advertise editable features",
           "[data_source_providers]" )
{
  QTemporaryDir temporaryDirectory;
  REQUIRE( temporaryDirectory.isValid() );
  const QString readOnlyPath =
    temporaryDirectory.filePath( QStringLiteral( "readonly.geojson" ) );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "test_vectors.geojson" ) ),
                        readOnlyPath ) );
  REQUIRE( QFile::setPermissions(
    readOnlyPath,
    QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther ) );

  const OgrVectorSourceProvider provider;
  const auto resolved = provider.resolve( ogrDescriptor( readOnlyPath ) );

  REQUIRE( resolved );
  CHECK( resolved.value().state == AssetState::Ready );
  CHECK_FALSE(
    resolved.value().capabilities.testFlag( AssetCapability::EditableFeatures ) );
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

TEST_CASE( "Equivalent raster provider hints share one canonical SourceKey",
           "[data_source_providers]" )
{
  DataManager manager;
  const QString path = fixturePath( QStringLiteral( "samples/dem_sample.tif" ) );

  SourceDescriptor inferred;
  inferred.canonicalSource = path;
  const auto first = manager.registerSource( RegisterRequest{ inferred } );
  REQUIRE_FALSE( first.assetId.isNull() );

  SourceDescriptor explicitGdal = inferred;
  explicitGdal.providerKey = QStringLiteral( "gdal" );
  const auto second = manager.registerSource( RegisterRequest{ explicitGdal } );

  SourceDescriptor rasterAlias = inferred;
  rasterAlias.providerKey = QStringLiteral( "raster" );
  const auto third = manager.registerSource( RegisterRequest{ rasterAlias } );

  CHECK( second.assetId == first.assetId );
  CHECK( second.reusedExisting );
  CHECK( third.assetId == first.assetId );
  CHECK( third.reusedExisting );
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

  // A missing source resolves to no structure, so a later recovery relocation is
  // validated against nothing and may adopt the replacement's structure.
  CHECK( std::holds_alternative<std::monostate>( asset->structure() ) );
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

TEST_CASE( "Raster structure surfaces semantic band roles from product metadata",
           "[data_source_providers]" )
{
  // A stacked product raster carries SICNU_BAND_ROLE band metadata; the GDAL
  // provider maps it onto RasterBandStructure::role.
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.path() + QStringLiteral( "/role_stack.tif" );

  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 2, 2, 2, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  for ( int b = 1; b <= 2; ++b )
  {
    GDALRasterBandH band = GDALGetRasterBand( ds, b );
    REQUIRE( band != nullptr );
    float val = 1.0f;
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 1, 1, &val, 1, 1, GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALSetMetadataItem( GDALGetRasterBand( ds, 1 ), "SICNU_BAND_ROLE", "red", nullptr );
  GDALSetMetadataItem( GDALGetRasterBand( ds, 2 ), "SICNU_BAND_ROLE", "nir", nullptr );
  GDALClose( ds );

  const GdalRasterSourceProvider provider;
  const auto resolved = provider.resolve( gdalDescriptor( path ) );
  REQUIRE( resolved );
  const auto *structure = std::get_if<RasterStructure>( &resolved.value().structure );
  REQUIRE( structure != nullptr );
  REQUIRE( structure->bands.size() == 2 );
  CHECK( structure->bands[0].role == sicnu::data::BandRole::Red );
  CHECK( structure->bands[1].role == sicnu::data::BandRole::NIR );
}
