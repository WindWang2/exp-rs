/***************************************************************************
 * mission_timeline_store.h — persistence for the mission task space
 *
 * Contract mirrors D18's MissionContext store (`mission_context_store.h`):
 *   - one sidecar artifact per project file (`<stem>.mission-timeline.json`);
 *   - atomic-ish write via QSaveFile (temp file in the same directory, then
 *     commit); a failed write never leaves a truncated file behind;
 *   - fail-closed reads: wrong kind / unsupported version / malformed payload
 *     are reported as errors and never partially applied;
 *   - a missing sidecar is NOT an error (fresh project).
 *
 * The XML channel is intentionally not duplicated here: the timeline is also
 * embedded into `MissionContext::metadata` by mission_timeline_bridge so the
 * existing D18 dual-write carries it inside .qgs/.qgz.
 *
 * Qt Core only (QSaveFile / QJsonDocument); no QGIS, no Widgets.
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_stage.h"

#include <QString>

namespace sicnu::app
{

/// Derive the sidecar path for a project file (".qgz"/".qgs" →
/// ".mission-timeline.json").
QString missionTimelineSidecarPathForProject( const QString &projectFilePath );

/// Atomic write. Returns false (with @p error) on any I/O or commit failure;
/// a failed commit cancels the temp file so no truncated artifact survives.
bool saveMissionTimelineToSidecar( const QString &projectFilePath,
                                   const MissionTimeline &timeline,
                                   QString *error = nullptr );

/// Read back. A missing sidecar returns true with @p loaded == false.
bool loadMissionTimelineFromSidecar( const QString &projectFilePath,
                                     MissionTimeline &out,
                                     bool *loaded = nullptr,
                                     QString *error = nullptr );

} // namespace sicnu::app
