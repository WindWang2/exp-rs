// capsule_io.h — export / load / validate for ReproducibilityCapsules.
//
// The capsule on disk is the CANONICAL byte form of the document — not a
// pretty-printed convenience copy. Integrity is the point:
//   - export refuses a document whose self digest does not verify;
//   - load re-derives the canonical bytes and refuses anything else
//     (capsule.not-canonical), then runs the shape gates;
//   - validate runs the content gates over the parsed document and REFUSES
//     (typed issues, fail closed): secret-shaped names/values (the same
//     denylists the run record filters by) and absolute-path values — an
//     absolute local path must never be identity.
#pragma once

#include "capsule_document.h"

#include <QByteArray>
#include <QString>

namespace sicnu::experiment::capsule
{

struct CapsuleExportReport
{
    bool ok = false;
    QString path;
    qint64 bytes = 0;

    QJsonObject toJson() const;
};

class CapsuleIO
{
  public:
    /// Writes @p doc's canonical bytes to @p path (parent directory created
    /// as needed). Refuses documents whose digest does not verify.
    static Result<CapsuleExportReport> exportCapsule( const CapsuleDocument &doc,
                                                      const QString &path );

    /// Reads @p path and parses it. Canonical-form gate, then shape gates.
    static Result<CapsuleDocument> loadCapsule( const QString &path );

    /// Parses @p bytes. Canonical-form gate, then shape gates. All failures
    /// carry typed diagnostic codes (capsule.parse-error,
    /// capsule.not-canonical, capsule.schema-*, capsule.digest-*,
    /// capsule.identity-missing).
    static Result<CapsuleDocument> fromBytes( const QByteArray &bytes );

    /// Full document validation: shape gates + content gates (secret scan,
    /// absolute-path scan). Never touches the filesystem; pass/fail with
    /// per-gate evidence.
    static CapsuleValidation validate( const CapsuleDocument &doc );
};

} // namespace sicnu::experiment::capsule
