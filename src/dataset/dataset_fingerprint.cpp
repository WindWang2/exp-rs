// dataset_fingerprint.cpp — SHA-256 over the canonical manifest form.
//
// Deliberately reuses the platform's ONE canonical JSON serializer
// (sicnu::data::canonicalizeJsonRfc8785) so "canonical" has a single
// meaning across execution fingerprints and dataset fingerprints.
#include "dataset_fingerprint.h"

#include "../data/execution_fingerprint.h"

#include <QCryptographicHash>

namespace sicnu::dataset
{

DatasetFingerprint makeDatasetFingerprint( const QJsonObject &manifestJson )
{
    QJsonObject content = manifestJson;
    content.remove( QStringLiteral( "fingerprint" ) );
    const QByteArray canonical = sicnu::data::canonicalizeJsonRfc8785( content );
    return DatasetFingerprint(
        QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ) );
}

bool manifestFingerprintMatches( const QJsonObject &manifestJson )
{
    const QString recorded =
        manifestJson.value( QStringLiteral( "fingerprint" ) ).toString();
    if ( recorded.isEmpty() )
        return false;
    return makeDatasetFingerprint( manifestJson ).toHex() == recorded;
}

} // namespace sicnu::dataset
