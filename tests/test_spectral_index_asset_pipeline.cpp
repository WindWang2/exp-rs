#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include <cpl_conv.h>
#include <gdal.h>

#include <vector>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/processing_asset_resolver.h"
#include "data/source_descriptor.h"

#include "operators/framework/asset_index_pipeline.h"
#include "processing/framework/output_committer.h"

using namespace sicnu::data;
using sicnu::operators::SpectralIndexParams;
using sicnu::operators::StableOutputSpec;
using sicnu::operators::runSpectralIndexFromAsset;
using sicnu::OutputCommitter;

namespace
{

/// Synthesise a small GeoTIFF per distinct `relative` path and cache them (plus
/// the holding temp dir) for the process lifetime, so tests do not depend on a
/// committed sample raster under data/samples/. Distinct relative paths yield
/// distinct files so the Data Manager does not dedup them by SourceKey. Landsat
/// stand-ins get 7 bands because the spectral-index operator selects NIR/Red by
/// 1-based band number (nir=5, red=4 in these tests).
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
  constexpr int W = 16, H = 16;
  const int bands = relative.contains( QStringLiteral( "landsat" ) ) ? 7 : 1;
  GDALDatasetH ds =
      GDALCreate( driver, path.toUtf8().constData(), W, H, bands, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection(
    ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  std::vector<float> line( W, 1.0f );
  for ( int band = 1; band <= bands; ++band )
  {
    GDALRasterBandH gdalBand = GDALGetRasterBand( ds, band );
    for ( int row = 0; row < H; ++row )
      GDALRasterIO( gdalBand, GF_Write, 0, row, W, 1, line.data(), W, 1,
                    GDT_Float32, 0, 0 );
  }
  GDALClose( ds );
  cache.insert( relative, path );
  return path;
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
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

/// Registers a real (GDAL-resolved) raster asset and returns its id + revision.
struct RegisteredAsset
{
  AssetId id;
  AssetRevision revision;
};

RegisteredAsset registerRasterAsset( DataManager &manager, const QString &fixture )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = fixturePath( fixture );
  RegisterRequest request;
  request.source = source;
  const RegisterResult result = manager.registerSource( request );
  REQUIRE( !result.assetId.isNull() );
  const auto snapshot = manager.asset( result.assetId );
  REQUIRE( snapshot.has_value() );
  return { result.assetId, snapshot->revision() };
}

StableOutputSpec makeOutput( QTemporaryDir &dir, const QString &name, bool autoLoad )
{
  StableOutputSpec output;
  output.tempPath = dir.filePath( QStringLiteral( "scratch.tif" ) );
  output.stablePath = dir.filePath( name );
  output.autoLoad = autoLoad;
  return output;
}

} // namespace

TEST_CASE( "Spectral index runs from a Data Asset and registers a derived output",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );
  QSignalSpy displaySpy( &committer, &OutputCommitter::displayRequested );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 5;
  params.red = 4;

  // Lease count is zero before the run.
  CHECK( manager.leaseCount( input.id ) == 0 );

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, input.revision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/true ),
    resolver, committer );

  REQUIRE( result );
  CHECK( !result.value().isNull() );

  // The Task lease was released when the run completed.
  CHECK( manager.leaseCount( input.id ) == 0 );

  // Output registered as a distinct raster asset.
  CHECK( result.value() != input.id );
  const auto snapshot = manager.asset( result.value() );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->kind() == AssetKind::Raster );

  // Provenance ties the output to the algorithm and its input.
  const auto provenance = manager.provenance( result.value() );
  REQUIRE( provenance.has_value() );
  CHECK( provenance->algorithmId == QStringLiteral( "rs:spectral_index" ) );
  CHECK( provenance->outputAssetId == result.value() );
  REQUIRE( provenance->inputs.size() == 1 );
  CHECK( provenance->inputs.first().assetId == input.id );
  CHECK( provenance->inputs.first().revision == input.revision );

  // Display was requested (autoLoad=true).
  REQUIRE( displaySpy.count() == 1 );
  CHECK( displaySpy.takeFirst().at( 0 ).value<AssetId>() == result.value() );

  // The temp was consumed; the stable output exists.
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "scratch.tif" ) ) ) );
  CHECK( QFile::exists( dir.filePath( QStringLiteral( "ndvi_stable.tif" ) ) ) );
}

TEST_CASE( "Display of a spectral-index output is opt-in",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );
  QSignalSpy displaySpy( &committer, &OutputCommitter::displayRequested );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 5;
  params.red = 4;

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, input.revision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/false ),
    resolver, committer );

  REQUIRE( result );
  CHECK( displaySpy.count() == 0 );
}

TEST_CASE( "A stale input revision is rejected before work starts",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );
  QSignalSpy displaySpy( &committer, &OutputCommitter::displayRequested );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  // Queue against a revision that has already been superseded.
  const AssetRevision staleRevision = AssetRevision::fromValue( input.revision.value() + 1 );

  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 5;
  params.red = 4;

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, staleRevision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/true ),
    resolver, committer );

  REQUIRE_FALSE( result );
  REQUIRE_FALSE( result.diagnostics().isEmpty() );

  // Nothing was registered, nothing displayed, no stable output.
  CHECK( manager.assets().size() == 1 ); // only the input asset
  CHECK( displaySpy.count() == 0 );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "ndvi_stable.tif" ) ) ) );
}

TEST_CASE( "A failed run registers nothing and discards the temporary output",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  // An impossible band number: the operator validates and throws before
  // writing any output.
  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 99;
  params.red = 99;

  // Pre-create a temp output so discardTemporary has something to remove.
  {
    QFile scratch( dir.filePath( QStringLiteral( "scratch.tif" ) ) );
    REQUIRE( scratch.open( QIODevice::WriteOnly ) );
    scratch.write( "placeholder" );
  }

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, input.revision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/false ),
    resolver, committer );

  REQUIRE_FALSE( result );

  // No derived output registered; the input asset is the only one.
  CHECK( manager.assets().size() == 1 );
  // The temporary output was discarded.
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "scratch.tif" ) ) ) );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "ndvi_stable.tif" ) ) ) );
}

TEST_CASE( "Spectral index pipeline masks input NoData to NaN and declares output NoData (#298)",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );

  // Create an input raster with NoData = -9999
  const QString inPath = dir.filePath( QStringLiteral( "nodata_sample.tif" ) );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  constexpr int W = 4, H = 4;
  GDALDatasetH ds = GDALCreate( driver, inPath.toUtf8().constData(), W, H, 5, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  for ( int b = 1; b <= 5; ++b )
  {
    GDALRasterBandH gdalBand = GDALGetRasterBand( ds, b );
    GDALSetRasterNoDataValue( gdalBand, -9999.0 );
    std::vector<float> data( W * H, 100.0f );
    // Set first pixel to NoData
    data[0] = -9999.0f;
    GDALRasterIO( gdalBand, GF_Write, 0, 0, W, H, data.data(), W, H, GDT_Float32, 0, 0 );
  }
  GDALClose( ds );

  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = inPath;
  RegisterRequest request;
  request.source = source;
  const RegisterResult regRes = manager.registerSource( request );
  REQUIRE( !regRes.assetId.isNull() );

  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 4;
  params.red = 3;

  const auto snapshot = manager.asset( regRes.assetId );
  REQUIRE( snapshot.has_value() );

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ regRes.assetId, snapshot->revision() }, params,
    makeOutput( dir, QStringLiteral( "ndvi_nodata_out.tif" ), /*autoLoad=*/false ),
    resolver, committer );

  REQUIRE( result );

  // Check output raster NoData declaration and pixel values
  const QString outPath = dir.filePath( QStringLiteral( "ndvi_nodata_out.tif" ) );
  GDALDatasetH outDs = GDALOpen( outPath.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( outDs != nullptr );
  GDALRasterBandH outBand = GDALGetRasterBand( outDs, 1 );
  REQUIRE( outBand != nullptr );
  int hasNodata = 0;
  double outNodata = GDALGetRasterNoDataValue( outBand, &hasNodata );
  CHECK( hasNodata );
  CHECK( ( std::isnan( outNodata ) || outNodata == -9999.0 ) );

  std::vector<float> outPixels( W * H );
  GDALRasterIO( outBand, GF_Read, 0, 0, W, H, outPixels.data(), W, H, GDT_Float32, 0, 0 );
  CHECK( std::isnan( outPixels[0] ) ); // Pixel with NoData in inputs evaluated to NaN
  CHECK( std::isfinite( outPixels[1] ) ); // Normal pixel evaluated to finite NDVI

  GDALClose( outDs );
}
