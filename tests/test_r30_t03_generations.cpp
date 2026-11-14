// tests/test_r30_t03_generations.cpp — R30-T03 WP02/WP03/WP05.
//
// The generation contract of warp-backed virtual rasters:
//   * a failed final publish never deletes the still-served generation's
//     warp dependencies (the pre-R30 sweep ran inside the layout, BEFORE the
//     sidecar publish — a publish failure left the old VRT referencing
//     deleted files);
//   * a same-size, same-mtime (same-second) input replacement is separated
//     by content-proven identity, not by metadata coincidence;
//   * an unchanged identity reuses the prepared intermediate instead of
//     re-warping, and one asset referenced by several BandRefs warps ONCE.
//
// Oracle independence: pixel expectations are the closed-form source
// gradient; the old/new generation values are distinguished by writing two
// different gradients into the same file shape.
//
// RED discipline: this file only uses APIs present on the R30 base
// (2-argument createVirtualRaster, registerSource, the fault registry), so
// it runs against the unpatched provider to demonstrate the defect.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal.h>
#include <ogr_spatialref.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "data/data_manager.h"
#include "data/raster_grid_compat.h"
#include "data/virtual_raster_recipe.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "runtime/observability/fault_registry.h"

#include <QApplication>
#include <qgsapplication.h>

using namespace sicnu::data;
using sicnu::runtime::observability::fault::ArmedFault;
using sicnu::runtime::observability::fault::FaultAction;
using sicnu::runtime::observability::fault::Mode;

namespace
{

constexpr int kSide = 21;

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_r30_t03_generations";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void )application;
  QgsApplication::initQgis();
}

float sourceValue( int col, int row )
{
  return 1000.f + static_cast<float>( row ) * kSide + static_cast<float>( col );
}

float replacementValue( int col, int row )
{
  return 5000.f + static_cast<float>( row ) * kSide + static_cast<float>( col );
}

using ValueFn = float ( * )( int, int );

/// North-up UTM48N source: origin (500000, 4500000), 10 m pixels, closed-form
/// gradient. The SAME file is rewritten with @p valueFn for the replacement
/// scenario (identical geometry ⇒ identical size).
QString writeUtmSource( const QString &path, ValueFn valueFn )
{
  ensureGdalInit();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH ds =
    GDALCreate( driver, path.toUtf8().constData(), kSide, kSide, 1, GDT_Float32,
                nullptr );
  REQUIRE( ds != nullptr );
  std::array<double, 6> gt = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
  REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
  GDALSetProjection( ds, "EPSG:32648" );
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  std::vector<float> line( kSide );
  for ( int row = 0; row < kSide; ++row )
  {
    for ( int col = 0; col < kSide; ++col )
      line[col] = valueFn( col, row );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, row, kSide, 1, line.data(), kSide,
                           1, GDT_Float32, 0, 0 ) == CE_None );
  }
  GDALClose( ds );
  return path;
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

/// The *.warp* intermediates staged beside @p vrtPath (name → size bytes).
QHash<QString, qint64> warpIntermediates( const QString &vrtPath )
{
  QHash<QString, qint64> files;
  const QFileInfo vrtInfo( vrtPath );
  const QString base = vrtInfo.fileName() + QStringLiteral( ".warp" );
  const QDir dir = vrtInfo.dir();
  for ( const QFileInfo &entry : dir.entryInfoList( QDir::Files ) )
    if ( entry.fileName().startsWith( base ) )
      files.insert( entry.fileName(), entry.size() );
  return files;
}

/// True when @p expected appears within ±@p window pixels of the projected
/// source pixel center (nearest-neighbour tolerance).
bool windowContainsValue( GDALDatasetH ds, int col, int row, float expected,
                          int window )
{
  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  if ( GDALGetGeoTransform( ds, gt.data() ) != CE_None )
    return false;
  const double resX = gt[1];
  const double resY = -gt[5];

  OGRSpatialReference src;
  REQUIRE( src.importFromEPSG( 32648 ) == OGRERR_NONE );
  OGRSpatialReference dst;
  REQUIRE( dst.importFromEPSG( 4326 ) == OGRERR_NONE );
  dst.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER );
  OGRCoordinateTransformation *xf =
    OGRCreateCoordinateTransformation( &src, &dst );
  REQUIRE( xf != nullptr );
  double lx = 500000.0 + ( col + 0.5 ) * 10.0;
  double ly = 4500000.0 - ( row + 0.5 ) * 10.0;
  xf->Transform( 1, &lx, &ly );
  OGRCoordinateTransformation::DestroyCT( xf );

  const int targetCol = static_cast<int>( std::llround( ( lx - gt[0] ) / resX ) );
  const int targetRow = static_cast<int>( std::llround( ( gt[3] - ly ) / resY ) );
  for ( int dr = -window; dr <= window; ++dr )
  {
    for ( int dc = -window; dc <= window; ++dc )
    {
      const int c = targetCol + dc;
      const int r = targetRow + dr;
      if ( c < 0 || r < 0 || c >= GDALGetRasterXSize( ds )
           || r >= GDALGetRasterYSize( ds ) )
        continue;
      float value = std::numeric_limits<float>::quiet_NaN();
      if ( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, c, r, 1, 1, &value,
                         1, 1, GDT_Float32, 0, 0 ) == CE_None
           && value == expected )
        return true;
    }
  }
  return false;
}

/// Band-parameterized variant of the window oracle: maps the source pixel
/// (col,row) center (UTM48N, 10 m grid at the fixture origin) through an
/// independent OGR transform onto the VRT's grid and searches ±window for
/// @p expected in @p band.
bool readBandValue( GDALDatasetH ds, int band, int col, int row,
                    float expected, int window = 1 )
{
  std::array<double, 6> gt = { 0, 0, 0, 0, 0, 0 };
  if ( GDALGetGeoTransform( ds, gt.data() ) != CE_None )
    return false;
  const double resX = gt[1];
  const double resY = -gt[5];

  OGRSpatialReference src;
  REQUIRE( src.importFromEPSG( 32648 ) == OGRERR_NONE );
  OGRSpatialReference dst;
  REQUIRE( dst.importFromEPSG( 4326 ) == OGRERR_NONE );
  dst.SetAxisMappingStrategy( OAMS_TRADITIONAL_GIS_ORDER );
  OGRCoordinateTransformation *xf =
    OGRCreateCoordinateTransformation( &src, &dst );
  REQUIRE( xf != nullptr );
  double lx = 500000.0 + ( col + 0.5 ) * 10.0;
  double ly = 4500000.0 - ( row + 0.5 ) * 10.0;
  xf->Transform( 1, &lx, &ly );
  OGRCoordinateTransformation::DestroyCT( xf );

  const int targetCol = static_cast<int>( std::llround( ( lx - gt[0] ) / resX ) );
  const int targetRow = static_cast<int>( std::llround( ( gt[3] - ly ) / resY ) );
  for ( int dr = -window; dr <= window; ++dr )
  {
    for ( int dc = -window; dc <= window; ++dc )
    {
      const int c = targetCol + dc;
      const int r = targetRow + dr;
      if ( c < 0 || r < 0 || c >= GDALGetRasterXSize( ds )
           || r >= GDALGetRasterYSize( ds ) )
        continue;
      float value = std::numeric_limits<float>::quiet_NaN();
      if ( GDALRasterIO( GDALGetRasterBand( ds, band ), GF_Read, c, r, 1, 1,
                         &value, 1, 1, GDT_Float32, 0, 0 ) == CE_None
           && value == expected )
        return true;
    }
  }
  return false;
}

VirtualRasterRecipe crossCrsRecipe( const AssetId &input )
{
  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ input, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  recipe.noDataPolicy = NoDataPolicy::Preserve;
  return recipe;
}

} // namespace

TEST_CASE( "A failed VRT publish keeps the previous generation's warp "
           "dependencies readable",
           "[r30][t03][generations]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString sourcePath =
    writeUtmSource( dir.filePath( QStringLiteral( "a.tif" ) ), &sourceValue );
  const QDateTime originalMtime = QFileInfo( sourcePath ).lastModified();

  DataManager manager;
  const AssetId a = registerRaster( manager, sourcePath );

  // Generation 1: a successful warp-backed VRT exists and reads true values.
  const Result<AssetId> created =
    manager.createVirtualRaster( crossCrsRecipe( a ) );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;
  const QHash<QString, qint64> generation1 = warpIntermediates( vrtPath );
  REQUIRE( generation1.size() == 1 );
  const QByteArray vrtBytes1 = [&vrtPath]() {
    QFile file( vrtPath );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    return file.readAll();
  }();
  {
    ensureGdalInit();
    GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( vrt != nullptr );
    CHECK( windowContainsValue( vrt, 10, 10, sourceValue( 10, 10 ), 1 ) );
    CHECK( windowContainsValue( vrt, 4, 16, sourceValue( 4, 16 ), 1 ) );
    GDALClose( vrt );
  }

  // Replace the input IN PLACE: same file, same size, same mtime — only the
  // bytes differ (the same-second/same-size replacement scenario).
  QFile::remove( sourcePath );
  writeUtmSource( sourcePath, &replacementValue );
  QFile stamped( sourcePath );
  REQUIRE( stamped.open( QIODevice::ReadWrite ) );
  REQUIRE( stamped.setFileTime( originalMtime, QFile::FileModificationTime ) );
  stamped.close();

  // The final publish fails deterministically (registry fault; the real
  // branches are full-disk/short-write/verify — same code path).
  {
    ArmedFault fault( FaultAction{ "virtual_raster.publish", Mode::NextN, 1, "" } );

    RegisterRequest request;
    request.source = snapshot->source();
    const RegisterResult failed = manager.registerSource( request );
    REQUIRE( failed.assetId.isNull() );
    bool typed = false;
    for ( const Diagnostic &d : failed.diagnostics )
      if ( d.code == QLatin1String( "virtual_raster.write_failed" ) )
        typed = true;
    CHECK( typed );
  }

  // The still-served generation 1 must be COMPLETE: same VRT bytes, every
  // warp dependency still on disk, true old values readable.
  {
    QFile file( vrtPath );
    REQUIRE( file.open( QIODevice::ReadOnly ) );
    CHECK( file.readAll() == vrtBytes1 );
  }
  const QHash<QString, qint64> afterFailure = warpIntermediates( vrtPath );
  for ( auto it = generation1.constBegin(); it != generation1.constEnd(); ++it )
  {
    INFO( "generation-1 intermediate swept before the publish committed: "
          << it.key().toUtf8().constData() );
    CHECK( afterFailure.contains( it.key() ) );
  }
  {
    ensureGdalInit();
    GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( vrt != nullptr );
    CHECK( windowContainsValue( vrt, 10, 10, sourceValue( 10, 10 ), 1 ) );
    GDALClose( vrt );
  }

  // A successful retry commits the new generation (the replacement content)
  // and only then recycles generation 1's intermediate.
  const RegisterResult retried = [&manager, &snapshot]() {
    RegisterRequest request;
    request.source = snapshot->source();
    return manager.registerSource( request );
  }();
  REQUIRE( !retried.assetId.isNull() );
  const QHash<QString, qint64> generation2 = warpIntermediates( vrtPath );
  REQUIRE( generation2.size() == 1 );
  CHECK( generation2.constBegin().key() != generation1.constBegin().key() );
  {
    ensureGdalInit();
    GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( vrt != nullptr );
    CHECK( windowContainsValue( vrt, 10, 10, replacementValue( 10, 10 ), 1 ) );
    CHECK( windowContainsValue( vrt, 4, 16, replacementValue( 4, 16 ), 1 ) );
    GDALClose( vrt );
  }
}

TEST_CASE( "An external input change regenerates dependents or flags them "
           "stale — never Ready on stale facts",
           "[r30][t03][generations]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString sourcePath =
    writeUtmSource( dir.filePath( QStringLiteral( "a.tif" ) ), &sourceValue );

  DataManager manager;
  const AssetId a = registerRaster( manager, sourcePath );
  const Result<AssetId> created =
    manager.createVirtualRaster( crossCrsRecipe( a ) );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;

  auto vrtReadsValue = [&vrtPath]( float expected ) {
    ensureGdalInit();
    GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
    if ( vrt == nullptr )
      return false;
    const bool found = windowContainsValue( vrt, 10, 10, expected, 1 );
    GDALClose( vrt );
    return found;
  };

  REQUIRE( vrtReadsValue( sourceValue( 10, 10 ) ) );

  // External in-place replacement: only the bytes change.
  QFile::remove( sourcePath );
  writeUtmSource( sourcePath, &replacementValue );

  // The drift pass recomputes the dependent FOR REAL (layout+publish) — the
  // artifact now serves the new content, and the dependent is Ready.
  REQUIRE( manager.notifyExternalContentChange( a ) );
  CHECK( vrtReadsValue( replacementValue( 10, 10 ) ) );
  {
    const std::optional<AssetSnapshot> dependent =
      manager.asset( created.value() );
    REQUIRE( dependent.has_value() );
    CHECK( dependent->state() == AssetState::Ready );
  }
  // One live intermediate after the regeneration (the old one was swept
  // post-publish).
  REQUIRE( warpIntermediates( vrtPath ).size() == 1 );

  // The input disappears: the drift pass flags the dependent Error (typed,
  // recipe kept for regeneration) instead of serving the old artifact as
  // current — and the input itself reports Missing (its provider resolution).
  QFile::remove( sourcePath );
  REQUIRE( manager.notifyExternalContentChange( a ) );
  {
    const std::optional<AssetSnapshot> input = manager.asset( a );
    REQUIRE( input.has_value() );
    CHECK( input->state() == AssetState::Missing );
  }
  {
    const std::optional<AssetSnapshot> dependent =
      manager.asset( created.value() );
    REQUIRE( dependent.has_value() );
    CHECK( dependent->state() == AssetState::Error );
  }

  // The input returns: the same pass self-heals both records.
  writeUtmSource( sourcePath, &sourceValue );
  REQUIRE( manager.notifyExternalContentChange( a ) );
  {
    const std::optional<AssetSnapshot> dependent =
      manager.asset( created.value() );
    REQUIRE( dependent.has_value() );
    CHECK( dependent->state() == AssetState::Ready );
  }
  CHECK( vrtReadsValue( sourceValue( 10, 10 ) ) );
}

TEST_CASE( "An unchanged input reuses its prepared intermediate; one asset "
           "with several bands warps once",
           "[r30][t03][generations]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;

  // Two-band source so one recipe can reference the SAME asset twice.
  const QString sourcePath = dir.filePath( QStringLiteral( "two.tif" ) );
  {
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds =
      GDALCreate( driver, sourcePath.toUtf8().constData(), kSide, kSide, 2,
                  GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::array<double, 6> gt = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
    REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
    GDALSetProjection( ds, "EPSG:32648" );
    std::vector<float> line( kSide );
    for ( int row = 0; row < kSide; ++row )
    {
      for ( int col = 0; col < kSide; ++col )
        line[col] = sourceValue( col, row );
      REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, row,
                             kSide, 1, line.data(), kSide, 1, GDT_Float32, 0,
                             0 ) == CE_None );
      for ( int col = 0; col < kSide; ++col )
        line[col] = replacementValue( col, row );
      REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 2 ), GF_Write, 0, row,
                             kSide, 1, line.data(), kSide, 1, GDT_Float32, 0,
                             0 ) == CE_None );
    }
    GDALClose( ds );
  }

  DataManager manager;
  const AssetId a = registerRaster( manager, sourcePath );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ a, 1 }, BandRef{ a, 2 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;
  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;

  // Several BandRefs of one asset ⇒ ONE prepared intermediate on the target
  // grid (the pre-R30 layout keyed the staged name by input INDEX and warped
  // the same file once per reference).
  const QHash<QString, qint64> staged = warpIntermediates( vrtPath );
  INFO( "expected exactly one warp intermediate for one distinct input, got "
        << staged.size() );
  REQUIRE( staged.size() == 1 );
  const QString stagedName = staged.constBegin().key();
  const QFileInfo stagedInfo( QFileInfo( vrtPath ).dir(), stagedName );
  const qint64 stagedMtimeMs =
    stagedInfo.lastModified().toMSecsSinceEpoch();

  // A re-resolve with the input unchanged must NOT re-warp: the same file
  // keeps its mtime (an atomic republication would renew it).
  const RegisterResult reresolved = [&manager, &snapshot]() {
    RegisterRequest request;
    request.source = snapshot->source();
    return manager.registerSource( request );
  }();
  REQUIRE( !reresolved.assetId.isNull() );
  const QHash<QString, qint64> after = warpIntermediates( vrtPath );
  REQUIRE( after.size() == 1 );
  CHECK( after.constBegin().key() == stagedName );
  const QFileInfo afterInfo( QFileInfo( vrtPath ).dir(), stagedName );
  CHECK( afterInfo.lastModified().toMSecsSinceEpoch() == stagedMtimeMs );

  // And the composed VRT still reads BOTH bands' true values.
  {
    ensureGdalInit();
    GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( vrt != nullptr );
    CHECK( windowContainsValue( vrt, 10, 10, sourceValue( 10, 10 ), 1 ) );
    GDALClose( vrt );
  }
}

TEST_CASE( "Chain 1: an open reader keeps the old generation across a failed "
           "publish and the successful retry",
           "[r30][t03][generations][chain]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString sourcePath =
    writeUtmSource( dir.filePath( QStringLiteral( "a.tif" ) ), &sourceValue );

  DataManager manager;
  const AssetId a = registerRaster( manager, sourcePath );
  const Result<AssetId> created =
    manager.createVirtualRaster( crossCrsRecipe( a ) );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;

  // A long-lived reader holds the generation-1 artifact open.
  ensureGdalInit();
  GDALDatasetH heldReader = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( heldReader != nullptr );
  CHECK( windowContainsValue( heldReader, 10, 10, sourceValue( 10, 10 ), 1 ) );

  // Input replaced (same size / same mtime — identity is content-proven).
  const QDateTime originalMtime = QFileInfo( sourcePath ).lastModified();
  QFile::remove( sourcePath );
  writeUtmSource( sourcePath, &replacementValue );
  QFile stamped( sourcePath );
  REQUIRE( stamped.open( QIODevice::ReadWrite ) );
  REQUIRE( stamped.setFileTime( originalMtime, QFile::FileModificationTime ) );
  stamped.close();

  // Final publish fails: the held reader keeps reading the OLD generation.
  {
    ArmedFault fault( FaultAction{ "virtual_raster.publish", Mode::NextN, 1, "" } );
    RegisterRequest request;
    request.source = snapshot->source();
    const RegisterResult failed = manager.registerSource( request );
    REQUIRE( failed.assetId.isNull() );
  }
  CHECK( windowContainsValue( heldReader, 10, 10, sourceValue( 10, 10 ), 1 ) );

  // The retry commits generation 2: the held reader still sees the bytes it
  // opened (POSIX open-fd semantics — its dependencies were only recycled
  // after the publish), a fresh reader sees the new content.
  const RegisterResult retried = [&manager, &snapshot]() {
    RegisterRequest request;
    request.source = snapshot->source();
    return manager.registerSource( request );
  }();
  REQUIRE( !retried.assetId.isNull() );
  CHECK( windowContainsValue( heldReader, 10, 10, sourceValue( 10, 10 ), 1 ) );
  {
    GDALDatasetH fresh = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( fresh != nullptr );
    CHECK( windowContainsValue( fresh, 10, 10, replacementValue( 10, 10 ), 1 ) );
    GDALClose( fresh );
  }
  GDALClose( heldReader );
}

TEST_CASE( "Chain 3: a re-fetched input version never leaks through the "
           "derived VRT on reopen",
           "[r30][t03][generations][chain]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  const QString sourcePath =
    writeUtmSource( dir.filePath( QStringLiteral( "a.tif" ) ), &sourceValue );

  // Session 1: the derived warp VRT over the (locally cached) input.
  DataManager session1;
  const AssetId a = registerRaster( session1, sourcePath );
  const Result<AssetId> created =
    session1.createVirtualRaster( crossCrsRecipe( a ) );
  REQUIRE( created );
  const VirtualRasterRecipe recipe =
    session1.virtualRasterRecipe( created.value() ).value();

  // The remote side re-publishes under a new version: the local cache copy
  // is REPLACED (the deterministic stand-in for a fabric re-fetch behind an
  // ETag change — same bytes-are-newer semantics, no live network here).
  QFile::remove( sourcePath );
  writeUtmSource( sourcePath, &replacementValue );

  // Session 2 (project reopen): the project serializer restores the INPUT
  // under its PERSISTED id first, then the derived asset restores FROM THE
  // RECIPE — which resolves against the refreshed bytes, so it can never
  // serve the superseded version.
  DataManager session2;
  RestoreRequest inputRestore;
  inputRestore.id = a;
  inputRestore.source.providerKey = QStringLiteral( "gdal" );
  inputRestore.source.canonicalSource = sourcePath;
  REQUIRE( session2.restoreSource( inputRestore ) );
  RestoreVirtualRasterRequest restore;
  restore.id = created.value();
  restore.recipe = recipe;
  const Result<AssetId> restored = session2.restoreVirtualRaster( restore );
  REQUIRE( restored );
  {
    const std::optional<AssetSnapshot> snapshot =
      session2.asset( restored.value() );
    REQUIRE( snapshot.has_value() );
    CHECK( snapshot->state() == AssetState::Ready );
    ensureGdalInit();
    GDALDatasetH vrt =
      GDALOpen( snapshot->source().canonicalSource.toUtf8().constData(),
                GA_ReadOnly );
    REQUIRE( vrt != nullptr );
    CHECK( windowContainsValue( vrt, 10, 10, replacementValue( 10, 10 ), 1 ) );
    GDALClose( vrt );
  }

  // Offline reopen (session 3, purely local paths — no network involved)
  // restores the same recipe deterministically.
  DataManager session3;
  RestoreRequest inputRestore3;
  inputRestore3.id = a;
  inputRestore3.source.providerKey = QStringLiteral( "gdal" );
  inputRestore3.source.canonicalSource = sourcePath;
  REQUIRE( session3.restoreSource( inputRestore3 ) );
  const Result<AssetId> restoredAgain = session3.restoreVirtualRaster( restore );
  REQUIRE( restoredAgain );
  {
    const std::optional<AssetSnapshot> snapshot =
      session3.asset( restoredAgain.value() );
    REQUIRE( snapshot.has_value() );
    CHECK( snapshot->state() == AssetState::Ready );
  }
}

TEST_CASE( "Two distinct container-member inputs never share one staged "
           "warp file",
           "[r30][t03][generations]" )
{
  // R1 review P0-1 regression: member spellings fail a plain-file stat, and
  // the pre-fix identity degraded every such input to the SAME "r<rev>-x"
  // token — two members then shared one staged warp file and the second
  // band silently served the first member's pixels.
  ensureQgisApplication();
  QTemporaryDir dir;

  // One georeferenced two-variable netCDF container (UTM48N, 10 m).
  const QString container = dir.filePath( QStringLiteral( "cube.nc" ) );
  {
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "netCDF" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, container.toUtf8().constData(), kSide,
                                  kSide, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::array<double, 6> gt = { 500000.0, 10.0, 0.0, 4500000.0, 0.0, -10.0 };
    REQUIRE( GDALSetGeoTransform( ds, gt.data() ) == CE_None );
    GDALSetProjection( ds, "EPSG:32648" );
    GDALRasterBandH band1 = GDALGetRasterBand( ds, 1 );
    GDALRasterBandH band2 = GDALGetRasterBand( ds, 2 );
    std::vector<float> line1( kSide ), line2( kSide );
    for ( int row = 0; row < kSide; ++row )
    {
      for ( int col = 0; col < kSide; ++col )
      {
        line1[col] = sourceValue( col, row );
        line2[col] = replacementValue( col, row );
      }
      REQUIRE( GDALRasterIO( band1, GF_Write, 0, row, kSide, 1, line1.data(),
                             kSide, 1, GDT_Float32, 0, 0 ) == CE_None );
      REQUIRE( GDALRasterIO( band2, GF_Write, 0, row, kSide, 1, line2.data(),
                             kSide, 1, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
  }

  DataManager manager;
  const QString member1 =
    QStringLiteral( "NETCDF:\"%1\":Band1" ).arg( container );
  const QString member2 =
    QStringLiteral( "NETCDF:\"%1\":Band2" ).arg( container );
  const AssetId m1 = registerRaster( manager, member1 );
  const AssetId m2 = registerRaster( manager, member2 );
  REQUIRE( m1 != m2 );

  VirtualRasterRecipe recipe;
  recipe.inputs = { BandRef{ m1, 1 }, BandRef{ m2, 1 } };
  recipe.targetCrs = QStringLiteral( "EPSG:4326" );
  recipe.targetResolutionX = 0.0001;
  recipe.targetResolutionY = 0.0001;
  recipe.resampling = ResamplingMethod::NearestNeighbour;

  const Result<AssetId> created = manager.createVirtualRaster( recipe );
  REQUIRE( created );
  const std::optional<AssetSnapshot> snapshot =
    manager.asset( created.value() );
  REQUIRE( snapshot.has_value() );
  const QString vrtPath = snapshot->source().canonicalSource;

  // TWO distinct staged intermediates (one per distinct member identity).
  const QHash<QString, qint64> staged = warpIntermediates( vrtPath );
  INFO( "expected two warp intermediates for two distinct members, got "
        << staged.size() );
  REQUIRE( staged.size() == 2 );

  // And each band serves ITS OWN member's content (band 1 = sourceValue
  // gradient, band 2 = replacementValue gradient — the values differ by
  // thousands, so a swapped/wrong source cannot pass).
  {
    ensureGdalInit();
    GDALDatasetH vrt = GDALOpen( vrtPath.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( vrt != nullptr );
    CHECK( readBandValue( vrt, 1, 10, 10, sourceValue( 10, 10 ) ) );
    CHECK( readBandValue( vrt, 2, 10, 10, replacementValue( 10, 10 ) ) );
    CHECK( readBandValue( vrt, 1, 4, 16, sourceValue( 4, 16 ) ) );
    CHECK( readBandValue( vrt, 2, 4, 16, replacementValue( 4, 16 ) ) );
    GDALClose( vrt );
  }
}
