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
    QString studyId;
    QString experimentId;
    QString algorithmId;
    QString activeTab; ///< designer|matrix|spatial|sensitivity|fault|divergence|export
    QStringList selectedPointIds;
    QString referenceRunId;
    QString studentRunId;
    QString faultScenarioId;
    QString lastExportPath;
    StudioResourcePolicy policy;
    QJsonObject designerDraft;
    QJsonObject lastStudyReport;

    QJsonObject toJson() const;
    static StudioSessionState fromJson( const QJsonObject &json );
};

} // namespace sicnu::experiment_studio
