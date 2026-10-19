/***************************************************************************
  tests/test_r15_B_partition_metadata.cpp
  R15-B round 2:
    * B11 — spatial partition leakage regression oracles (negative
      coordinates, block edges, buffer edges, zero quotas, fixed seed);
    * B06 — the metadata fact chain: an external content change must
      re-derive the asset's facts (structure/state), never keep serving the
      previous generation's facts under the new revision, and an unreadable
      new generation must be flagged unusable.
 ***************************************************************************/

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/artifact_store.h"
#include "data/providers/remote_source_cache.h"
#include "dataset/leakage_audit.h"
#include "dataset/split.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>

#include <gdal.h>

#include <cmath>
#include <memory>
#include <variant>

using namespace sicnu::dataset;

namespace
{

SplitInput boxSample( int i, double cx, double cy, double half = 0.25 )
{
  SplitInput input;
  input.sampleId = QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) );
  input.groupId = input.sampleId;
  input.minX = cx - half;
  input.maxX = cx + half;
  input.minY = cy - half;
  input.maxY = cy + half;
  input.validBounds = true;
  return input;
}

/// 20 x 20 lattice centred on the origin (negative and positive coords,
/// centres on both sides of every block edge).
QVector<SplitInput> latticeAroundOrigin( double step = 1.0 )
{
  QVector<SplitInput> inputs;
  int i = 0;
  for ( int y = -10; y < 10; ++y )
    for ( int x = -10; x < 10; ++x )
      inputs.append( boxSample( i++, ( x + 0.5 ) * step, ( y + 0.5 ) * step ) );
  return inputs;
}

QHash<QString, SplitRole> roles( const SplitManifest &manifest )
{
  QHash<QString, SplitRole> out;
  for ( const SplitAssignment &a : manifest.assignments() )
    out.insert( a.sampleId, a.role );
  return out;
}

} // namespace

// ---------------------------------------------------------------------------
// B11 — spatial partition leakage
// ---------------------------------------------------------------------------

TEST_CASE( "B11: spatial_block keeps every floor-grid cell atomic across negative coordinates",
           "[r15b][b11][split][spatial]" )
{
  const QVector<SplitInput> inputs = latticeAroundOrigin();
  SplitConfig config;
  config.method = SplitMethod::SpatialBlock;
  config.seed = 1505;
  config.blockSizeX = 4.0;
  config.blockSizeY = 4.0;
  const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
  REQUIRE( manifest.has_value() );
  const auto byId = roles( manifest.value() );
  // Oracle cell = floor(center / block) — the mathematical grid, so cells
  // straddling 0 (center -0.5 vs +0.5) are DIFFERENT cells; a truncating
  // implementation would merge them into one double-width cell.
  QHash<QString, QSet<int>> rolesPerCell;
  QSet<QString> cells;
  for ( const SplitInput &in : inputs )
  {
    const double cx = ( in.minX + in.maxX ) / 2.0;
    const double cy = ( in.minY + in.maxY ) / 2.0;
    const QString cell = QStringLiteral( "%1|%2" )
                           .arg( qint64( std::floor( cx / 4.0 ) ) )
                           .arg( qint64( std::floor( cy / 4.0 ) ) );
    cells.insert( cell );
    rolesPerCell[cell].insert( int( byId.value( in.sampleId ) ) );
  }
  // Centres span -9.5..9.5, floor(c / 4) spans -3..2: 6 x 6 cells (the
  // edge cells are partial, which is exactly the boundary case of interest).
  CHECK( cells.size() == 36 );
  for ( auto it = rolesPerCell.cbegin(); it != rolesPerCell.cend(); ++it )
  {
    INFO( "cell " << it.key().toStdString() );
    CHECK( it.value().size() == 1 );
  }
  // Every role the ratios ask for is present.
  QSet<int> used;
  for ( const SplitRole r : byId )
    used.insert( int( r ) );
  CHECK( used.contains( int( SplitRole::Train ) ) );
  CHECK( used.contains( int( SplitRole::Test ) ) );
}

TEST_CASE( "B11: spatial_buffer never trains within the buffer of a test sample (edge cases)",
           "[r15b][b11][split][buffer]" )
{
  const QVector<SplitInput> inputs = latticeAroundOrigin();
  for ( const double buffer : { 1.0, 1.5, 2.0 } )
  {
    SplitConfig config;
    config.method = SplitMethod::SpatialBuffer;
    config.seed = 1505;
    config.bufferDistance = buffer;
    config.trainRatio = 0.6;
    config.validationRatio = 0.2;
    config.testRatio = 0.2;
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );
    const auto byId = roles( manifest.value() );
    QVector<const SplitInput *> tests;
    for ( const SplitInput &in : inputs )
      if ( byId.value( in.sampleId ) == SplitRole::Test )
        tests.append( &in );
    REQUIRE_FALSE( tests.isEmpty() );
    int violations = 0;
    for ( const SplitInput &in : inputs )
    {
      const SplitRole role = byId.value( in.sampleId );
      if ( role != SplitRole::Train && role != SplitRole::Validation )
        continue;
      for ( const SplitInput *t : tests )
      {
        const double dx = ( in.minX + in.maxX - t->minX - t->maxX ) / 2.0;
        const double dy = ( in.minY + in.maxY - t->minY - t->maxY ) / 2.0;
        if ( std::sqrt( dx * dx + dy * dy ) < buffer )
          ++violations;
      }
    }
    INFO( "buffer=" << buffer );
    CHECK( violations == 0 );

    // The auditor, run with the engine's own metric, agrees: no cross-split
    // Train/Validation-vs-Test pair under the buffer.
    QVector<AuditSample> audit;
    for ( const SplitInput &in : inputs )
    {
      AuditSample s;
      s.input = in;
      s.role = byId.value( in.sampleId );
      audit.append( s );
    }
    LeakageAuditConfig auditConfig;
    auditConfig.checks = { QStringLiteral( "distance_below_threshold" ) };
    auditConfig.distanceThreshold = buffer;
    const auto report =
      LeakageAuditor::audit( QStringLiteral( "v" ), manifest.value().manifestId(), audit, auditConfig );
    REQUIRE( report.has_value() );
    int trainTestFindings = 0;
    for ( const LeakageFinding &f : report.value().findings() )
    {
      const SplitRole a = byId.value( f.sampleA );
      const SplitRole b = byId.value( f.sampleB );
      const bool involvesTest = a == SplitRole::Test || b == SplitRole::Test;
      const bool involvesUnassigned = a == SplitRole::Unassigned || b == SplitRole::Unassigned;
      if ( involvesTest && !involvesUnassigned )
        ++trainTestFindings;
    }
    CHECK( trainTestFindings == 0 );
  }
}

TEST_CASE( "B11: zero quotas are hard contracts for the spatial engines",
           "[r15b][b11][split][quota]" )
{
  const QVector<SplitInput> inputs = latticeAroundOrigin();
  for ( const SplitMethod method : { SplitMethod::SpatialBlock, SplitMethod::SpatialBuffer } )
  {
    SplitConfig config;
    config.method = method;
    config.seed = 7;
    config.blockSizeX = 4.0;
    config.blockSizeY = 4.0;
    config.bufferDistance = 1.0;
    config.trainRatio = 0.8;
    config.validationRatio = 0.0;
    config.testRatio = 0.2;
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );
    for ( const SplitAssignment &a : manifest.value().assignments() )
      CHECK( a.role != SplitRole::Validation );
  }
}

TEST_CASE( "B11: fixed seed reproduces the spatial split bit-for-bit",
           "[r15b][b11][split][seed]" )
{
  const QVector<SplitInput> inputs = latticeAroundOrigin( 2.5 );
  for ( const SplitMethod method :
        { SplitMethod::SpatialBlock, SplitMethod::SpatialBuffer, SplitMethod::SpatialKFold } )
  {
    SplitConfig config;
    config.method = method;
    config.seed = 1505;
    config.blockSizeX = 10.0;
    config.blockSizeY = 10.0;
    config.bufferDistance = 3.0;
    config.foldCount = 4;
    const auto first = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    const auto second = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    CHECK( first.value().assignments() == second.value().assignments() );
    CHECK( first.value().fingerprint() == second.value().fingerprint() );
    if ( method == SplitMethod::SpatialKFold )
    {
      // Fold atomicity per floor cell, across negative coordinates too.
      QHash<QString, QSet<int>> foldsPerCell;
      QHash<QString, int> foldOf;
      for ( const SplitAssignment &a : first.value().assignments() )
        foldOf.insert( a.sampleId, a.fold );
      for ( const SplitInput &in : inputs )
      {
        const QString cell =
          QStringLiteral( "%1|%2" )
            .arg( qint64( std::floor( ( in.minX + in.maxX ) / 2.0 / 10.0 ) ) )
            .arg( qint64( std::floor( ( in.minY + in.maxY ) / 2.0 / 10.0 ) ) );
        foldsPerCell[cell].insert( foldOf.value( in.sampleId ) );
      }
      for ( const auto &folds : foldsPerCell )
        CHECK( folds.size() == 1 );
    }
  }
}

// ---------------------------------------------------------------------------
// B06 — metadata fact chain after an external content change
// ---------------------------------------------------------------------------

namespace
{

bool writeGeoTiff( const QString &path, int width, int height, int bands )
{
  GDALAllRegister();
  QFile::remove( path );
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, bands, GDT_Byte, nullptr );
  if ( !ds )
    return false;
  double gt[6] = { 500000.0, 10.0, 0.0, 4200000.0, 0.0, -10.0 };
  GDALSetGeoTransform( ds, gt );
  GDALClose( ds );
  return true;
}

int bandCountOf( const sicnu::data::AssetSnapshot &snapshot )
{
  if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &snapshot.structure() ) )
    return raster->bandCount;
  return -1;
}

} // namespace

TEST_CASE( "B06: external content change re-derives metadata facts and flags unusable content",
           "[r15b][b06][data][metadata]" )
{
  int argc = 0;
  std::unique_ptr<QCoreApplication> app;
  if ( !QCoreApplication::instance() )
    app = std::make_unique<QCoreApplication>( argc, nullptr );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "scene.tif" ) );
  REQUIRE( writeGeoTiff( path, 8, 8, 1 ) );

  sicnu::data::DataManager manager;
  sicnu::data::RegisterRequest request;
  request.source.providerKey = QStringLiteral( "gdal" );
  request.source.canonicalSource = path;
  const auto registered = manager.registerSource( request );
  REQUIRE_FALSE( registered.assetId.isNull() );
  const auto before = manager.asset( registered.assetId );
  REQUIRE( before.has_value() );
  REQUIRE( bandCountOf( *before ) == 1 );
  REQUIRE( before->state() == sicnu::data::AssetState::Ready );

  // A new generation of the bytes: different band count and size.
  REQUIRE( writeGeoTiff( path, 16, 12, 3 ) );
  REQUIRE( manager.notifyExternalContentChange( registered.assetId ) );
  const auto after = manager.asset( registered.assetId );
  REQUIRE( after.has_value() );
  CHECK( after->revision() == before->revision().next() );
  INFO( "band count after the content change: " << bandCountOf( *after ) );
  CHECK( bandCountOf( *after ) == 3 ); // RED: the old generation's 1 was kept
  if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &after->structure() ) )
  {
    CHECK( raster->width == 16 );
    CHECK( raster->height == 12 );
  }
  CHECK( after->state() == sicnu::data::AssetState::Ready );

  // A generation that no longer parses must not stay Ready on stale facts.
  {
    QFile garbage( path );
    REQUIRE( garbage.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    garbage.write( QByteArray( 512, '\x5a' ) );
  }
  const auto unreadable = manager.notifyExternalContentChange( registered.assetId );
  CHECK( static_cast<bool>( unreadable ) ); // the change itself is recorded
  const auto broken = manager.asset( registered.assetId );
  REQUIRE( broken.has_value() );
  CHECK( broken->state() == sicnu::data::AssetState::Error ); // RED: stayed Ready

  // Content comes back: the facts and the usable state are restored.
  REQUIRE( writeGeoTiff( path, 8, 8, 2 ) );
  REQUIRE( manager.notifyExternalContentChange( registered.assetId ) );
  const auto healed = manager.asset( registered.assetId );
  REQUIRE( healed.has_value() );
  CHECK( healed->state() == sicnu::data::AssetState::Ready );
  CHECK( bandCountOf( *healed ) == 2 );
}

// ---------------------------------------------------------------------------
// B15: RemoteSourceCache identity. The validator record was keyed on the RAW
// URL, so (a) a presigned URL's signature / credential / expiry landed in the
// artifact store (logical key + storage path), and (b) every signature
// rotation of the SAME object started a fresh, never-validated record —
// isStale() then answered "not stale" forever and a changed object was
// served from cache. Oracle: identity = URL minus signing parameters;
// non-credential query parameters still distinguish resources.
// ---------------------------------------------------------------------------
namespace
{
class FixedValidator : public sicnu::data::RemoteSourceValidator
{
  public:
    QString token;
    QString currentValidatorToken( const QString & ) override { return token; }
};
QByteArray readAll( const QString &path )
{
  QFile f( path );
  return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray();
}
} // namespace

TEST_CASE( "B15: remote source validator identity ignores rotating signatures and never persists them",
           "[r15b][b15][cache]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString db = QDir( tmp.path() ).filePath( QStringLiteral( "artifacts.db" ) );
  {
    sicnu::data::ArtifactStore store;
    REQUIRE( store.open( db ) );
    sicnu::data::RemoteSourceCache cache( store );
    FixedValidator validator;
    cache.setValidator( &validator );

    const QString signedA = QStringLiteral(
      "https://bucket.example.org/scene/b04.tif?X-Amz-Algorithm=AWS4-HMAC-SHA256"
      "&X-Amz-Credential=AKIAR15BSECRETCRED&X-Amz-Date=20261016T000000Z&X-Amz-Expires=900"
      "&X-Amz-SignedHeaders=host&X-Amz-Signature=R15BSIGNATUREAAAA" );
    const QString signedB = QStringLiteral(
      "https://bucket.example.org/scene/b04.tif?X-Amz-Algorithm=AWS4-HMAC-SHA256"
      "&X-Amz-Credential=AKIAR15BSECRETCRED&X-Amz-Date=20261016T010000Z&X-Amz-Expires=900"
      "&X-Amz-SignedHeaders=host&X-Amz-Signature=R15BSIGNATUREBBBB" );
    cache.recordKnownGood( signedA, QStringLiteral( "etag-1" ) );

    // Same object, re-signed URL: the recorded state is found ...
    CHECK( cache.recordedToken( signedB ) == QStringLiteral( "etag-1" ) );
    // ... so a changed object is detected as stale ...
    validator.token = QStringLiteral( "etag-2" );
    CHECK( cache.isStale( signedB ) );
    // ... and an unchanged one is not.
    validator.token = QStringLiteral( "etag-1" );
    CHECK_FALSE( cache.isStale( signedB ) );

    // Non-credential query parameters still name different resources.
    cache.recordKnownGood( QStringLiteral( "https://h.example.org/x.tif?band=1" ), QStringLiteral( "t1" ) );
    CHECK( cache.recordedToken( QStringLiteral( "https://h.example.org/x.tif?band=1" ) ) == QStringLiteral( "t1" ) );
    CHECK( cache.recordedToken( QStringLiteral( "https://h.example.org/x.tif?band=2" ) ).isEmpty() );
    // Plain URLs keep working unchanged.
    cache.recordKnownGood( QStringLiteral( "https://h.example.org/plain.tif" ), QStringLiteral( "p1" ) );
    CHECK( cache.recordedToken( QStringLiteral( "https://h.example.org/plain.tif" ) ) == QStringLiteral( "p1" ) );
  }
  // The signing secret never reaches disk (db + WAL + journal).
  QByteArray bytes = readAll( db ) + readAll( db + QStringLiteral( "-wal" ) ) + readAll( db + QStringLiteral( "-journal" ) );
  CHECK_FALSE( bytes.contains( "R15BSIGNATUREAAAA" ) );
  CHECK_FALSE( bytes.contains( "AKIAR15BSECRETCRED" ) );
}
