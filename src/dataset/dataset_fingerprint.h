// dataset_fingerprint.h — content identity for dataset versions (ADR 0134).
//
// A fingerprint is a full SHA-256 digest over the CANONICAL form of a
// manifest payload (canonicalizeJsonRfc8785). It answers "is this the same
// content", independent of storage location, row order in a UI, or the
// logical identity of the version. Two fingerprints equal + identities
// different = same content produced twice (the duplicate-detection input,
// goal §36).
//
// The `fingerprint` member of the manifest is excluded from hashing, and
// volatile context (store timestamps) must live outside the hashed payload.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace sicnu::dataset
{

class DatasetFingerprint
{
  public:
    DatasetFingerprint() = default;

    explicit DatasetFingerprint( QByteArray digest )
      : m_digest( std::move( digest ) )
    {
    }

    /// Lowercase hex (64 chars) when valid.
    QString toHex() const { return QString::fromUtf8( m_digest.toHex() ); }
    bool isValid() const { return !m_digest.isEmpty(); }
    const QByteArray &digest() const { return m_digest; }

    bool operator==( const DatasetFingerprint & ) const = default;

  private:
    QByteArray m_digest;
};

/// Hash the canonical form of @p manifestJson. Any top-level `fingerprint`
/// field is removed before hashing (self-reference), so callers may pass the
/// full serialization. The input object is not modified.
DatasetFingerprint makeDatasetFingerprint( const QJsonObject &manifestJson );

/// True when the recorded fingerprint of a manifest payload matches its own
/// content (integrity check used on load and by reproduction validation).
bool manifestFingerprintMatches( const QJsonObject &manifestJson );

} // namespace sicnu::dataset
