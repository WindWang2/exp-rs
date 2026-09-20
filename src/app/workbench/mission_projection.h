/***************************************************************************
 * mission_projection.h — ONE projection for GUI, MCP and Pi
 *
 * Surface parity is a *construction* property here, not a test-time hope:
 * the desktop model, the MCP `mission:*` tools and the Pi bridge all render
 * `missionTaskProjectionJson()` / `missionTimelineProjectionJson()`. They
 * therefore cannot drift, and tests/test_mission_surface_parity.cpp only has
 * to lock the identity layer (unique ids, no phantom entries, no Pi-name
 * collisions) plus assert byte-equality of the two call paths.
 *
 * Qt Core only — see mission_stage.h for the dependency policy.
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_stage.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::app
{

// ---------------------------------------------------------------------------
// Shared projections (GUI == MCP == Pi, by construction)
// ---------------------------------------------------------------------------

/// Canonical task projection. Field order is fixed so the compact JSON is
/// byte-identical on every surface.
QJsonObject missionTaskProjectionJson( const MissionTask &task );

/// Bounded timeline projection: stage summary, tasks (capped at @p maxItems)
/// and, when @p sinceSeq > 0, only the events after that cursor — the exact
/// payload an incremental UI update consumes.
QJsonObject missionTimelineProjectionJson( const MissionTimeline &timeline,
                                           int maxItems = 32,
                                           quint64 sinceSeq = 0 );

QJsonObject missionReconciliationProjectionJson( const MissionReconciliation &rec );

/// Canonical compact serialization helper (deterministic: no locale, no
/// whitespace, insertion order == declaration order).
QByteArray missionCanonicalJson( const QJsonObject &obj );

// ---------------------------------------------------------------------------
// Surface identity registry (parity gate inputs)
// ---------------------------------------------------------------------------

enum class MissionSurface
{
    AgentTool,    ///< `mission:<verb>` — MCP / Agent tool id
    AppCommand,   ///< `mission.<noun>.<verb>` — CommandRegistry id
    ArtifactKind  ///< persisted artifact kind emitted by the mission store
};

const char *missionSurfaceKey( MissionSurface surface );

struct MissionSurfaceEntry
{
    QString id;
    MissionSurface surface = MissionSurface::AgentTool;
    QString description;

    bool operator==( const MissionSurfaceEntry & ) const = default;
};

/// The single authoritative list. Appending here is the only way to add a
/// mission capability; the parity gate then enforces uniqueness and the
/// absence of phantoms automatically.
const QVector<MissionSurfaceEntry> &missionSurfaceRegistry();

QStringList missionSurfaceIds( const QVector<MissionSurfaceEntry> &entries );
QStringList missionSurfaceIds( MissionSurface surface );

/// Ids advertised more than once (across and inside surfaces).
QStringList missionDuplicateSurfaceIds( const QVector<MissionSurfaceEntry> &entries );
QStringList missionDuplicateSurfaceIds();

/// Ids advertised without a description — an agent would see a name it cannot
/// explain, i.e. a phantom capability.
QStringList missionPhantomSurfaceIds( const QVector<MissionSurfaceEntry> &entries );
QStringList missionPhantomSurfaceIds();

/// Pi's documented mapping rule (pi/mcp_bridge.ts: `exprs_` + non
/// [A-Za-z0-9_-] replaced by `_`).
QString missionPiToolName( const QString &mcpToolId );

/// Pi names for every AgentTool entry, in registry order.
QStringList missionPiToolNames();
QStringList missionDuplicatePiToolNames();

} // namespace sicnu::app
