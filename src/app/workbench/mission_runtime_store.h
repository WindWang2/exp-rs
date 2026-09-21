/***************************************************************************
 * mission_runtime_store.h — single-authority load/save of the mission runtime
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * One authority, one write path, one read path:
 *
 *   authority  = the MissionContext document (sidecar `<stem>.mission.json`
 *                + `sicnuMissionContext` project-XML block, both written by
 *                the D18 store with QSaveFile atomicity), with the timeline
 *                embedded under metadata["mission_timeline"].
 *   legacy     = the Workbench 12.0 `<stem>.mission-timeline.json` sidecar.
 *                IMPORT-ONLY: read when the authority has no embedded
 *                timeline (migration), never written, never allowed to
 *                override a present authority document.
 *   last-good  = `<stem>.mission.json.last-good`, snapshotted from the
 *                authority sidecar right AFTER every successful commit, so a
 *                later corruption costs nothing. A sidecar that no longer
 *                decodes is never snapshotted (the older good copy wins); a
 *                save that only reached the XML channel skips rotation.
 *
 * Fail-closed rules:
 *   - unknown/future embedded timeline schema_version → load refused;
 *   - corrupt authority with no last-good recovery → load refused, and the
 *     poisoned state can never be saved over the artifact;
 *   - a failed save never truncates (QSaveFile) and never touches legacy.
 *
 * Qt Core + Qt Xml only (QSaveFile / QDomDocument / QJsonDocument).
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_context.h"
#include "app/workbench/mission_context_store.h"
#include "app/workbench/mission_stage.h"
#include "app/workbench/mission_timeline_bridge.h"
#include "app/workbench/mission_timeline_store.h"

#include <QDomDocument>
#include <QString>
#include <QStringList>

namespace sicnu::app
{

/// Legacy 12.0 timeline sidecar path (import-only channel).
QString missionTimelineLegacySidecarPathForProject( const QString &projectFilePath );

/// Last-known-good snapshot path of the authority sidecar.
QString missionRuntimeLastGoodPathForProject( const QString &projectFilePath );

/// Loaded mission runtime. Pure value type; the caller owns mutation.
struct MissionRuntimeState
{
    MissionContext context;
    MissionTimeline timeline;

    bool authorityLoaded = false;       ///< an authority document existed and decoded
    bool timelineMigrated = false;      ///< timeline adopted from the legacy 12.0 sidecar
    bool recoveredFromLastGood = false; ///< authority recovered from the last-good snapshot
    bool authorityCorrupt = false;      ///< authority existed but decoded nowhere (poisoned)
    QStringList notices;                ///< machine codes (migration, recovery)
    QStringList problems;               ///< machine codes for refused/degraded channels
};

/// Single-authority load. Never throws; never partially applies. A missing
/// authority (fresh project) is success with authorityLoaded == false.
bool loadMissionRuntime( const QString &projectFilePath, const QDomDocument &projectDocument,
                         MissionRuntimeState &out, QString *error = nullptr );

/// Single-authority save. Embeds the timeline, rotates the last-good
/// snapshot, then persists through the D18 dual write. Refuses to publish a
/// poisoned state (authorityCorrupt) and never writes the legacy sidecar.
bool saveMissionRuntime( const QString &projectFilePath, QDomDocument &projectDocument,
                         MissionRuntimeState &state, QString *error = nullptr );

} // namespace sicnu::app
