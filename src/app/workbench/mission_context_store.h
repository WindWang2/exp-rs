/***************************************************************************
 * mission_context_store.h — D18 sidecar persistence for MissionContext
 *
 * Writes/reads `<projectStem>.mission.json` beside the project file.
 * Fail-closed on I/O and schema errors. Does not touch QgsProject XML yet
 * (DataProjectSerializer integration is a follow-up).
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_context.h"

namespace sicnu::app
{

/// Derive the sidecar path for a project file (".qgz"/".qgs" → ".mission.json").
QString missionSidecarPathForProject( const QString &projectFilePath );

/// Atomic-ish write: temp file in same directory then rename.
bool saveMissionContextToSidecar( const QString &projectFilePath, const MissionContext &ctx,
                                  QString *error = nullptr );

bool loadMissionContextFromSidecar( const QString &projectFilePath, MissionContext &out,
                                    QString *error = nullptr );

} // namespace sicnu::app
