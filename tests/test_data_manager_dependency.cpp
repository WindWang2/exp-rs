// test_data_manager_dependency.cpp - strong-dependency DAG (#56)
//
// Strong dependencies form a DAG: edges from a dependent asset to its inputs.
// Cycle creation is rejected; normal unload of a depended-on asset is refused
// (dependents named, mirroring the lease-refusal shape); cascade unload
// removes dependents transitively, deepest-first; edges are pruned when either
// endpoint leaves the catalog.
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/source_descriptor.h"

using namespace sicnu::data;

namespace
{

QString fixturePath( const QString &relative )
{
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

QString stageRaster( QTemporaryDir &dir, const QString &name )
{
  const QString path = dir.filePath( name );
  REQUIRE( QFile::copy( fixturePath( QStringLiteral( "samples/dem_sample.tif" ) ),
                         path ) );
  return path;
}

} // namespace

TEST_CASE( "Strong dependency edges are recorded and queryable",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId inputA = registerRaster( manager, stageRaster( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId inputB = registerRaster( manager, stageRaster( dir, QStringLiteral( "b.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "c.tif" ) ) );

  REQUIRE( manager.addStrongDependency( dependent, inputA ) );
  REQUIRE( manager.addStrongDependency( dependent, inputB ) );

  // Inputs of the dependent, in insertion order.
  const QVector<AssetId> inputs = manager.strongDependenciesOf( dependent );
  REQUIRE( inputs.size() == 2 );
  CHECK( inputs.contains( inputA ) );
  CHECK( inputs.contains( inputB ) );

  // Consumers of each input.
  CHECK( manager.strongDependentsOf( inputA ) == QVector<AssetId>{ dependent } );
  CHECK( manager.strongDependentsOf( inputB ) == QVector<AssetId>{ dependent } );
  CHECK( manager.strongDependentsOf( dependent ).isEmpty() );
}

TEST_CASE( "A duplicate edge is a successful no-op",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input = registerRaster( manager, stageRaster( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "b.tif" ) ) );

  REQUIRE( manager.addStrongDependency( dependent, input ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );
  CHECK( manager.strongDependenciesOf( dependent ).size() == 1 );
}

TEST_CASE( "Direct and transitive cycles are rejected without mutation",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageRaster( dir, QStringLiteral( "a.tif" ) ) );
  const AssetId b = registerRaster( manager, stageRaster( dir, QStringLiteral( "b.tif" ) ) );
  const AssetId c = registerRaster( manager, stageRaster( dir, QStringLiteral( "c.tif" ) ) );

  // Direct cycle: A -> B then B -> A.
  REQUIRE( manager.addStrongDependency( a, b ) );
  const Result<void> direct = manager.addStrongDependency( b, a );
  REQUIRE_FALSE( direct );
  CHECK( direct.diagnostics().first().code ==
         QStringLiteral( "dependency.cycle" ) );
  // The failed edge was not recorded.
  CHECK( manager.strongDependenciesOf( b ).isEmpty() );

  // Transitive cycle: A -> B, B -> C, then C -> A.
  REQUIRE( manager.addStrongDependency( b, c ) );
  const Result<void> transitive = manager.addStrongDependency( c, a );
  REQUIRE_FALSE( transitive );
  CHECK( transitive.diagnostics().first().code ==
         QStringLiteral( "dependency.cycle" ) );
  CHECK( manager.strongDependenciesOf( c ).isEmpty() );

  // The legal chain is intact: A depends on B, B depends on C.
  CHECK( manager.strongDependenciesOf( a ) == QVector<AssetId>{ b } );
  CHECK( manager.strongDependenciesOf( b ) == QVector<AssetId>{ c } );
}

TEST_CASE( "Self-dependency is rejected as a cycle",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageRaster( dir, QStringLiteral( "a.tif" ) ) );

  const Result<void> result = manager.addStrongDependency( a, a );
  REQUIRE_FALSE( result );
  CHECK( result.diagnostics().first().code ==
         QStringLiteral( "dependency.cycle" ) );
}

TEST_CASE( "Edges to unknown assets are refused",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId a = registerRaster( manager, stageRaster( dir, QStringLiteral( "a.tif" ) ) );

  CHECK_FALSE( manager.addStrongDependency( a, AssetId::generate() ) );
  CHECK_FALSE( manager.addStrongDependency( AssetId::generate(), a ) );
  CHECK( manager.strongDependenciesOf( a ).isEmpty() );
}

TEST_CASE( "Normal unload of a depended-on asset is refused and names the dependent",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input = registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );

  // planUnload reports the dependent in its impact.
  const UnloadPlan plan = manager.planUnload( input );
  CHECK( plan.strongDependents() == QVector<AssetId>{ dependent } );

  // Non-cascade unload refuses, naming the dependent.
  const Result<void> refused = manager.unload( plan );
  REQUIRE_FALSE( refused );
  CHECK( refused.diagnostics().first().code ==
         QStringLiteral( "unload.has_dependents" ) );
  // Nothing was removed.
  CHECK( manager.asset( input ).has_value() );
  CHECK( manager.asset( dependent ).has_value() );
}

TEST_CASE( "Cascade unload removes the dependent transitively, deepest-first",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;
  QSignalSpy aboutToUnloadSpy( &manager, &DataManager::assetAboutToUnload );
  QSignalSpy removedSpy( &manager, &DataManager::assetRemoved );

  // Chain: leaf <- mid <- top (top depends on mid, mid depends on leaf).
  const AssetId leaf = registerRaster( manager, stageRaster( dir, QStringLiteral( "leaf.tif" ) ) );
  const AssetId mid = registerRaster( manager, stageRaster( dir, QStringLiteral( "mid.tif" ) ) );
  const AssetId top = registerRaster( manager, stageRaster( dir, QStringLiteral( "top.tif" ) ) );
  REQUIRE( manager.addStrongDependency( mid, leaf ) );
  REQUIRE( manager.addStrongDependency( top, mid ) );

  const UnloadPlan plan = manager.planUnload( leaf ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );

  // All three removed, deepest dependent first: top, mid, then leaf.
  CHECK_FALSE( manager.asset( leaf ).has_value() );
  CHECK_FALSE( manager.asset( mid ).has_value() );
  CHECK_FALSE( manager.asset( top ).has_value() );

  REQUIRE( removedSpy.count() == 3 );
  CHECK( removedSpy.at( 0 ).first().value<AssetId>() == top );
  CHECK( removedSpy.at( 1 ).first().value<AssetId>() == mid );
  CHECK( removedSpy.at( 2 ).first().value<AssetId>() == leaf );
  CHECK( aboutToUnloadSpy.count() == 3 );
}

TEST_CASE( "Reaping a temporary asset with dependents is refused",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // The depended-on input is SessionTemporary (reapable in isolation).
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = stageRaster( dir, QStringLiteral( "in.tif" ) );
  RegisterRequest request;
  request.source = source;
  request.persistence = PersistencePolicy::SessionTemporary;
  const AssetId input = manager.registerSource( request ).assetId;
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );

  const ReapResult result = manager.reap( ReapRequest{ input } );
  CHECK_FALSE( result.unloaded );
  REQUIRE_FALSE( result.diagnostics.isEmpty() );
  CHECK( result.diagnostics.first().code ==
         QStringLiteral( "reap.has_dependents" ) );
  CHECK( manager.asset( input ).has_value() );
}

TEST_CASE( "Removing a dependent prunes its edges from the input's consumer list",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input = registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );
  REQUIRE( manager.strongDependentsOf( input ).size() == 1 );

  // Unload the dependent itself (no one depends on it).
  const UnloadPlan plan = manager.planUnload( dependent ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );

  // The input's consumer list no longer references the removed dependent.
  CHECK( manager.strongDependentsOf( input ).isEmpty() );
  // And the input can now be unloaded normally.
  CHECK( manager.planUnload( input ).strongDependents().isEmpty() );
}

TEST_CASE( "Unloading a collection cascade prunes dependency edges of its children",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input = registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );

  // Put the dependent into a collection, then cascade-unload the collection.
  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, dependent ) );

  REQUIRE( manager.unloadCollection( collectionId, /*cascade=*/true ) );

  // The edge is pruned: the input no longer reports a consumer.
  CHECK( manager.strongDependentsOf( input ).isEmpty() );
}

TEST_CASE( "Adding an edge stales a previously captured unload plan",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input = registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );

  // The plan is captured BEFORE the dependency exists: its impact says
  // "no dependents".
  const UnloadPlan stalePlan = manager.planUnload( input );
  REQUIRE( stalePlan.strongDependents().isEmpty() );

  REQUIRE( manager.addStrongDependency( dependent, input ) );

  // Using the stale plan fails rather than silently cascading away a dependent
  // the user never saw in the confirmation impact.
  const Result<void> result = manager.unload( stalePlan.confirmedCascade() );
  REQUIRE_FALSE( result );
  CHECK( result.diagnostics().first().code ==
         QStringLiteral( "unload.stale_plan" ) );
  CHECK( manager.asset( input ).has_value() );
  CHECK( manager.asset( dependent ).has_value() );
}

TEST_CASE( "Cascade unload revokes leases held by dependents",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input = registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );

  // The dependent holds an active View lease (e.g. a display layer).
  auto lease = manager
                 .acquire( AssetRef{ dependent, AssetRevision::initial() },
                           AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  // Cascade removal of the input removes the dependent and revokes its lease
  // rather than refusing (the user confirmed the cascade).
  const UnloadPlan plan = manager.planUnload( input ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );

  CHECK_FALSE( manager.asset( dependent ).has_value() );
  CHECK_FALSE( manager.asset( input ).has_value() );
  // The lease was revoked, not silently leaked.
  CHECK( lease.release() == LeaseOutcome::Invalid );
}

TEST_CASE( "An asset with both a lease and a dependent reports the lease refusal first",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input = registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );

  auto lease = manager
                 .acquire( AssetRef{ input, AssetRevision::initial() },
                           AssetUse{ LeaseKind::View, QStringLiteral( "viewer" ) } )
                 .take();
  REQUIRE( lease.isValid() );

  // Lease refusal takes precedence (it is the pre-existing, more specific
  // signal); both refusals leave the catalog untouched.
  const Result<void> refused = manager.unload( manager.planUnload( input ) );
  REQUIRE_FALSE( refused );
  CHECK( refused.diagnostics().first().code ==
         QStringLiteral( "unload.leased" ) );
  CHECK( manager.asset( input ).has_value() );

  ( void ) lease.release();
}

TEST_CASE( "A diamond dependency is cascade-removed exactly once per node",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;
  QSignalSpy removedSpy( &manager, &DataManager::assetRemoved );

  // Diamond: base <- left, base <- right, left <- top, right <- top.
  const AssetId base = registerRaster( manager, stageRaster( dir, QStringLiteral( "base.tif" ) ) );
  const AssetId left = registerRaster( manager, stageRaster( dir, QStringLiteral( "left.tif" ) ) );
  const AssetId right = registerRaster( manager, stageRaster( dir, QStringLiteral( "right.tif" ) ) );
  const AssetId top = registerRaster( manager, stageRaster( dir, QStringLiteral( "top.tif" ) ) );
  REQUIRE( manager.addStrongDependency( left, base ) );
  REQUIRE( manager.addStrongDependency( right, base ) );
  REQUIRE( manager.addStrongDependency( top, left ) );
  REQUIRE( manager.addStrongDependency( top, right ) );

  const UnloadPlan plan = manager.planUnload( base ).confirmedCascade();
  REQUIRE( manager.unload( plan ) );

  // All four removed; `top` (reachable through both left and right) exactly
  // once, and before its inputs.
  REQUIRE( removedSpy.count() == 4 );
  CHECK( removedSpy.at( 0 ).first().value<AssetId>() == top );
  CHECK( manager.asset( base ) == std::nullopt );
  CHECK( manager.asset( left ) == std::nullopt );
  CHECK( manager.asset( right ) == std::nullopt );
  CHECK( manager.asset( top ) == std::nullopt );
}

TEST_CASE( "A session sweep skips a temporary asset that has a dependent",
           "[data_manager][dependency]" )
{
  QTemporaryDir dir;
  DataManager manager;

  // Session-temporary input with a dependent: the sweep must NOT collect it as
  // idle and then misreport the dependent-refusal as a lease skip.
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = stageRaster( dir, QStringLiteral( "in.tif" ) );
  RegisterRequest request;
  request.source = source;
  request.persistence = PersistencePolicy::SessionTemporary;
  const AssetId input = manager.registerSource( request ).assetId;
  const AssetId dependent = registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );

  const TemporaryReapResult result = manager.reapSessionTemporaries();

  // Not reaped; reported as skipped (not idle), with no spurious diagnostics
  // from a doomed reap attempt.
  CHECK( result.reapedCount == 0 );
  CHECK( result.skippedLeased == QVector<AssetId>{ input } );
  CHECK( result.diagnostics.isEmpty() );
  CHECK( manager.asset( input ).has_value() );
}

TEST_CASE(
  "Cascade-unloading a collection refuses while a child is an external input",
  "[data_manager][dependency][collection]" )
{
  // AC1 (#60): a collection whose child is a strong-dependency INPUT consumed
  // by a dependent living OUTSIDE the collection cannot be cascade-unloaded
  // silently. Doing so would erase the child and prune the edge, leaving the
  // external dependent (e.g. a Virtual Raster) registered but broken. The
  // refusal names the dependent and leaves both the child and the dependent
  // registered, mirroring the lease-safety refusal shape.
  QTemporaryDir dir;
  DataManager manager;

  const AssetId input =
    registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent =
    registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, input ) );

  // The INPUT lives in the collection; the dependent is external to it.
  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, input ) );

  const Result<void> result = manager.unloadCollection( collectionId, /*cascade=*/true );

  REQUIRE_FALSE( result );
  // The refusal names the external dependent that would be orphaned.
  CHECK( result.diagnostics().first().code ==
         QStringLiteral( "collection.has_external_dependents" ) );
  CHECK( result.diagnostics().first().message.contains( dependent.toString() ) );

  // Nothing was removed: the collection, the child input, and the dependent
  // all remain registered and the edge is intact.
  REQUIRE( manager.collection( collectionId ).has_value() );
  CHECK( manager.asset( input ).has_value() );
  CHECK( manager.asset( dependent ).has_value() );
  CHECK( manager.strongDependentsOf( input ) == QVector<AssetId>{ dependent } );
}

TEST_CASE(
  "Cascade-unloading a collection whose child is the dependent still unloads cleanly",
  "[data_manager][dependency][collection]" )
{
  // AC2 (#60): the other sub-case is safe. A collection child that IS a
  // dependent with edges to external inputs cascade-unloads without refusal:
  // removing the dependent simply prunes its edges, which is harmless.
  QTemporaryDir dir;
  DataManager manager;

  const AssetId externalInput =
    registerRaster( manager, stageRaster( dir, QStringLiteral( "in.tif" ) ) );
  const AssetId dependent =
    registerRaster( manager, stageRaster( dir, QStringLiteral( "dep.tif" ) ) );
  REQUIRE( manager.addStrongDependency( dependent, externalInput ) );

  // The DEPENDENT lives in the collection; its input is external.
  const CollectionId collectionId =
    manager.createCollection( { QStringLiteral( "scene" ), ProductMetadata() } ).collectionId;
  REQUIRE( manager.addChildToCollection( collectionId, dependent ) );

  REQUIRE( manager.unloadCollection( collectionId, /*cascade=*/true ) );

  // The collection and the dependent are gone; the external input survives and
  // its consumer list is now empty (edge pruned, not orphaned).
  CHECK_FALSE( manager.collection( collectionId ).has_value() );
  CHECK_FALSE( manager.asset( dependent ).has_value() );
  CHECK( manager.asset( externalInput ).has_value() );
  CHECK( manager.strongDependentsOf( externalInput ).isEmpty() );
}
