/***************************************************************************
  tests/test_r15_B_stores.cpp
  R15-B round 2 — store integrity oracles:
    * B07 tail: bare-container readers of DatasetStore / ExperimentStore /
      GovernanceStore must not return a silently PARTIAL answer when a real
      on-disk b-tree page is corrupt (SQLITE_CORRUPT mid-scan);
    * #1449: store close() lifecycle — repeated close, close racing live
      readers/writers on other threads (no use-after-free, no crash);
    * B10: lineage graph discloses an unreadable dataset-side feed.
 ***************************************************************************/

#include "data/governance/governance_store.h"
#include "data/workspace_catalog.h"
#include "dataset/dataset_ids.h"
#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"
#include "experiment/lineage.h"
#include "experiment/reproduction_bundle.h"

#include <catch2/catch_test_macros.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using sicnu::dataset::DatasetStore;
using sicnu::experiment::ExperimentStore;
using sicnu::workspace::GovernanceStore;
using sicnu::data::WorkspaceCatalog;

namespace
{

/// Invalidates the b-tree page-type byte of EVERY page holding @p marker
/// (table and index pages alike), so any scan reaching that row fails with
/// SQLITE_CORRUPT part-way. Page 1 (header) is never touched.
int corruptPagesContaining( const QString &dbPath, const QByteArray &marker, int pageSize = 4096 )
{
  QFile file( dbPath );
  if ( !file.open( QIODevice::ReadWrite ) )
    return 0;
  const QByteArray bytes = file.readAll();
  int corrupted = 0;
  qint64 lastPage = -1;
  for ( qsizetype at = bytes.indexOf( marker ); at >= 0; at = bytes.indexOf( marker, at + 1 ) )
  {
    const qint64 pageStart = ( at / pageSize ) * pageSize;
    if ( pageStart == 0 || pageStart == lastPage )
      continue;
    lastPage = pageStart;
    file.seek( pageStart );
    const char garbage = static_cast<char>( 0x7F ); // not a valid b-tree page type
    if ( file.write( &garbage, 1 ) == 1 )
      ++corrupted;
  }
  return corrupted;
}

QString padded( int i )
{
  return QStringLiteral( "R15B-EDGE-%1-END-" ).arg( i, 3, 10, QLatin1Char( '0' ) ) +
         QString( 1500, QLatin1Char( 'p' ) );
}

constexpr int kEdges = 60;
constexpr int kVictim = 40;

} // namespace

// ---------------------------------------------------------------------------
// B07 tail — mid-scan corruption is never a silently partial answer
// ---------------------------------------------------------------------------

TEST_CASE( "B07t: DatasetStore lineage scans refuse a partial answer on SQLITE_CORRUPT",
           "[r15b][b07t][sqlite][dataset]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString dbPath = dir.filePath( QStringLiteral( "ds.db" ) );
  {
    DatasetStore store;
    REQUIRE( store.open( dbPath ) );
    for ( int i = 0; i < kEdges; ++i )
      REQUIRE( store.addLineageEdge( QStringLiteral( "asset" ), QStringLiteral( "a-%1" ).arg( i ),
                                     QStringLiteral( "derived" ), QStringLiteral( "dataset" ),
                                     padded( i ) )
                 .has_value() );
    REQUIRE( store.allLineageEdges().size() == kEdges );
    store.close();
  }
  REQUIRE( corruptPagesContaining( dbPath, QByteArrayLiteral( "R15B-EDGE-040-END" ) ) > 0 );

  DatasetStore reopened;
  REQUIRE( reopened.open( dbPath ) );
  // Legacy signature: complete or empty — never a short list read as whole.
  const auto legacy = reopened.allLineageEdges();
  INFO( "allLineageEdges returned " << legacy.size() << " of " << kEdges );
  CHECK( ( legacy.size() == kEdges || legacy.isEmpty() ) );

#ifndef R15B_LEGACY_API // RED builds against the pre-fix API skip the additive checks
  QString scanError;
  const auto edges = reopened.allLineageEdges( 100000, &scanError );
  CHECK( edges.isEmpty() );            // the injected fault is reached ...
  CHECK_FALSE( scanError.isEmpty() );  // ... and reported, not swallowed
  QString incomingError;
  const auto incoming =
      reopened.incomingEdges( QStringLiteral( "dataset" ), padded( kVictim ), 10, &incomingError );
  CHECK( ( incoming.size() == 1 || !incomingError.isEmpty() ) );
#endif
}

TEST_CASE( "B07t: ExperimentStore edge readers refuse a partial answer on SQLITE_CORRUPT",
           "[r15b][b07t][sqlite][experiment]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString dbPath = dir.filePath( QStringLiteral( "exp.db" ) );
  {
    ExperimentStore store;
    REQUIRE( store.open( dbPath ) );
    for ( int i = 0; i < kEdges; ++i )
      REQUIRE( store.addLineageEdge( QStringLiteral( "matrix" ), QStringLiteral( "cell-1" ),
                                     QStringLiteral( "recorded" ), QStringLiteral( "run" ),
                                     padded( i ) )
                 .has_value() );
    REQUIRE( store.outgoingEdges( QStringLiteral( "matrix" ), QStringLiteral( "cell-1" ), 1000 ).size() ==
             kEdges );
    store.close();
  }
  REQUIRE( corruptPagesContaining( dbPath, QByteArrayLiteral( "R15B-EDGE-040-END" ) ) > 0 );

  ExperimentStore reopened;
  REQUIRE( reopened.open( dbPath ) );
  const auto legacy = reopened.outgoingEdges( QStringLiteral( "matrix" ), QStringLiteral( "cell-1" ), 1000 );
  INFO( "outgoingEdges returned " << legacy.size() << " of " << kEdges );
  CHECK( ( legacy.size() == kEdges || legacy.isEmpty() ) );

#ifndef R15B_LEGACY_API
  QString scanError;
  const auto edges = reopened.outgoingEdges( QStringLiteral( "matrix" ), QStringLiteral( "cell-1" ), 1000,
                                             QString(), QString(), &scanError );
  CHECK( edges.isEmpty() );
  CHECK_FALSE( scanError.isEmpty() );

  const auto page = reopened.lineageEdgePage();
  CHECK( page.edges.size() < kEdges );
  CHECK( page.truncated );
  CHECK( page.scanFailed );
#endif
}

TEST_CASE( "B07t: GovernanceStore list readers raise stepFailed() on SQLITE_CORRUPT",
           "[r15b][b07t][sqlite][governance]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString dbPath = dir.filePath( QStringLiteral( "gov.db" ) );
  {
    GovernanceStore store;
    REQUIRE( store.open( dbPath ) );
    for ( int i = 0; i < kEdges; ++i )
      REQUIRE( store.setTags( QStringLiteral( "asset" ), padded( i ), { QStringLiteral( "t" ) } ).has_value() );
    REQUIRE( store.allTags().size() == kEdges );
    REQUIRE_FALSE( store.stepFailed() );
    store.close();
  }
  REQUIRE( corruptPagesContaining( dbPath, QByteArrayLiteral( "R15B-EDGE-040-END" ) ) > 0 );

  GovernanceStore reopened;
  REQUIRE( reopened.open( dbPath ) );
  const auto tags = reopened.allTags();
  INFO( "allTags returned " << tags.size() << " of " << kEdges );
  // GovernanceStore's established contract (PR #1481 Thread #14): the
  // answer may be a prefix, but then stepFailed() must say so.
  CHECK( ( tags.size() == kEdges || reopened.stepFailed() ) );
  CHECK( tags.size() < kEdges ); // the injected fault is reached
}

TEST_CASE( "B10: lineage graph discloses an unreadable dataset-side edge feed",
           "[r15b][b10][lineage]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString dsPath = dir.filePath( QStringLiteral( "ds.db" ) );
  {
    DatasetStore store;
    REQUIRE( store.open( dsPath ) );
    for ( int i = 0; i < kEdges; ++i )
      REQUIRE( store.addLineageEdge( QStringLiteral( "asset" ), QStringLiteral( "a-%1" ).arg( i ),
                                     QStringLiteral( "derived" ), QStringLiteral( "dataset" ),
                                     padded( i ) )
                 .has_value() );
    store.close();
  }
  DatasetStore healthy;
  REQUIRE( healthy.open( dsPath ) );
  ExperimentStore experiments;
  REQUIRE( experiments.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
  {
    const sicnu::experiment::LineageGraph graph( healthy, experiments );
#ifndef R15B_LEGACY_API
    CHECK_FALSE( graph.datasetSourceIncomplete() );
#endif
  }
  healthy.close();
  REQUIRE( corruptPagesContaining( dsPath, QByteArrayLiteral( "R15B-EDGE-040-END" ) ) > 0 );
  DatasetStore damaged;
  REQUIRE( damaged.open( dsPath ) );
#ifndef R15B_LEGACY_API
  const sicnu::experiment::LineageGraph graph( damaged, experiments );
  CHECK( graph.datasetSourceIncomplete() );
#else
  CHECK( damaged.allLineageEdges().isEmpty() ); // legacy: graph silently built on a prefix
#endif
}

// ---------------------------------------------------------------------------
// #1449 — close() lifecycle
// ---------------------------------------------------------------------------

namespace
{

/// Runs @p readers threads hammering @p work while the main thread closes
/// the store mid-flight; every call must either answer or report the store
/// closed — never touch freed memory (ASan/TSan builds make a violation
/// loud; the plain build at least must not crash).
template <typename Store, typename Work>
void raceCloseAgainst( Store &store, Work work, int readers = 4 )
{
  std::atomic<bool> stop{ false };
  std::atomic<long> calls{ 0 };
  std::vector<std::thread> threads;
  for ( int t = 0; t < readers; ++t )
    threads.emplace_back( [ & ] {
      while ( !stop.load() )
      {
        work( store );
        calls.fetch_add( 1 );
      }
    } );
  while ( calls.load() < 50 )
    std::this_thread::yield();
  store.close();
  store.close(); // repeated close is a no-op
  const long atClose = calls.load();
  while ( calls.load() < atClose + 50 )
    std::this_thread::yield(); // keep calling on the closed store
  stop.store( true );
  for ( auto &thread : threads )
    thread.join();
  CHECK_FALSE( store.isOpen() );
}

} // namespace

TEST_CASE( "#1449: DatasetStore close() racing live readers and writers is safe",
           "[r15b][1449][lifecycle][dataset]" )
{
  for ( int round = 0; round < 20; ++round )
  {
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    DatasetStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "ds.db" ) ) ) );
    std::atomic<int> n{ 0 };
    raceCloseAgainst( store, [ &n ]( DatasetStore &s ) {
      const int i = n.fetch_add( 1 );
      (void)s.addLineageEdge( QStringLiteral( "asset" ), QStringLiteral( "a%1" ).arg( i ),
                              QStringLiteral( "derived" ), QStringLiteral( "dataset" ),
                              QStringLiteral( "d%1" ).arg( i ) );
      (void)s.allLineageEdges( 50 );
      (void)s.datasetCount();
      (void)s.isOpen();
      (void)s.storePath();
    } );
  }
}

TEST_CASE( "#1449: ExperimentStore close() racing live readers and writers is safe",
           "[r15b][1449][lifecycle][experiment]" )
{
  for ( int round = 0; round < 20; ++round )
  {
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    std::atomic<int> n{ 0 };
    raceCloseAgainst( store, [ &n ]( ExperimentStore &s ) {
      const int i = n.fetch_add( 1 );
      (void)s.addLineageEdge( QStringLiteral( "matrix" ), QStringLiteral( "c" ),
                              QStringLiteral( "recorded" ), QStringLiteral( "run" ),
                              QStringLiteral( "r%1" ).arg( i ) );
      (void)s.lineageEdgePage( 50 );
      (void)s.listRuns( QString(), QString(), QString(), 0, 10 );
      (void)s.isOpen();
    } );
  }
}

TEST_CASE( "#1449: GovernanceStore and WorkspaceCatalog close() racing live calls is safe",
           "[r15b][1449][lifecycle][governance][catalog]" )
{
  for ( int round = 0; round < 10; ++round )
  {
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    GovernanceStore gov;
    REQUIRE( gov.open( dir.filePath( QStringLiteral( "gov.db" ) ) ) );
    std::atomic<int> n{ 0 };
    raceCloseAgainst( gov, [ &n ]( GovernanceStore &s ) {
      const int i = n.fetch_add( 1 );
      (void)s.setTags( QStringLiteral( "asset" ), QStringLiteral( "e%1" ).arg( i ), { QStringLiteral( "t" ) } );
      (void)s.allTags();
      (void)s.stepFailed();
      (void)s.storePath();
    } );

    WorkspaceCatalog catalog;
    REQUIRE( catalog.open( dir.filePath( QStringLiteral( "cat.db" ) ) ) );
    raceCloseAgainst( catalog, [ &n ]( WorkspaceCatalog &c ) {
      const int i = n.fetch_add( 1 );
      sicnu::data::CatalogAsset asset;
      asset.assetId = QStringLiteral( "asset-%1" ).arg( i );
      asset.sourceKey = asset.assetId;
      asset.canonicalSource = QStringLiteral( "/data/%1.tif" ).arg( i );
      asset.kind = QStringLiteral( "raster" );
      asset.state = QStringLiteral( "Ready" );
      (void)c.upsertAsset( asset );
      (void)c.page( {}, 0, 10 );
      (void)c.count();
    } );
  }
}

TEST_CASE( "#1449: a store can be reopened after close and stays consistent",
           "[r15b][1449][lifecycle]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "ds.db" ) );
  DatasetStore store;
  for ( int i = 0; i < 5; ++i )
  {
    REQUIRE( store.open( path ) );
    REQUIRE( store.addLineageEdge( QStringLiteral( "asset" ), QStringLiteral( "a%1" ).arg( i ),
                                   QStringLiteral( "derived" ), QStringLiteral( "dataset" ),
                                   QStringLiteral( "d" ) )
               .has_value() );
    CHECK( store.lineageEdgeCount() == i + 1 );
    store.close();
    store.close();
#ifndef R15B_LEGACY_API
    QString err;
    CHECK( store.lineageEdgeCount( &err ) == 0 );
    CHECK_FALSE( err.isEmpty() );
#endif
  }
}
