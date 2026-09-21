/***************************************************************************
 * mission_timeline_bridge.h — MissionContext × MissionTimeline single truth
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * Workbench 12.0 shipped the task-space value model and a sidecar store for
 * it, but the timeline had no channel inside the MissionContext document —
 * the comment in mission_timeline_store.h promised a
 * "mission_timeline_bridge" that was never written, so the task space could
 * only live in a second sidecar next to the mission context sidecar and the
 * project XML: two writable transports, no precedence rule, and no way for a
 * .qgz round-trip to carry the task space.
 *
 * This module is that bridge, and it is deliberately tiny: the timeline
 * document (kind "mission_timeline", schema_version "1.0") is embedded
 * verbatim under one metadata key of the MissionContext value model, so the
 * existing D18 dual-write (sidecar + sicnuMissionContext XML, both QSaveFile
 * atomic) becomes the ONE authority. Extraction reuses MissionTimeline's
 * own fail-closed decoder, so a tampered or future-version payload can never
 * be partially applied.
 *
 * Dependency policy: Qt Core only — see mission_stage.h.
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_context.h"
#include "app/workbench/mission_stage.h"

#include <QJsonObject>
#include <QString>

namespace sicnu::app
{

/// Metadata key under MissionContext::metadata carrying the embedded timeline
/// document. The value is a complete MissionTimeline JSON object.
inline constexpr const char *kMissionTimelineMetadataKey = "mission_timeline";

/// Metadata key recording where an embedded timeline was migrated from
/// (e.g. "legacy-sidecar-12.0"). Migration audit, not a second copy.
inline constexpr const char *kMissionTimelineMigrationKey = "mission_timeline_migrated_from";

/// Embed @p timeline into @p ctx. The embedded mission_id follows the context
/// (the context is the authority); a context without an id keeps the
/// timeline's own. Overwrites any previous embedding — there is exactly one
/// embedded copy by construction.
void embedMissionTimeline( MissionContext &ctx, const MissionTimeline &timeline );

/// True when the context carries an embedded timeline document.
bool missionContextHasTimeline( const MissionContext &ctx );

/// Fail-closed extraction. `missing` means "no embedded timeline" (the
/// legitimate legacy/fresh case); every other code means the authority
/// document itself is unusable and the caller must refuse to open.
bool extractMissionTimeline( const MissionContext &ctx, MissionTimeline &out,
                             QString *error = nullptr );

/// Machine code of the last extraction failure (for diagnostics).
QString missionTimelineMigrationSource( const MissionContext &ctx );
void setMissionTimelineMigrationSource( MissionContext &ctx, const QString &source );

} // namespace sicnu::app
