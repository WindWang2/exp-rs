// capsule_document.cpp — schema contract + deterministic canonicalization +
// self digest for the RS14-17 ReproducibilityCapsule.
//
// Determinism contract: identical payload ⇒ identical canonical bytes and
// digest, independent of QJsonObject key insertion order — canonicalization
// is delegated to sicnu::data::canonicalizeJsonRfc8785 (the same hashing
// doctrine as the ADR 0137 experiment fingerprints; no second serializer).
#include "capsule_document.h"

#include "data/execution_fingerprint.h"

#include <QCryptographicHash>
#include <QJsonArray>

namespace sicnu::experiment::capsule
{

namespace
{

CapsuleIssue makeIssue( const QString &code, const QString &section, const QString &message )
{
    return CapsuleIssue{ code, section, message };
}

Diagnostic diagnostic( const QString &code, const QString &message )
{
    return Diagnostic{ code, message, sicnu::dataset::DiagnosticSeverity::Error };
}

bool schemaBlockIs( const QJsonObject &root, QString *schemaIdOut, int *schemaVersionOut )
{
    const QJsonObject schema = root.value( QStringLiteral( "schema" ) ).toObject();
    if ( schema.isEmpty() || !schema.contains( QStringLiteral( "id" ) )
         || !schema.contains( QStringLiteral( "version" ) ) )
        return false;
    if ( schemaIdOut )
        *schemaIdOut = schema.value( QStringLiteral( "id" ) ).toString();
    if ( schemaVersionOut )
        *schemaVersionOut = schema.value( QStringLiteral( "version" ) ).toInt( -1 );
    return true;
}

} // namespace

QJsonObject CapsuleIssue::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "code" ), code );
    json.insert( QStringLiteral( "section" ), section );
    json.insert( QStringLiteral( "message" ), message );
    return json;
}

QJsonObject CapsuleValidation::toJson() const
{
    QJsonArray issueArray;
    for ( const auto &i : issues )
        issueArray.append( i.toJson() );
    QJsonArray checkArray;
    for ( const auto &c : checks )
        checkArray.append( c );
    QJsonObject json;
    json.insert( QStringLiteral( "ok" ), ok );
    json.insert( QStringLiteral( "issues" ), issueArray );
    json.insert( QStringLiteral( "checks" ), checkArray );
    return json;
}

QJsonObject capsuleDigestBody( const QJsonObject &root )
{
    QJsonObject body = root;
    body.remove( QStringLiteral( "digest" ) );
    return body;
}

QString capsuleDigest( const QJsonObject &digestBody )
{
    return capsuleSha256Hex( sicnu::data::canonicalizeJsonRfc8785( digestBody ) );
}

QString capsuleSha256Hex( const QByteArray &bytes )
{
    return QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex() );
}

CapsuleDocument CapsuleDocument::fromRoot( const QJsonObject &root )
{
    return CapsuleDocument{ root };
}

CapsuleDocument::CapsuleDocument( QJsonObject root )
    : m_root{ std::move( root ) }
{
}

Result<CapsuleDocument> CapsuleDocument::finalize( QJsonObject payload )
{
    if ( payload.contains( QStringLiteral( "digest" ) ) )
    {
        return Result<CapsuleDocument>::failure( diagnostic(
            QStringLiteral( "capsule.digest-present" ),
            QStringLiteral( "payload already carries a digest section; finalize stamps exactly one" ) ) );
    }
    QJsonObject digest;
    digest.insert( QStringLiteral( "algorithm" ), QString::fromLatin1( kCapsuleDigestAlgorithm ) );
    digest.insert( QStringLiteral( "value" ), capsuleDigest( payload ) );
    payload.insert( QStringLiteral( "digest" ), digest );
    return Result<CapsuleDocument>::success( CapsuleDocument{ std::move( payload ) } );
}

QString CapsuleDocument::capsuleId() const
{
    return m_root.value( QStringLiteral( "capsule_id" ) ).toString();
}

QString CapsuleDocument::digestValue() const
{
    return m_root.value( QStringLiteral( "digest" ) ).toObject()
        .value( QStringLiteral( "value" ) )
        .toString();
}

QString CapsuleDocument::computedDigest() const
{
    return capsuleDigest( capsuleDigestBody( m_root ) );
}

bool CapsuleDocument::digestValid() const
{
    const QJsonObject digest = m_root.value( QStringLiteral( "digest" ) ).toObject();
    if ( digest.value( QStringLiteral( "algorithm" ) ).toString()
         != QLatin1String( kCapsuleDigestAlgorithm ) )
        return false;
    const QString recorded = digest.value( QStringLiteral( "value" ) ).toString();
    return !recorded.isEmpty() && recorded == computedDigest();
}

QByteArray CapsuleDocument::canonicalBytes() const
{
    return sicnu::data::canonicalizeJsonRfc8785( m_root );
}

CapsuleValidation validateShape( const QJsonObject &root )
{
    CapsuleValidation validation;

    QString schemaId;
    int schemaVersion = -1;
    if ( !schemaBlockIs( root, &schemaId, &schemaVersion ) )
    {
        validation.issues.append( makeIssue(
            QStringLiteral( "capsule.schema-missing" ), QStringLiteral( "schema" ),
            QStringLiteral( "document carries no {id, version} schema block" ) ) );
    }
    else if ( schemaId != QLatin1String( kCapsuleSchemaId ) )
    {
        validation.issues.append( makeIssue(
            QStringLiteral( "capsule.schema-unknown" ), QStringLiteral( "schema" ),
            QStringLiteral( "unknown schema id '%1'; this reader only knows '%2'" )
                .arg( schemaId, QString::fromLatin1( kCapsuleSchemaId ) ) ) );
    }
    else if ( schemaVersion != kCapsuleSchemaVersion )
    {
        validation.issues.append( makeIssue(
            QStringLiteral( "capsule.schema-unsupported" ), QStringLiteral( "schema" ),
            QStringLiteral( "schema version %1 unsupported; reader implements version %2" )
                .arg( schemaVersion )
                .arg( kCapsuleSchemaVersion ) ) );
    }
    else
    {
        validation.checks << QStringLiteral( "schema: sicnu.capsule v%1" ).arg( schemaVersion );
    }

    if ( root.value( QStringLiteral( "capsule_id" ) ).toString().isEmpty() )
    {
        validation.issues.append( makeIssue(
            QStringLiteral( "capsule.identity-missing" ), QStringLiteral( "capsule_id" ),
            QStringLiteral( "document carries no capsule_id" ) ) );
    }
    else
    {
        validation.checks << QStringLiteral( "identity: %1" )
                                 .arg( root.value( QStringLiteral( "capsule_id" ) ).toString() );
    }

    const QJsonObject digest = root.value( QStringLiteral( "digest" ) ).toObject();
    if ( digest.isEmpty() || digest.value( QStringLiteral( "value" ) ).toString().isEmpty() )
    {
        validation.issues.append( makeIssue(
            QStringLiteral( "capsule.digest-missing" ), QStringLiteral( "digest" ),
            QStringLiteral( "document carries no verifiable digest section" ) ) );
        validation.ok = false;
        return validation;
    }

    const QString algorithm = digest.value( QStringLiteral( "algorithm" ) ).toString();
    if ( algorithm != QLatin1String( kCapsuleDigestAlgorithm ) )
    {
        validation.issues.append( makeIssue(
            QStringLiteral( "capsule.digest-algorithm-unknown" ), QStringLiteral( "digest" ),
            QStringLiteral( "digest algorithm '%1' unsupported; reader implements '%2'" )
                .arg( algorithm, QString::fromLatin1( kCapsuleDigestAlgorithm ) ) ) );
        validation.ok = false;
        return validation;
    }

    const CapsuleDocument doc = CapsuleDocument::fromRoot( root );
    if ( !doc.digestValid() )
    {
        validation.issues.append( makeIssue(
            QStringLiteral( "capsule.digest-mismatch" ), QStringLiteral( "digest" ),
            QStringLiteral( "recorded digest does not verify over the document body" ) ) );
        validation.ok = false;
        return validation;
    }
    validation.checks << QStringLiteral( "digest: %1" ).arg( doc.digestValue() );

    validation.ok = validation.issues.isEmpty();
    return validation;
}

} // namespace sicnu::experiment::capsule
