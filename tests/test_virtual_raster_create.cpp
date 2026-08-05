// test_virtual_raster_create.cpp - VirtualRaster provider + createVirtualRaster (#58)
//
// createVirtualRaster runs the #57 preflight, registers the composition as a
// VirtualRaster asset through the normal pipeline (dedup by recipe), records
// strong dependencies (#56), and stores the recipe. The provider generates a
// VRT in a managed scratch location; the recipe - not the file - is the
// identity.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_spatialref.h>
#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>

#include "app/display/qgis_display_manager.h"
#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/source_descriptor.h"
#include "data/virtual_raster_recipe.h"

using namespace sicnu::data;
using sicnu::display::DisplayViewId;
using sicnu::display::DisplayViewSpec;
using sicnu::display::QgisDisplayManager;

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_virtual_raster_create";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

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
// distinct path). Non-raster samples fall through to the real data tree.
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

VirtualRasterRecipe recipeFor( std::initializer_list<BandRef> inputs )
{
  VirtualRasterRecipe recipe;
  recipe.inputs = QVector<BandRef>( inputs );
  return recipe;
}

/// Writes a 4x4 single-band Byte GeoTIFF with a trivial geotransform and a
/// CRS. Used to prove the VRT band follows the input band's native data type
/// (Byte) rather than being silently coerced to Float32.
QString writeByteGrid( const QString &path, const QString &crs )
{
  CPLPushErrorHandler( CPLQuietErrorHandler );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr );
  REQUIRE( ds != nullptr );
  const std::array<double, 6> gt = { 500000.0, 30.0, 0.0, 4500000.0, 0.0, -30.0 };
  GDALSetGeoTransform( ds, gt.data() );
  if ( !crs.isEmpty() )
  {
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    OSRSetFromUserInput( srs, crs.toUtf8().constData() );
    char *wkt = nullptr;
    OSRExportToWkt( srs, &wkt );
    GDALSetProjection( ds, wkt );
    OSRDestroySpatialReference( srs );
    CPLFree( wkt );
  }
  GByte scanline[4] = { 1, 2, 3, 4 };
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  for ( int y = 0; y < 4; ++y )
  {
    const CPLErr rc = GDALRasterIO( band, GF_Write, 0, y, 4, 1, scanline, 4, 1,
                                    GDT_Byte, 0, 0 );
    REQUIRE( rc == CE_None );
  }
  GDALClose( ds );
  CPLPopErrorHandler();
  return path;
}

} // namespace

TEST_CASE( "createVirtualRaster registers a VirtualRaster asset with the recipe's shape",
           "[virtual_raster][create]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId b = registerRaster( manager, stageDem( dir, QStringLiteral( "b.tif" ) ) );

  const Result<AssetId> created = manager.createVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 }, BandRef{ b, 1 } } ) );

  REQUIRE( created );
  const AssetId virtualId = created.value();

  const std::optional<AssetSnapshot> snapshot = manager.asset( virtualId );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->kind() == AssetKind::VirtualRaster );
  CHECK( snapshot->state() == AssetState::Ready );
  CHECK( snapshot->capabilities().testFlag( AssetCapability::Renderable ) );
  CHECK( snapshot->capabilities().testFlag( AssetCapability::ReadablePixels ) );

  // The structure reads back: band count = inputs, dimensions from the
  // (shared) input grid.
  const auto *raster = std::get_if<RasterStructure>( &snapshot->structure() );
  REQUIRE( raster != nullptr );
  CHECK( raster->bandCount == 3 );
  CHECK( raster->width > 0 );
  CHECK( raster->height > 0 );

  // The recipe is stored and queryable.
  const std::optional<VirtualRasterRecipe> stored =
    manager.virtualRasterRecipe( virtualId );
  REQUIRE( stored.has_value() );
  REQUIRE( stored->inputs.size() == 3 );
}

TEST_CASE( "createVirtualRaster records strong dependencies that block input unload",
           "[virtual_raster][create]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId b = registerRaster( manager, stageDem( dir, QStringLiteral( "b.tif" ) ) );

  const Result<AssetId> created = manager.createVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ) );
  REQUIRE( created );

  // One edge per distinct input.
  const QVector<AssetId> inputs = manager.strongDependenciesOf( created.value() );
  REQUIRE( inputs.size() == 2 );
  CHECK( inputs.contains( a ) );
  CHECK( inputs.contains( b ) );

  // Normal unload of an input is now refused.
  const Result<void> refused = manager.unload( manager.planUnload( a ) );
  REQUIRE_FALSE( refused );
  CHECK( refused.diagnostics().first().code ==
         QStringLiteral( "unload.has_dependents" ) );
}

TEST_CASE( "A hard preflight failure refuses creation and registers nothing",
           "[virtual_raster][create]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const int assetCountBefore = manager.assets().size();

  // Unknown input: UnavailableSource is a hard failure.
  const Result<AssetId> created = manager.createVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ AssetId::generate(), 1 } } ) );

  REQUIRE_FALSE( created );
  REQUIRE_FALSE( created.diagnostics().isEmpty() );
  CHECK( manager.assets().size() == assetCountBefore );
  CHECK( manager.collections().isEmpty() );
}

TEST_CASE( "Same-recipe re-creation dedups to the existing virtual asset",
           "[virtual_raster][create]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId b = registerRaster( manager, stageDem( dir, QStringLiteral( "b.tif" ) ) );
  const VirtualRasterRecipe recipe = recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } );

  const Result<AssetId> first = manager.createVirtualRaster( recipe );
  const Result<AssetId> second = manager.createVirtualRaster( recipe );

  REQUIRE( first );
  REQUIRE( second );
  CHECK( first.value() == second.value() );
  // Only one virtual asset exists.
  int virtualCount = 0;
  for ( const AssetSnapshot &snapshot : manager.assets() )
  {
    if ( snapshot.kind() == AssetKind::VirtualRaster )
      ++virtualCount;
  }
  CHECK( virtualCount == 1 );
  // And only one set of edges.
  CHECK( manager.strongDependenciesOf( first.value() ).size() == 2 );
}

TEST_CASE( "The virtual asset displays through the Display Manager",
           "[virtual_raster][create][display]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId b = registerRaster( manager, stageDem( dir, QStringLiteral( "b.tif" ) ) );
  const Result<AssetId> created = manager.createVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ) );
  REQUIRE( created );

  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager( &manager );
  DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &layerTree;
  spec.layerStore = &layerStore;
  const auto view = displayManager.createView( spec );
  REQUIRE( view );

  // The generated VRT opens as a normal GDAL raster through the display seam.
  const auto layer = displayManager.addLayer( view.value(), created.value() );
  REQUIRE( layer );
  CHECK( displayManager.mapLayer( layer.value() ) != nullptr );
}

TEST_CASE( "Relocating an input keeps the virtual asset resolvable",
           "[virtual_raster][create][relocate]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const QString originalPath = stageDem( dir, QStringLiteral( "original.tif" ) );
  const AssetId a = registerRaster( manager, originalPath );
  const AssetId b = registerRaster( manager, stageDem( dir, QStringLiteral( "b.tif" ) ) );

  const Result<AssetId> created = manager.createVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ) );
  REQUIRE( created );
  const QString vrtPath =
    manager.asset( created.value() )->source().canonicalSource;
  REQUIRE( QFile::exists( vrtPath ) );

  // Move the input to a new path and relocate the asset (same AssetId).
  const QString movedPath = dir.filePath( QStringLiteral( "moved.tif" ) );
  REQUIRE( QFile::rename( originalPath, movedPath ) );
  SourceDescriptor replacement;
  replacement.providerKey = QStringLiteral( "gdal" );
  replacement.canonicalSource = movedPath;
  const Result<RelocateResult> relocated =
    manager.relocate( RelocateRequest{ a, replacement } );
  REQUIRE( relocated );

  // The virtual asset's VRT was regenerated against the new input location:
  // the file references the moved path and still opens.
  QFile vrtFile( vrtPath );
  REQUIRE( vrtFile.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString xml = QString::fromUtf8( vrtFile.readAll() );
  CHECK( xml.contains( movedPath ) );
  CHECK_FALSE( xml.contains( originalPath ) );

  const std::optional<AssetSnapshot> snapshot = manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->state() == AssetState::Ready );
}

TEST_CASE( "A chained virtual raster (virtual of virtual) registers and links",
           "[virtual_raster][create]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageDem( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId b = registerRaster( manager, stageDem( dir, QStringLiteral( "b.tif" ) ) );

  const Result<AssetId> inner = manager.createVirtualRaster(
    recipeFor( { BandRef{ a, 1 }, BandRef{ b, 1 } } ) );
  REQUIRE( inner );

  // A second virtual raster consuming the first (the DAG permits chains).
  const Result<AssetId> outer = manager.createVirtualRaster(
    recipeFor( { BandRef{ inner.value(), 1 }, BandRef{ inner.value(), 2 } } ) );

  REQUIRE( outer );
  CHECK( manager.strongDependenciesOf( outer.value() ) ==
         QVector<AssetId>{ inner.value() } );
  CHECK( manager.strongDependentsOf( inner.value() ) ==
         QVector<AssetId>{ outer.value() } );

  const auto *raster =
    std::get_if<RasterStructure>( &manager.asset( outer.value() )->structure() );
  REQUIRE( raster != nullptr );
  CHECK( raster->bandCount == 2 );
}

TEST_CASE( "The VRT band data type follows the input band's native type",
           "[virtual_raster][create][datatype]" )
{
  // A Byte input must not be silently coerced to Float32: the recipe is
  // identity, and the generated VRT band type matches the referenced input
  // band's native type. A Float32-only hardcoding regresses this assertion.
  ensureQgisApplication();
  QTemporaryDir dir;
  DataManager manager;

  const AssetId byteAsset =
    registerRaster( manager, writeByteGrid( dir.filePath( QStringLiteral( "byte.tif" ) ),
                                            QStringLiteral( "EPSG:32648" ) ) );

  const Result<AssetId> created =
    manager.createVirtualRaster( recipeFor( { BandRef{ byteAsset, 1 } } ) );
  REQUIRE( created );

  const auto *raster =
    std::get_if<RasterStructure>( &manager.asset( created.value() )->structure() );
  REQUIRE( raster != nullptr );
  REQUIRE( raster->bands.size() == 1 );
  CHECK( raster->bands.first().dataType == QStringLiteral( "Byte" ) );
}
