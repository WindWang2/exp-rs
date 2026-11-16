// studio_submission.h — the identity of ONE submitted Studio study (R30-T07
// WP04). Qt Core only; no execution, no second record system.
//
// Problem: the dock validated design A, the user edited to B, then ran B, but
// the export still attached `m_lastDesignerVm` (A). A callback from run A
// could also overwrite a newer B session. One immutable snapshot per
// submission ties the draft, the spec, the report and the export bundle to
// the SAME identity, and lets an expired delivery be refused.
#pragma once

#include "experiment_studio/studio_errors.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace sicnu::experiment_studio
{

inline constexpr char kStudioSubmissionSchema[] = "sicnu.experiment_studio.submission/1";

struct StudioSubmission
{
    QString schema = QString::fromUtf8( kStudioSubmissionSchema );
    quint64 submissionId = 0;     ///< monotonic per dock instance
    QString storePath;            ///< experiment store the run was submitted to
    QString studyId;
    QString experimentId;
    QString algorithmId;
    QString specDigest;           ///< sha256 of the canonical normalized spec
    QJsonObject specJson;         ///< ParameterStudySpec::toJson()
    QJsonObject designerVm;       ///< validated designer VM at submit time

    QJsonObject toJson() const;
    static Result<StudioSubmission> fromJson( const QJsonObject &json );
};

/// Canonical digest over a spec document. Sorted keys make it independent of
/// QJsonObject insertion order; the digest is identity evidence, not a
/// cryptographic signature.
QString studioSpecDigest( const QJsonObject &specJson );

/// A report belongs to this submission only when its `spec` echo digests
/// identically AND its study/experiment ids agree. @p why is filled with a
/// stable dotted refusal code on mismatch.
bool studioSubmissionMatchesReport( const StudioSubmission &submission,
                                    const QJsonObject &studyReportJson,
                                    QString *why = nullptr );

/// A delivered result is stale when no active submission matches its id,
/// or when the live store changed while it was running.
bool studioSubmissionIsStale( quint64 deliveredId,
                              const std::optional<StudioSubmission> &active,
                              const QString &currentStorePath,
                              QString *why = nullptr );

} // namespace sicnu::experiment_studio
