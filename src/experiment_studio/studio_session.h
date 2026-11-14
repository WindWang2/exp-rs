// studio_session.h — lightweight session state for Studio UI / future Agent.
#pragma once

#include "experiment_studio/studio_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::experiment_studio
{

struct StudioSessionState
{
    QString schema = QString::fromUtf8( kStudioSessionSchema );
    /// True only when fromJson accepted the document's schema marker; a
    /// refused document imports nothing (R18-E T202). R30-T07: also false
    /// when a numeric policy field failed its typed/value contract.
    bool ok = false;
    QString studyId;
    QString experimentId;
    QString algorithmId;
    QString activeTab; ///< designer|matrix|spatial|sensitivity|fault|divergence|export
    QStringList selectedPointIds;
    QString referenceRunId;
    QString studentRunId;
    QString faultScenarioId;
    QString lastExportPath;
    /// Optional (R29-T04): the experiment store the last live session had
    /// open — a NAVIGATION ref only. Restoring never re-opens it (opening a
    /// store is an explicit user act), the summary just points back at it.
    /// Absent in pre-R29 documents; readers default it empty.
    QString liveStorePath;
    /// R30-T07 (optional, additive): the store identity the restored
    /// report / designer refs were produced against. A ref from store A is
    /// never silently attached to store B. Absent in legacy documents.
    QString reportStorePath;
    QString designerStorePath;
    /// R30-T07 (optional, additive): identity of the LAST SUBMITTED design.
    /// The export bundle must show the design that was actually run, not a
    /// newer unsubmitted draft. Empty in legacy documents.
    QJsonObject submittedDesignerVm;
    QString submittedSpecDigest;
    QString submittedStorePath;
    /// Full StudioSubmission::toJson() of the last submitted design. Optional;
    /// lets an export after a restart still carry the exact design identity.
    QJsonObject submissionJson;
    StudioResourcePolicy policy;
    QJsonObject designerDraft;
    QJsonObject lastStudyReport;
    /// Transient decode diagnostics (NOT serialized). A non-empty list means
    /// at least one field was refused; readers must fail closed.
    QStringList issues;

    QJsonObject toJson() const;
    static StudioSessionState fromJson( const QJsonObject &json );
};

} // namespace sicnu::experiment_studio
