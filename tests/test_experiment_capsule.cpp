// test_experiment_capsule.cpp — RS14-17 ReproducibilityCapsule.
//
// Slice A: schema contract + deterministic canonicalization + self digest.
// The capsule is a PROJECTION of recorded experiment truth; these tests pin
// the document contract (canonical bytes, digest semantics, shape gates)
// before any builder exists.
#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "experiment/capsule/capsule_document.h"

namespace
{

using sicnu::experiment::capsule::CapsuleDocument;
using sicnu::experiment::capsule::CapsuleValidation;
using sicnu::experiment::capsule::capsuleDigest;
using sicnu::experiment::capsule::capsuleDigestBody;
using sicnu::experiment::capsule::validateShape;

/// A minimal but well-formed capsule payload (no digest section yet).
QJsonObject minimalPayload( const QString &capsuleId = QStringLiteral( "capsule-run-1" ) )
{
    QJsonObject payload;
    QJsonObject schema;
    schema.insert( QStringLiteral( "id" ), QStringLiteral( "sicnu.capsule" ) );
    schema.insert( QStringLiteral( "version" ), 1 );
    payload.insert( QStringLiteral( "schema" ), schema );
    payload.insert( QStringLiteral( "capsule_id" ), capsuleId );
    payload.insert( QStringLiteral( "created_utc" ), QStringLiteral( "2026-09-21T00:00:00Z" ) );
    QJsonObject goal;
    goal.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-1" ) );
    payload.insert( QStringLiteral( "goal" ), goal );
    payload.insert( QStringLiteral( "software" ), QJsonObject{} );
    payload.insert( QStringLiteral( "capabilities" ), QJsonArray{} );
    payload.insert( QStringLiteral( "inputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "parameters" ), QJsonObject{} );
    payload.insert( QStringLiteral( "plan" ), QJsonObject{} );
    payload.insert( QStringLiteral( "environment" ), QJsonObject{} );
    payload.insert( QStringLiteral( "outputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "evidence" ), QJsonObject{} );
    payload.insert( QStringLiteral( "provenance" ), QJsonObject{} );
    return payload;
}

/// Same content as minimalPayload but a DIFFERENT key insertion order — the
/// digest must not be able to tell the difference.
QJsonObject minimalPayloadReordered()
{
    QJsonObject payload;
    payload.insert( QStringLiteral( "parameters" ), QJsonObject{} );
    payload.insert( QStringLiteral( "provenance" ), QJsonObject{} );
    payload.insert( QStringLiteral( "schema" ), [&] {
        QJsonObject s;
        s.insert( QStringLiteral( "version" ), 1 );
        s.insert( QStringLiteral( "id" ), QStringLiteral( "sicnu.capsule" ) );
        return s;
    }() );
    payload.insert( QStringLiteral( "outputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "capsule_id" ), QStringLiteral( "capsule-run-1" ) );
    payload.insert( QStringLiteral( "evidence" ), QJsonObject{} );
    payload.insert( QStringLiteral( "created_utc" ), QStringLiteral( "2026-09-21T00:00:00Z" ) );
    payload.insert( QStringLiteral( "software" ), QJsonObject{} );
    payload.insert( QStringLiteral( "inputs" ), QJsonArray{} );
    payload.insert( QStringLiteral( "plan" ), QJsonObject{} );
    payload.insert( QStringLiteral( "environment" ), QJsonObject{} );
    payload.insert( QStringLiteral( "goal" ), [&] {
        QJsonObject g;
        g.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-1" ) );
        return g;
    }() );
    payload.insert( QStringLiteral( "capabilities" ), QJsonArray{} );
    return payload;
}

} // namespace

TEST_CASE( "capsule digest is stable under key insertion order", "[capsule][digest]" )
{
    const QString digestA = capsuleDigest( capsuleDigestBody( minimalPayload() ) );
    const QString digestB = capsuleDigest( capsuleDigestBody( minimalPayloadReordered() ) );
    REQUIRE( !digestA.isEmpty() );
    CHECK( digestA == digestB );
}

TEST_CASE( "capsule digest changes when recorded content changes", "[capsule][digest]" )
{
    QJsonObject changed = minimalPayload();
    QJsonObject goal = changed.value( QStringLiteral( "goal" ) ).toObject();
    goal.insert( QStringLiteral( "experiment_id" ), QStringLiteral( "exp-OTHER" ) );
    changed.insert( QStringLiteral( "goal" ), goal );

    const QString digestA = capsuleDigest( capsuleDigestBody( minimalPayload() ) );
    const QString digestB = capsuleDigest( capsuleDigestBody( changed ) );
    CHECK( digestA != digestB );
}

TEST_CASE( "finalize stamps a self digest that verifies and detects tampering",
           "[capsule][digest]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    const CapsuleDocument doc = finalized.take();
    CHECK( doc.digestValid() );

    // Tamper with recorded content: the recorded digest no longer verifies.
    QJsonObject tamperedRoot = doc.root();
    tamperedRoot.insert( QStringLiteral( "capsule_id" ), QStringLiteral( "capsule-run-1-TAMPERED" ) );
    CapsuleDocument tampered = CapsuleDocument::fromRoot( tamperedRoot );
    CHECK( !tampered.digestValid() );
}

TEST_CASE( "digest body excludes the digest section itself", "[capsule][digest]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    const CapsuleDocument doc = finalized.take();

    // Re-deriving the digest body must not recurse into the digest section.
    const QString recomputed = capsuleDigest( capsuleDigestBody( doc.root() ) );
    CHECK( recomputed == doc.digestValue() );
}

TEST_CASE( "canonical bytes are identical for semantically identical documents",
           "[capsule][canonical]" )
{
    auto a = CapsuleDocument::finalize( minimalPayload() );
    auto b = CapsuleDocument::finalize( minimalPayloadReordered() );
    REQUIRE( a.has_value() );
    REQUIRE( b.has_value() );
    CHECK( a->canonicalBytes() == b->canonicalBytes() );
    CHECK( a->root().value( QStringLiteral( "digest" ) ).toObject()
               .value( QStringLiteral( "value" ) ).toString()
           == b->root().value( QStringLiteral( "digest" ) ).toObject()
                  .value( QStringLiteral( "value" ) ).toString() );
}

TEST_CASE( "finalize refuses payloads that already carry a digest section",
           "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    QJsonObject digest;
    digest.insert( QStringLiteral( "algorithm" ), QStringLiteral( "sha256-canonical-json" ) );
    digest.insert( QStringLiteral( "value" ), QStringLiteral( "00" ) );
    payload.insert( QStringLiteral( "digest" ), digest );

    auto refused = CapsuleDocument::finalize( payload );
    REQUIRE( !refused.has_value() );
    bool sawCode = false;
    for ( const auto &diagnostic : refused.diagnostics() )
        if ( diagnostic.code == QLatin1String( "capsule.digest-present" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: missing schema block is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    payload.remove( QStringLiteral( "schema" ) );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.schema-missing" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: unknown schema id is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    QJsonObject schema;
    schema.insert( QStringLiteral( "id" ), QStringLiteral( "some.other.capsule" ) );
    schema.insert( QStringLiteral( "version" ), 1 );
    payload.insert( QStringLiteral( "schema" ), schema );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.schema-unknown" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: unsupported schema version is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    QJsonObject schema;
    schema.insert( QStringLiteral( "id" ), QStringLiteral( "sicnu.capsule" ) );
    schema.insert( QStringLiteral( "version" ), 99 );
    payload.insert( QStringLiteral( "schema" ), schema );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.schema-unsupported" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: missing capsule id is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    payload.remove( QStringLiteral( "capsule_id" ) );
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.identity-missing" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: digest mismatch is refused", "[capsule][schema]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    QJsonObject tamperedRoot = finalized->root();
    QJsonObject digest = tamperedRoot.value( QStringLiteral( "digest" ) ).toObject();
    digest.insert( QStringLiteral( "value" ), QStringLiteral( "deadbeef" ) );
    tamperedRoot.insert( QStringLiteral( "digest" ), digest );

    const CapsuleValidation validation = validateShape( tamperedRoot );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.digest-mismatch" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: unknown digest algorithm is refused", "[capsule][schema]" )
{
    QJsonObject payload = minimalPayload();
    // Build a complete document, then swap the algorithm without fixing the
    // value: the digest gate must catch the un-verifiable algorithm first.
    auto finalized = CapsuleDocument::finalize( payload );
    REQUIRE( finalized.has_value() );
    QJsonObject tamperedRoot = finalized->root();
    QJsonObject digest = tamperedRoot.value( QStringLiteral( "digest" ) ).toObject();
    digest.insert( QStringLiteral( "algorithm" ), QStringLiteral( "md5" ) );
    tamperedRoot.insert( QStringLiteral( "digest" ), digest );

    const CapsuleValidation validation = validateShape( tamperedRoot );
    CHECK( !validation.ok );
    bool sawCode = false;
    for ( const auto &issue : validation.issues )
        if ( issue.code == QLatin1String( "capsule.digest-algorithm-unknown" ) )
            sawCode = true;
    CHECK( sawCode );
}

TEST_CASE( "shape validation: a finalized minimal document passes", "[capsule][schema]" )
{
    auto finalized = CapsuleDocument::finalize( minimalPayload() );
    REQUIRE( finalized.has_value() );
    const CapsuleValidation validation = validateShape( finalized->root() );
    if ( !validation.ok )
    {
        for ( const auto &issue : validation.issues )
            WARN( issue.code.toStdString() << ": " << issue.message.toStdString() );
    }
    CHECK( validation.ok );
    CHECK( validation.issues.empty() );
}
