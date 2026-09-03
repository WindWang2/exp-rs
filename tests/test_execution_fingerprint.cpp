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

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

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

TEST_CASE( "RFC 8785 canonical form: -0.0 and 0.0 serialize identically",
           "[cache][fingerprint]" )
{
  // RFC 8785 §3.2.2.3: negative zero serializes as "0". Both zero signs are
  // the same parameter value and must not split the cache into two entries.
  const auto z = canonicalizeJsonRfc8785( QJsonObject{ { "x", 0.0 } } );
  const auto nz = canonicalizeJsonRfc8785( QJsonObject{ { "x", -0.0 } } );
  REQUIRE( z == nz );
  REQUIRE( z == QByteArray( R"({"x":0})" ) );
}

TEST_CASE( "RFC 8785 canonical form: shortest round-trip distinguishes near-equal doubles",
           "[cache][fingerprint]" )
{
  const auto a = canonicalizeJsonRfc8785( QJsonObject{ { "x", 0.3 } } );
  const auto b = canonicalizeJsonRfc8785( QJsonObject{ { "x", 0.30000000000000004 } } );
  REQUIRE( a != b );
  REQUIRE( a == QByteArray( R"({"x":0.3})" ) );
}

TEST_CASE( "ExecutionFingerprint v1 framing is not delimiter-injectable",
           "[cache][fingerprint]" )
{
  // An adversarial algorithm id containing the '\nver=' framing must not
  // alias a genuinely different (id, version) pair: before field escaping
  // these two tuples produced byte-identical canonical forms.
  const QJsonObject params;
  const QVector<DerivationInput> inputs{ makeInput( AssetId::generate(), AssetRevision::initial() ) };

  const auto injected = makeExecutionFingerprint( "rs:x\nver=2.0", "1.0", params, inputs );
  const auto genuine = makeExecutionFingerprint( "rs:x", "2.0", params, inputs );
  REQUIRE_FALSE( injected == genuine );
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

TEST_CASE( "ExecutionResultCache: bounded — evicts least-recently-used beyond cap",
           "[cache][fingerprint]" )
{
  auto &cache = ExecutionResultCache::instance();
  cache.clear();
  cache.setEnabled( true );
  cache.setMaxEntries( 3 );

  const AssetId inputAsset = AssetId::generate();
  const QJsonObject params;
  const auto fpFor = [&]( int i ) {
    return makeExecutionFingerprint( "rs:bounded", "1.0", params,
                                     { makeInput( inputAsset, AssetRevision::fromValue( i ) ) } );
  };

  // Fill the cache to capacity.
  const auto fp1 = fpFor( 1 );
  const auto fp2 = fpFor( 2 );
  const auto fp3 = fpFor( 3 );
  cache.store( fp1, AssetId::generate() );
  cache.store( fp2, AssetId::generate() );
  cache.store( fp3, AssetId::generate() );
  REQUIRE( cache.size() == 3 );

  // Touch fp1 (most recently used) and fp2; fp3 becomes least-recently-used.
  REQUIRE( cache.lookup( fp1 ).has_value() );
  REQUIRE( cache.lookup( fp2 ).has_value() );

  // Inserting a 4th entry must evict the LRU entry (fp3), not grow the map.
  const auto fp4 = fpFor( 4 );
  cache.store( fp4, AssetId::generate() );
  REQUIRE( cache.size() == 3 );
  REQUIRE_FALSE( cache.lookup( fp3 ).has_value() );
  REQUIRE( cache.lookup( fp1 ).has_value() );
  REQUIRE( cache.lookup( fp2 ).has_value() );
  REQUIRE( cache.lookup( fp4 ).has_value() );

  cache.setEnabled( false );
  cache.clear();
  cache.setMaxEntries( 4096 );
}
TEST_CASE( "TaggedDerivationInput chained producer fingerprint changes the digest (#726)",
           "[cache][fingerprint]" )
{
  const AssetId producer = AssetId::generate();
  sicnu::data::TaggedDerivationInput chained;
  chained.revision = sicnu::data::AssetRevision::initial();
  chained.toPort = QStringLiteral( "input" );
  chained.valueDomain = QStringLiteral( "pipeline_output" );
  chained.producerFingerprint = producer.toString();

  const QJsonObject params{ { "kernel", 3 } };

  const auto base = sicnu::data::makeExecutionFingerprintV2(
      "rs:majority_filter", "impl", params, { chained } );

  sicnu::data::TaggedDerivationInput upstream = chained;
  upstream.producerFingerprint = AssetId::generate().toString();
  const auto changedUpstream = sicnu::data::makeExecutionFingerprintV2(
      "rs:majority_filter", "impl", params, { upstream } );
  // Any change upstream re-keys every downstream step.
  REQUIRE( base != changedUpstream );

  // Port wiring is part of the identity: the same producer through a
  // different consuming port hashes differently.
  sicnu::data::TaggedDerivationInput otherPort = chained;
  otherPort.toPort = QStringLiteral( "mask" );
  const auto rewired = sicnu::data::makeExecutionFingerprintV2(
      "rs:majority_filter", "impl", params, { otherPort } );
  REQUIRE( base != rewired );
}

TEST_CASE( "Execution cache stores full results and never serves a replaced path (#726)",
           "[cache][fingerprint]" )
{
  auto &cache = ExecutionResultCache::instance();
  cache.clear();
  cache.setEnabled( true );

  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString outA = dir.filePath( "out_a.tif" );
  const QString outB = dir.filePath( "out_b.tif" );
  { QFile f( outA ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( "A" ); }
  { QFile f( outB ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( "B" ); }

  const ExecutionFingerprint fp1{ QByteArray( 32, '\x01' ) };
  const ExecutionFingerprint fp2{ QByteArray( 32, '\x02' ) };

  ExecutionResultCache::CachedExecution execution;
  execution.declaredOutputPath = outA;
  execution.producedArtifacts = { outA };
  execution.artifactSizes.insert( outA, QFileInfo( outA ).size() );
  execution.artifactMsecs.insert( outA, QFileInfo( outA ).lastModified().toMSecsSinceEpoch() );
  execution.resultPayload = QJsonDocument( QJsonObject{ { "output", outA } } );
  cache.storeExecution( fp1, execution );

  const auto served = cache.lookupExecution( fp1 );
  REQUIRE( served.has_value() );
  REQUIRE( served->declaredOutputPath == outA );
  REQUIRE( served->resultPayload.object().value( "output" ).toString() == outA );

  // A DIFFERENT execution writing the same destination evicts the old
  // fingerprint's claim: the path can only vouch for the newest bytes.
  ExecutionResultCache::CachedExecution execution2 = execution;
  execution2.declaredOutputPath = outA;
  execution2.producedArtifacts = { outA };
  execution2.artifactSizes.clear();
  execution2.artifactMsecs.clear();
  execution2.artifactSizes.insert( outA, QFileInfo( outA ).size() );
  execution2.artifactMsecs.insert( outA, QFileInfo( outA ).lastModified().toMSecsSinceEpoch() );
  execution2.resultPayload = QJsonDocument( QJsonObject{ { "output", outA } } );
  cache.storeExecution( fp2, execution2 );
  REQUIRE( cache.lookupExecution( fp1 ) == std::nullopt );
  REQUIRE( cache.lookupExecution( fp2 ).has_value() );

  // Missing artifacts self-heal to a miss.
  ExecutionResultCache::CachedExecution grouped = execution2;
  grouped.declaredOutputPath = outB; // grouping convention: never written
  grouped.producedArtifacts = { outA };
  cache.storeExecution( fp1, grouped );
  REQUIRE( cache.lookupExecution( fp1 ).has_value() ); // artifacts alive
  REQUIRE( QFile::remove( outA ) );
  REQUIRE( cache.lookupExecution( fp1 ) == std::nullopt ); // self-healed

  // A rewrite with DIFFERENT bytes (same path) is refused by the recorded
  // size/mtime stats — the core anti-poisoning branch (#726 review).
  ExecutionResultCache::CachedExecution poisoned = execution2;
  poisoned.declaredOutputPath = outB;
  poisoned.producedArtifacts = { outB };
  poisoned.artifactSizes.clear();
  poisoned.artifactMsecs.clear();
  poisoned.artifactSizes.insert( outB, QFileInfo( outB ).size() );
  poisoned.artifactMsecs.insert( outB, QFileInfo( outB ).lastModified().toMSecsSinceEpoch() );
  cache.storeExecution( fp2, poisoned );
  REQUIRE( cache.lookupExecution( fp2 ).has_value() );
  { QFile f( outB ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( "BBBB" ); }
  REQUIRE( cache.lookupExecution( fp2 ) == std::nullopt ); // bytes moved ⇒ miss

  // Chained INPUT stats are part of the claim: an intermediate rewritten
  // out-of-band invalidates the cached consumer (#726 review P1).
  { QFile f( outA ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( "out" ); }
  const QString inputA = dir.filePath( "chained_input.tif" );
  { QFile f( inputA ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( "in1" ); }
  ExecutionResultCache::CachedExecution consumer;
  consumer.declaredOutputPath = outA;
  consumer.producedArtifacts = { outA };
  consumer.artifactSizes.insert( outA, QFileInfo( outA ).size() );
  consumer.artifactMsecs.insert( outA, QFileInfo( outA ).lastModified().toMSecsSinceEpoch() );
  consumer.inputSizes.insert( inputA, QFileInfo( inputA ).size() );
  consumer.inputMsecs.insert( inputA, QFileInfo( inputA ).lastModified().toMSecsSinceEpoch() );
  const ExecutionFingerprint fpConsumer{ QByteArray( 32, '\x03' ) };
  cache.storeExecution( fpConsumer, consumer );
  REQUIRE( cache.lookupExecution( fpConsumer ).has_value() );
  { QFile f( inputA ); REQUIRE( f.open( QIODevice::WriteOnly ) ); f.write( "in2-longer" ); }
  REQUIRE( cache.lookupExecution( fpConsumer ) == std::nullopt );

  cache.clear();
  cache.setEnabled( false );
}

TEST_CASE( "Destination value under a non-output key stays hashed (#726)",
           "[cache][fingerprint]" )
{
  // C1 second bullet: {output:O, scratch:O} must not collapse onto
  // {output:O} — the scratch key is not output vocabulary, so its VALUE (the
  // destination string) is a hashed parameter.
  const AssetId a = AssetId::generate();
  const QJsonObject withScratch{
      { "output", "/w/o.tif" }, { "scratch", "/w/o.tif" } };
  const QJsonObject withoutScratch{ { "output", "/w/o.tif" } };
  const auto f1 = sicnu::data::makeExecutionFingerprintV2(
      "rs:x", "impl", withScratch, {} );
  const auto f2 = sicnu::data::makeExecutionFingerprintV2(
      "rs:x", "impl", withoutScratch, {} );
  REQUIRE( f1 != f2 );

  // The destination COLLAPSE itself lives one layer up: TaskCenter filters
  // output-vocabulary keys before hashing (the V2 hash hashes whatever it is
  // given). Pin the vocabulary that drives the filter — the injectivity of
  // the whole pipeline depends on it.
  using sicnu::data::isOutputVocabularyKey;
  REQUIRE( isOutputVocabularyKey( QStringLiteral( "output" ) ) );
  REQUIRE( isOutputVocabularyKey( QStringLiteral( "OUTPUT" ) ) );
  REQUIRE( isOutputVocabularyKey( QStringLiteral( "resultRaster" ) ) );
  REQUIRE( isOutputVocabularyKey( QStringLiteral( "outputPath" ) ) );
  REQUIRE( isOutputVocabularyKey( QStringLiteral( "resultRaster" ) ) );
  // "modelOut" carries neither "output" nor "result" — NOT vocabulary (this
  // is the platform's historical findOutputPathInParams contract).
  REQUIRE_FALSE( isOutputVocabularyKey( QStringLiteral( "scratch" ) ) );
  REQUIRE_FALSE( isOutputVocabularyKey( QStringLiteral( "input" ) ) );
  REQUIRE_FALSE( isOutputVocabularyKey( QStringLiteral( "kernel" ) ) );
}

TEST_CASE( "Execution cache fingerprints differ when the contract version differs (#726)",
           "[cache][fingerprint]" )
{
  // Contract-level regression guard: v2 semantics introduced the chained
  // producer field. A fingerprint computed with (and without) a producer
  // token must differ — the digest space of the old contract can never be
  // compared against the new one.
  const AssetId a = AssetId::generate();
  const QJsonObject params{ { "input", "x.tif" } };
  sicnu::data::TaggedDerivationInput plain;
  plain.assetId = a;
  plain.revision = AssetRevision::initial();
  plain.toPort = QStringLiteral( "input" );

  sicnu::data::TaggedDerivationInput chained = plain;
  chained.producerFingerprint = QStringLiteral( "deadbeef" );

  const auto withoutChain = sicnu::data::makeExecutionFingerprintV2(
      "rs:spectral_index", "impl", params, { plain } );
  const auto withChain = sicnu::data::makeExecutionFingerprintV2(
      "rs:spectral_index", "impl", params, { chained } );
  REQUIRE( withoutChain != withChain );
}
