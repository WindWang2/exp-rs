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
    /// refused document imports nothing (R18-E T202).
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
    StudioResourcePolicy policy;
    QJsonObject designerDraft;
    QJsonObject lastStudyReport;

    QJsonObject toJson() const;
    static StudioSessionState fromJson( const QJsonObject &json );
};

} // namespace sicnu::experiment_studio
