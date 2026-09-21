// capsule_document.h — RS14-17 ReproducibilityCapsule: the versioned,
// canonicalized, portable projection of ONE experiment run's reproducibility
// facts.
//
// The capsule is a DOCUMENT, not a store: every section is projected from
// recorded truth (ExperimentStore / DatasetStore / EvidenceProjector /
// lineage) or arrives through explicitly injected provider hooks
// (capability descriptors, verifier summaries). Nothing is computed,
// inferred, or silently repaired — the same projection-not-computation
// doctrine as evidence.h and lab_report.h.
//
// Identity doctrine:
//   - the capsule digest is SHA-256 over canonicalizeJsonRfc8785 of the
//     document WITHOUT its self-describing "digest" member (same hashing
//     doctrine as runExecutionFingerprint, ADR 0137);
//   - canonical bytes make key insertion order, whitespace and number
//     formatting irrelevant — two machines produce byte-identical capsules
//     for identical recorded facts;
//   - absolute local paths must never enter the document (Slice G enforces
//     portability refs; validate refuses them as identity).
//
// Schema: id "sicnu.capsule", integer version kCapsuleSchemaVersion.
// Readers refuse unknown ids and unsupported versions with typed issues —
// never a best-effort guess.
#pragma once

#include "dataset/dataset_types.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::experiment::capsule
{

using sicnu::dataset::Diagnostic;
using sicnu::dataset::Result;

inline constexpr const char *kCapsuleSchemaId = "sicnu.capsule";
inline constexpr int kCapsuleSchemaVersion = 1;
inline constexpr const char *kCapsuleDigestAlgorithm = "sha256-canonical-json";

/// Machine-readable issue codes used by validation (prefix "capsule."):
///   capsule.digest-present          finalize() got a payload that already
///                                   carries a digest section
///   capsule.schema-missing          no/invalid "schema" block
///   capsule.schema-unknown          schema id is not sicnu.capsule
///   capsule.schema-unsupported      known id, version this reader refuses
///   capsule.identity-missing        no capsule_id
///   capsule.digest-missing          no/invalid digest section
///   capsule.digest-algorithm-unknown digest algorithm this reader refuses
///   capsule.digest-mismatch         recorded digest ≠ recomputed digest
struct CapsuleIssue
{
    QString code;
    QString section; ///< document section the issue attaches to ("" = whole)
    QString message;

    QJsonObject toJson() const;
};

/// Verdict of a validation pass: ok only when NO issue fired. @p checks
/// carries one evidence line per gate that passed (ReplayCheck style: a
/// judgment is only as good as its evidence).
struct CapsuleValidation
{
    bool ok = false;
    QVector<CapsuleIssue> issues;
    QStringList checks;

    QJsonObject toJson() const;
};

/// The document value. The wire JSON is the single representation — C++
/// accessors only read the load-bearing members, they never maintain a
/// parallel truth.
class CapsuleDocument
{
  public:
    CapsuleDocument() = default;

    /// Adopts a COMPLETE root (digest section present). No validation here;
    /// use validateShape / CapsuleIO::validate for gate checks.
    static CapsuleDocument fromRoot( const QJsonObject &root );

    /// Completes @p payload (which must NOT carry a digest member) by
    /// stamping the self digest. Refuses with capsule.digest-present when a
    /// digest section is already present (double-digest guard).
    static Result<CapsuleDocument> finalize( QJsonObject payload );

    const QJsonObject &root() const { return m_root; }

    QString capsuleId() const;
    /// The recorded digest value ("" when absent).
    QString digestValue() const;
    /// Recomputes the digest over the root without the digest member.
    QString computedDigest() const;
    /// True when the recorded digest section names the supported algorithm
    /// and its value equals computedDigest().
    bool digestValid() const;

    /// canonicalizeJsonRfc8785 over the full root — the wire bytes.
    QByteArray canonicalBytes() const;

  private:
    explicit CapsuleDocument( QJsonObject root );

    QJsonObject m_root;
};

/// The digest body: @p root minus the "digest" member.
QJsonObject capsuleDigestBody( const QJsonObject &root );

/// SHA-256 hex over canonicalizeJsonRfc8785( @p digestBody ).
QString capsuleDigest( const QJsonObject &digestBody );

/// Shape-level contract of a capsule document: schema known? identity
/// present? digest present, well-formed and matching? Content-level gates
/// (secret scan, portable-path scan, section semantics) live in
/// CapsuleIO::validate — this function NEVER reads the filesystem.
CapsuleValidation validateShape( const QJsonObject &root );

} // namespace sicnu::experiment::capsule
