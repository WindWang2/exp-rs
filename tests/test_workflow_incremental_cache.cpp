#include <catch2/catch_test_macros.hpp>

#include "data/execution_fingerprint.h"
#include <QJsonObject>
#include <QJsonArray>

using namespace sicnu::data;

TEST_CASE( "RFC 8785 canonical JSON serializer", "[workflow][v2][rfc8785]" )
{
  QJsonObject obj1;
  obj1["z"] = 100;
  obj1["a"] = "first";
  obj1["m"] = true;

  QJsonObject obj2;
  obj2["a"] = "first";
  obj2["m"] = true;
  obj2["z"] = 100;

  const QByteArray canon1 = canonicalizeJsonRfc8785( obj1 );
  const QByteArray canon2 = canonicalizeJsonRfc8785( obj2 );

  REQUIRE( canon1 == canon2 );
  REQUIRE( canon1 == "{\"a\":\"first\",\"m\":true,\"z\":100}" );
}

TEST_CASE( "makeExecutionFingerprintV2 determinism and revision sensitivity", "[workflow][v2][fingerprint]" )
{
  const AssetId assetA = AssetId::generate();
  const AssetId assetB = AssetId::generate();

  TaggedDerivationInput in1;
  in1.assetId = assetA;
  in1.revision = AssetRevision::fromValue( 1 );
  in1.fromPort = "output";
  in1.toPort = "inputA";

  TaggedDerivationInput in2;
  in2.assetId = assetB;
  in2.revision = AssetRevision::fromValue( 2 );
  in2.fromPort = "output";
  in2.toPort = "inputB";

  QJsonObject params;
  params["threshold"] = 0.5;
  params["mode"] = "fast";

  // Identical construction
  const auto fp1 = makeExecutionFingerprintV2( "rs:bandmath", "1.0", params, { in1, in2 } );
  const auto fp2 = makeExecutionFingerprintV2( "rs:bandmath", "1.0", params, { in2, in1 } ); // order flipped
  REQUIRE( fp1.isValid() );
  REQUIRE( fp1 == fp2 );

  // Changed parameter -> different fingerprint
  QJsonObject paramsChanged = params;
  paramsChanged["threshold"] = 0.75;
  const auto fpParamChanged = makeExecutionFingerprintV2( "rs:bandmath", "1.0", paramsChanged, { in1, in2 } );
  REQUIRE_FALSE( fp1 == fpParamChanged );

  // Changed revision -> different fingerprint
  TaggedDerivationInput in1Updated = in1;
  in1Updated.revision = AssetRevision::fromValue( 2 );
  const auto fpRevChanged = makeExecutionFingerprintV2( "rs:bandmath", "1.0", params, { in1Updated, in2 } );
  REQUIRE_FALSE( fp1 == fpRevChanged );

  // Changed port -> different fingerprint
  TaggedDerivationInput in1PortChanged = in1;
  in1PortChanged.toPort = "inputOther";
  const auto fpPortChanged = makeExecutionFingerprintV2( "rs:bandmath", "1.0", params, { in1PortChanged, in2 } );
  REQUIRE_FALSE( fp1 == fpPortChanged );
}

TEST_CASE( "Fingerprints distinguish doubles that 16-digit formatting merges", "[workflow][v2][fingerprint][numbers]" )
{
  const AssetId asset = AssetId::generate();
  TaggedDerivationInput in;
  in.assetId = asset;
  in.revision = AssetRevision::fromValue( 1 );

  QJsonObject shortVal;
  shortVal["x"] = 0.3;

  QJsonObject longVal;
  longVal["x"] = 0.30000000000000004; // differs from 0.3 only in the 17th significant digit

  const auto fpShort = makeExecutionFingerprintV2( "rs:op", "1.0", shortVal, { in } );
  const auto fpLong = makeExecutionFingerprintV2( "rs:op", "1.0", longVal, { in } );
  REQUIRE( fpShort.isValid() );
  REQUIRE_FALSE( fpShort == fpLong ); // regression: 'g',16 canonicalized both to "0.3"

  // Canonical serializer details. Note: QJsonValue normalizes -0.0 to +0.0 on
  // storage, so the ES6 "-0" distinction is not observable through QJsonObject
  // inputs; the serializer branch exists for direct-call correctness.
  QJsonObject negZero;
  negZero["z"] = -0.0;
  QJsonObject posZero;
  posZero["z"] = 0.0;
  REQUIRE( canonicalizeJsonRfc8785( negZero ) == canonicalizeJsonRfc8785( posZero ) );

  QJsonObject huge;
  huge["n"] = 1e19; // beyond qint64: must not hit UB in the integer fast path
  REQUIRE( canonicalizeJsonRfc8785( huge ).contains( "1e" ) );

  QJsonObject roundTrips;
  roundTrips["a"] = 0.1;
  const QByteArray canon = canonicalizeJsonRfc8785( roundTrips );
  REQUIRE( canon == "{\"a\":0.1}" ); // shortest round-trip keeps "0.1", not "0.10000000000000001"
}

TEST_CASE( "Fingerprints are order-independent when inputs share ports and asset", "[workflow][v2][fingerprint][ordering]" )
{
  const AssetId asset = AssetId::generate();

  // Same (toPort, fromPort, assetId, revision) but differing band references
  // and content digest: ordering over these extra fields must not depend on
  // the caller's list order.
  TaggedDerivationInput inA;
  inA.assetId = asset;
  inA.revision = AssetRevision::fromValue( 3 );
  inA.toPort = "input";
  inA.bandReferences = { "B1", "B2" };
  inA.lazyContentDigest = "aaaa";

  TaggedDerivationInput inB;
  inB.assetId = asset;
  inB.revision = AssetRevision::fromValue( 3 );
  inB.toPort = "input";
  inB.bandReferences = { "B3" };
  inB.lazyContentDigest = "bbbb";

  QJsonObject params;

  const auto fp1 = makeExecutionFingerprintV2( "rs:op", "1.0", params, { inA, inB } );
  const auto fp2 = makeExecutionFingerprintV2( "rs:op", "1.0", params, { inB, inA } );
  REQUIRE( fp1.isValid() );
  REQUIRE( fp1 == fp2 );

  // And a differing digest alone still changes the fingerprint
  TaggedDerivationInput inBMutated = inB;
  inBMutated.lazyContentDigest = "cccc";
  const auto fp3 = makeExecutionFingerprintV2( "rs:op", "1.0", params, { inA, inBMutated } );
  REQUIRE_FALSE( fp1 == fp3 );
}

TEST_CASE( "ExecutionResultCache with V2 fingerprints", "[workflow][v2][cache]" )
{
  auto &cache = ExecutionResultCache::instance();
  cache.clear();
  cache.setEnabled( true );

  const AssetId inputAsset = AssetId::generate();
  const AssetId outputAsset = AssetId::generate();

  TaggedDerivationInput in;
  in.assetId = inputAsset;
  in.revision = AssetRevision::fromValue( 1 );
  in.toPort = "input";

  QJsonObject params;
  params["scale"] = 2.0;

  const auto fp = makeExecutionFingerprintV2( "rs:resample", "1.0", params, { in } );

  // Initial miss
  REQUIRE_FALSE( cache.lookup( fp ).has_value() );

  // Store and hit
  cache.store( fp, outputAsset );
  auto hit = cache.lookup( fp );
  REQUIRE( hit.has_value() );
  REQUIRE( *hit == outputAsset );

  // Invalidate
  cache.invalidate( fp );
  REQUIRE_FALSE( cache.lookup( fp ).has_value() );

  cache.clear();
  cache.setEnabled( false );
}