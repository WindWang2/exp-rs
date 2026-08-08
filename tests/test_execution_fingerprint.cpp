// test_execution_fingerprint.cpp — revision-aware execution cache (Phase F).
//
// Verifies the fingerprint contract: deterministic for identical inputs,
// sensitive to algorithm version / parameters / input AssetRevision (the key
// invalidation property — a re-derived input must yield a different fingerprint
// so a stale cached result is never reused). Plus the in-memory cache's
// opt-in/lookup/store/invalidate behavior.
#include <catch2/catch_test_macros.hpp>

#include "data/execution_fingerprint.h"
#include "data/derivation_record.h"
#include "data/asset_types.h"

#include <QJsonObject>

using namespace sicnu::data;

namespace
{
DerivationInput makeInput( const AssetId &id, AssetRevision rev,
                           QStringList bands = {} )
{
  DerivationInput in;
  in.assetId = id;
  in.revision = rev;
  in.bandReferences = std::move( bands );
  return in;
}
} // namespace

TEST_CASE( "ExecutionFingerprint is deterministic for identical inputs",
           "[cache][fingerprint]" )
{
  const AssetId a = AssetId::generate();
  const QJsonObject params{ { "threshold", 0.5 }, { "index", "ndvi" } };
  const QVector<DerivationInput> inputs{ makeInput( a, AssetRevision::initial(), { "nir", "red" } ) };

  const auto f1 = makeExecutionFingerprint( "rs:spectral_index", "1.0", params, inputs );
  const auto f2 = makeExecutionFingerprint( "rs:spectral_index", "1.0", params, inputs );
  REQUIRE( f1 == f2 );
}

TEST_CASE( "ExecutionFingerprint is sensitive to algorithm version",
           "[cache][fingerprint]" )
{
  const AssetId a = AssetId::generate();
  const QJsonObject params;
  const QVector<DerivationInput> inputs{ makeInput( a, AssetRevision::initial() ) };

  const auto f1 = makeExecutionFingerprint( "rs:band_math", "1.0", params, inputs );
  const auto f2 = makeExecutionFingerprint( "rs:band_math", "1.1", params, inputs );
  REQUIRE_FALSE( f1 == f2 );
}

TEST_CASE( "ExecutionFingerprint is sensitive to parameters",
           "[cache][fingerprint]" )
{
  const AssetId a = AssetId::generate();
  const QVector<DerivationInput> inputs{ makeInput( a, AssetRevision::initial() ) };

  const auto f1 = makeExecutionFingerprint( "rs:band_math", "1.0",
                                            QJsonObject{ { "threshold", 0.5 } }, inputs );
  const auto f2 = makeExecutionFingerprint( "rs:band_math", "1.0",
                                            QJsonObject{ { "threshold", 0.6 } }, inputs );
  REQUIRE_FALSE( f1 == f2 );
}

TEST_CASE( "ExecutionFingerprint is insensitive to parameter insertion order",
           "[cache][fingerprint]" )
{
  const AssetId a = AssetId::generate();
  const QVector<DerivationInput> inputs{ makeInput( a, AssetRevision::initial() ) };
  // QJsonObject built in different orders — the canonical form sorts keys.
  QJsonObject p1;
  p1.insert( "a", 1 );
  p1.insert( "b", 2 );
  QJsonObject p2;
  p2.insert( "b", 2 );
  p2.insert( "a", 1 );

  const auto f1 = makeExecutionFingerprint( "rs:x", "1.0", p1, inputs );
  const auto f2 = makeExecutionFingerprint( "rs:x", "1.0", p2, inputs );
  REQUIRE( f1 == f2 );
}

TEST_CASE( "ExecutionFingerprint is revision-sensitive (the core invalidation property)",
           "[cache][fingerprint]" )
{
  // Same asset identity, two revisions. A re-derived input bumps the revision,
  // so the fingerprint MUST change → a stale cached result is never reused.
  const AssetId a = AssetId::generate();
  const QJsonObject params;
  const auto f1 = makeExecutionFingerprint( "rs:spectral_index", "1.0", params,
                                            { makeInput( a, AssetRevision::initial() ) } );
  const auto f2 = makeExecutionFingerprint( "rs:spectral_index", "1.0", params,
                                            { makeInput( a, AssetRevision::initial().next() ) } );
  REQUIRE_FALSE( f1 == f2 );
}

TEST_CASE( "fingerprintFromDerivation matches makeExecutionFingerprint",
           "[cache][fingerprint]" )
{
  DerivationRecord rec;
  rec.algorithmId = "rs:band_math";
  rec.algorithmVersion = "2.0";
  rec.parameters = QJsonObject{ { "expr", "b1+b2" } };
  rec.inputs = { makeInput( AssetId::generate(), AssetRevision::fromValue( 3 ) ) };

  const auto fromRec = fingerprintFromDerivation( rec );
  const auto direct = makeExecutionFingerprint( rec.algorithmId, rec.algorithmVersion,
                                                rec.parameters, rec.inputs );
  REQUIRE( fromRec == direct );
}

TEST_CASE( "ExecutionResultCache: off by default; opt-in lookup/store/invalidate",
           "[cache]" )
{
  auto &cache = ExecutionResultCache::instance();
  cache.clear();
  cache.setEnabled( false );

  const auto fp = makeExecutionFingerprint( "rs:x", "1.0", QJsonObject{}, {} );
  const AssetId out = AssetId::generate();

  // Disabled: store is a no-op, lookup returns nullopt.
  cache.store( fp, out );
  REQUIRE( cache.size() == 0 );
  REQUIRE_FALSE( cache.lookup( fp ).has_value() );

  // Enabled: store works, lookup hits, invalidate removes.
  cache.setEnabled( true );
  cache.store( fp, out );
  REQUIRE( cache.size() == 1 );
  auto hit = cache.lookup( fp );
  REQUIRE( hit.has_value() );
  REQUIRE( hit->toString() == out.toString() );

  cache.invalidate( fp );
  REQUIRE_FALSE( cache.lookup( fp ).has_value() );
  REQUIRE( cache.size() == 0 );

  // Different fingerprint misses.
  const AssetId out2 = AssetId::generate();
  cache.store( fp, out );
  const auto fpOther = makeExecutionFingerprint( "rs:y", "1.0", QJsonObject{}, {} );
  REQUIRE_FALSE( cache.lookup( fpOther ).has_value() );

  cache.setEnabled( false );
  cache.clear();
}

TEST_CASE( "ExecutionResultCache: revision change yields a miss (end-to-end invalidation)",
           "[cache][fingerprint]" )
{
  auto &cache = ExecutionResultCache::instance();
  cache.clear();
  cache.setEnabled( true );

  const AssetId inputAsset = AssetId::generate();
  const AssetId outputV1 = AssetId::generate();
  const QJsonObject params;

  // Run with input at revision 1 → cache the output.
  const auto fpV1 = makeExecutionFingerprint( "rs:spectral_index", "1.0", params,
                                              { makeInput( inputAsset, AssetRevision::initial() ) } );
  cache.store( fpV1, outputV1 );
  REQUIRE( cache.lookup( fpV1 ).has_value() );

  // Input is re-derived (revision bumped to 2). The new fingerprint differs →
  // cache MISS (the old output is NOT reused; the operator must re-run).
  const auto fpV2 = makeExecutionFingerprint( "rs:spectral_index", "1.0", params,
                                              { makeInput( inputAsset, AssetRevision::initial().next() ) } );
  REQUIRE_FALSE( cache.lookup( fpV2 ).has_value() );

  cache.setEnabled( false );
  cache.clear();
}
