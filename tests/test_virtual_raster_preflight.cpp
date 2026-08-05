// test_virtual_raster_preflight.cpp - preflight pure function (#57)
//
// preflightVirtualRaster classifies a recipe against the input assets'
// immutable snapshots - no new GDAL I/O beyond what registerSource already
// resolved. Hard failures (UnavailableSource, MissingCRS, NoOverlap,
// UnsupportedDataType) block creation; warnings (RequiresReprojection,
// RequiresResampling, PartialOverlap) allow it when the recipe carries an
// explicit target.
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <cpl_conv.h>
#include <gdal.h>
#include <gdal_priv.h>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/source_descriptor.h"
#include "data/virtual_raster_preflight.h"
#include "data/virtual_raster_recipe.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::data;

namespace
{

// Synthesise a small GeoTIFF per distinct `relative` path and cache them (plus
// the holding temp dir) for the process lifetime, so tests do not depend on a
// committed sample raster under data/samples/. Distinct relative paths yield
// distinct files so the Data Manager does not dedup them by SourceKey.
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
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
  REQUIRE( ds != nullptr );
  double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
  GDALSetGeoTransform( ds, gt );
  GDALSetProjection(
    ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( W, 1.0f );
  for ( int row = 0; row < H; ++row )
    GDALRasterIO( band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0 );
  GDALClose( ds );
  cache.insert( relative, path );
  return path;
}

// Resolve a fixture path. Sample rasters under data/samples/ are no longer
// committed; raster paths are redirected to a synthesised GeoTIFF (one per
// distinct path). Non-raster samples (e.g. samples/test.shp) fall through to
// the real data tree so the OGR provider sees a genuine missing source once the
// shapefile is gone.
QString fixturePath( const QString &relative )
{
  if ( ( relative.startsWith( QLatin1String( "samples/" ) ) &&
         ( relative.endsWith( QLatin1String( ".tif" ) ) ||
           relative.endsWith( QLatin1String( ".tiff" ) ) ) ) ||
       relative == QLatin1String( "phr_xs.tif" ) )
  {
    return syntheticSample( relative );
  }
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

AssetId registerRaster( DataManager &manager, const QString &path )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = path;
  RegisterRequest request;
  request.source = source;
  const RegisterResult result = manager.registerSource( request );
  REQUIRE( !result.assetId.isNull() );
  return result.assetId;
}

QString stageDem( QTemporaryDir &dir, const QString &name )
{
  const QString path = dir.filePath( name );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ),
                         path ) );
  return path;
}

/// Writes a 4x4 single-band GeoTIFF with the given geotransform and CRS (empty
/// CRS = no projection), mirroring test_satellite_products' writeTinyBand.
QString writeGrid( const QString &path,
                   double originX, double originY,
                   double pixelSize,
                   const QString &crs )
{
  ensureGdalInit();
  std::array<double, 6> gt = { originX, pixelSize, 0, originY, 0, -pixelSize };
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4 * 4, 1.0f ) );
  QString err;
  REQUIRE( writeGdalOutput( path, 4, 4, bands, gt, crs, &err ) );
  return path;
}

/// Writes a paletted (categorical) single-band GeoTIFF.
QString writePaletted( const QString &path )
{
  const QString written = writeGrid( path, 500000, 4500000, 30,
                                     QStringLiteral( "EPSG:32648" ) );
  GDALDatasetH ds = GDALOpen( written.toUtf8().constData(), GA_Update );
  REQUIRE( ds != nullptr );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  REQUIRE( band != nullptr );
  REQUIRE( GDALSetRasterColorInterpretation( band, GCI_PaletteIndex ) == CE_None );
  GDALClose( ds );
  return written;
}

/// Writes a GeoTIFF then overwrites its geotransform (for rotated grids).
QString writeRotated( const QString &path )
{
  const QString written = writeGrid( path, 500000, 4500000, 30,
                                     QStringLiteral( "EPSG:32648" ) );
  GDALDatasetH ds = GDALOpen( written.toUtf8().constData(), GA_Update );
  REQUIRE( ds != nullptr );
  // 30-unit pixels with a shear term.
  double gt[6] = { 500000, 30, 5, 4500000, 5, -30 };
  REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
  GDALClose( ds );
  return written;
}

/// Writes a file with a raster extension but non-raster content: the provider
/// resolves it to Error state.
QString writeCorrupt( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  file.write( "this is not a raster" );
  file.close();
  return path;
}

VirtualRasterRecipe recipeFor( std::initializer_list<BandRef> inputs )
{
  VirtualRasterRecipe recipe;
  recipe.inputs = QVector<BandRef>( inputs );
  return recipe;
}

} // namespace

TEST_CASE( "Same-grid same-CRS inputs are Compatible",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId b = registerRaster( manager, stageDem( dir, QStringLiteral( "b.tif" ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::Compatible );
  CHECK( result.canCreate );
}

TEST_CASE( "An unknown input AssetId is UnavailableSource and blocks creation",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ AssetId::generate(), 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::UnavailableSource );
  CHECK_FALSE( result.canCreate );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
}

TEST_CASE( "An input in Missing state is UnavailableSource",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // Register a path that does not exist: the provider resolves it as Missing.
  const AssetId missing = registerRaster(
    manager, dir.filePath( QStringLiteral( "does-not-exist.tif" ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ missing, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::UnavailableSource );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "A band number beyond the input's band count is UnavailableSource",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ a, 99 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::UnavailableSource );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "An input without a CRS and no explicit target is MissingCRS",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId noCrs = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "nocrs.tif" ) ),
                        500000, 4500000, 30, QString() ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ noCrs, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::MissingCRS );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "An input without a CRS is creatable when the recipe pins a target CRS",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId noCrs = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "nocrs.tif" ) ),
                        500000, 4500000, 30, QString() ) );

  VirtualRasterRecipe recipe = recipeFor( { BandRef{ noCrs, 1 } } );
  recipe.targetCrs = QStringLiteral( "EPSG:32648" );

  const PreflightResult result = preflightVirtualRaster( recipe, manager );

  // The input is treated as being in the target CRS; with matching grid and
  // full overlap this is Compatible.
  CHECK( result.verdict == PreflightVerdict::Compatible );
  CHECK( result.canCreate );
}

TEST_CASE( "Disjoint extents are NoOverlap and block creation",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "a.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  const AssetId b = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "b.tif" ) ),
                        900000, 4600000, 30, QStringLiteral( "EPSG:32648" ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::NoOverlap );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "Partially overlapping extents are a warning that allows creation",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "a.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  // Shifted by half the raster width: non-empty intersection, smaller than a.
  const AssetId b = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "b.tif" ) ),
                        500060, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::PartialOverlap );
  CHECK( result.canCreate );
}

TEST_CASE( "Differing CRSs require reprojection, creatable only with a target CRS",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "a.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  const AssetId b = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "b.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32633" ) ) );

  SECTION( "no target CRS: cannot create" )
  {
    const PreflightResult result = preflightVirtualRaster(
      recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ), manager );
    CHECK( result.verdict == PreflightVerdict::RequiresReprojection );
    CHECK_FALSE( result.canCreate );
  }
  SECTION( "explicit target CRS: can create" )
  {
    VirtualRasterRecipe recipe = recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } );
    recipe.targetCrs = QStringLiteral( "EPSG:32648" );
    const PreflightResult result = preflightVirtualRaster( recipe, manager );
    CHECK( result.verdict == PreflightVerdict::RequiresReprojection );
    CHECK( result.canCreate );
  }
}

TEST_CASE( "Differing grids require resampling, creatable only with a target grid",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "a.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  const AssetId b = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "b.tif" ) ),
                        500000, 4500000, 10, QStringLiteral( "EPSG:32648" ) ) );

  SECTION( "no target resolution: cannot create" )
  {
    const PreflightResult result = preflightVirtualRaster(
      recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ), manager );
    CHECK( result.verdict == PreflightVerdict::RequiresResampling );
    CHECK_FALSE( result.canCreate );
  }
  SECTION( "explicit target resolution: can create" )
  {
    VirtualRasterRecipe recipe = recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } );
    recipe.targetResolutionX = 30.0;
    recipe.targetResolutionY = 30.0;
    const PreflightResult result = preflightVirtualRaster( recipe, manager );
    CHECK( result.verdict == PreflightVerdict::RequiresResampling );
    CHECK( result.canCreate );
  }
}

TEST_CASE( "A categorical input with continuous resampling is UnsupportedDataType",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId categorical = registerRaster(
    manager, writePaletted( dir.filePath( QStringLiteral( "cat.tif" ) ) ) );

  VirtualRasterRecipe recipe = recipeFor( { BandRef{ categorical, 1 } } );
  recipe.resampling = ResamplingMethod::Bilinear;

  const PreflightResult result = preflightVirtualRaster( recipe, manager );

  CHECK( result.verdict == PreflightVerdict::UnsupportedDataType );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "A categorical input with nearest-neighbour resampling is fine",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId categorical = registerRaster(
    manager, writePaletted( dir.filePath( QStringLiteral( "cat.tif" ) ) ) );

  VirtualRasterRecipe recipe = recipeFor( { BandRef{ categorical, 1 } } );
  recipe.resampling = ResamplingMethod::NearestNeighbour;

  const PreflightResult result = preflightVirtualRaster( recipe, manager );

  CHECK( result.verdict == PreflightVerdict::Compatible );
  CHECK( result.canCreate );
}

TEST_CASE( "An empty recipe is refused instead of crashing",
           "[virtual_raster_preflight]" )
{
  DataManager manager;

  const PreflightResult result =
    preflightVirtualRaster( VirtualRasterRecipe(), manager );

  CHECK( result.verdict == PreflightVerdict::UnavailableSource );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "A union extent policy tolerates disjoint inputs",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "a.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  const AssetId b = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "b.tif" ) ),
                        900000, 4600000, 30, QStringLiteral( "EPSG:32648" ) ) );

  // The same disjoint inputs that NoOverlap rejects under the default
  // Intersection policy are creatable under an explicit Union (uncovered
  // pixels are NoData-filled).
  VirtualRasterRecipe recipe = recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } );
  recipe.extentPolicy = ExtentPolicy::Union;

  const PreflightResult result = preflightVirtualRaster( recipe, manager );

  CHECK( result.verdict == PreflightVerdict::Compatible );
  CHECK( result.canCreate );
}

TEST_CASE( "Extents in differing CRSs are not compared raw",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // Numerically disjoint extents in two different CRSs: raw comparison would
  // call this NoOverlap, but the extents are not comparable until reprojected -
  // the creatable RequiresReprojection verdict must win instead.
  const AssetId a = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "a.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  const AssetId b = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "b.tif" ) ),
                        900000, 4600000, 30, QStringLiteral( "EPSG:32633" ) ) );

  VirtualRasterRecipe recipe = recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } );
  recipe.targetCrs = QStringLiteral( "EPSG:32648" );

  const PreflightResult result = preflightVirtualRaster( recipe, manager );

  CHECK( result.verdict == PreflightVerdict::RequiresReprojection );
  CHECK( result.canCreate );
}

TEST_CASE( "A rotated input grid requires resampling",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId flat = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "flat.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  const AssetId rotated = registerRaster(
    manager, writeRotated( dir.filePath( QStringLiteral( "rot.tif" ) ) ) );

  VirtualRasterRecipe recipe = recipeFor( { BandRef{ flat, 1 }, BandRef{ rotated, 1 } } );
  recipe.targetResolutionX = 30.0;
  recipe.targetResolutionY = 30.0;

  const PreflightResult result = preflightVirtualRaster( recipe, manager );

  CHECK( result.verdict == PreflightVerdict::RequiresResampling );
  CHECK( result.canCreate );
}

TEST_CASE( "A shared-edge (zero-area) intersection counts as NoOverlap",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "a.tif" ) ),
                        500000, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );
  // Origin exactly at a's east edge (4 px * 30 = 120 units): touches but
  // shares no area.
  const AssetId b = registerRaster(
    manager, writeGrid( dir.filePath( QStringLiteral( "b.tif" ) ),
                        500120, 4500000, 30, QStringLiteral( "EPSG:32648" ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::NoOverlap );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "A vector input is UnavailableSource",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // samples/test.shp is no longer committed to VCS; fixturePath falls through
  // for non-raster paths, so the OGR source resolves as Missing (and would be a
  // Ready vector if the file were present). Either way the preflight must
  // classify it UnavailableSource.
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "ogr" );
  source.canonicalSource = fixturePath( QStringLiteral( "samples/test.shp" ) );
  RegisterRequest request;
  request.source = source;
  const AssetId vector = manager.registerSource( request ).assetId;
  REQUIRE( !vector.isNull() );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ vector, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::UnavailableSource );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "An input in Error state is UnavailableSource",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId corrupt = registerRaster(
    manager, writeCorrupt( dir.filePath( QStringLiteral( "corrupt.tif" ) ) ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ corrupt, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::UnavailableSource );
  CHECK_FALSE( result.canCreate );
}

TEST_CASE( "An unloaded input is UnavailableSource",
           "[virtual_raster_preflight]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const UnloadPlan plan = manager.planUnload( a ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );

  const PreflightResult result = preflightVirtualRaster(
    recipeFor( { BandRef{ a, 1 } } ), manager );

  CHECK( result.verdict == PreflightVerdict::UnavailableSource );
  CHECK_FALSE( result.canCreate );
}
