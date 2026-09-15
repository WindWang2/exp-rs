/***************************************************************************
 * mission_context.h — D18 Mission / Scientific Workspace Context
 *
 * Project-level scientific session state as typed references into existing
 * authorities (DataManager, Qgs layers, DatasetStore, ExperimentStore,
 * WorkflowRunCoordinator / PipelineRunCoordinator, ModelCatalog, …).
 *
 * Rules (GOAL §5 / ADR follow-up D-M1):
 *   - Value type: copyable, no QObject, no QgsMapLayer*, no QPointer.
 *   - Identity reuse: WorkbenchObjectRef / ObjectKind (Workbench 10).
 *   - Serializable for save / restore / checkpoint / Agent grounding.
 *   - Live resolution always re-queries owners by id; never cache pointers
 *     across event-loop turns.
 ***************************************************************************/
#pragma once

#include "app/workbench/object_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::app
{

inline constexpr const char *kMissionContextKind = "mission_context";
inline constexpr const char *kMissionContextSchemaVersion = "1.0";

/// Spatial AOI without geometry engine handles — WKT + CRS + optional asset.
struct SpatialContext
{
    QString aoiWkt;
    QString crs;           ///< e.g. "EPSG:4326"
    QString aoiAssetId;    ///< optional catalog asset carrying the AOI
    double xmin = 0.0;
    double ymin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    bool hasExtent = false;

    bool operator==( const SpatialContext & ) const = default;
};

/// Temporal window / active acquisition — ids and ISO-8601 only.
struct TemporalContext
{
    QString startIso;
    QString endIso;
    QString collectionId;
    QString activeAcquisitionId;
    QString activeResultId; ///< phenology / change result id when selected

    bool operator==( const TemporalContext & ) const = default;
};

/// Map view projection (no canvas pointer).
struct MapViewContext
{
    QString crs;
    double xmin = 0.0;
    double ymin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    double scale = 0.0;
    bool hasExtent = false;

    bool operator==( const MapViewContext & ) const = default;
};

/// Selection projection stored by id (mirrors SelectionContextSnapshot lists).
struct MissionSelection
{
    QString workbenchId;
    WorkbenchObjectRef primary;
    QStringList layerIds;
    QStringList assetIds;
    QStringList resultIds;
    QStringList datasetIds;
    QStringList experimentIds;
    QStringList modelIds;
    QStringList workflowRunIds;

    bool operator==( const MissionSelection & ) const = default;
};

/// Active visual / compiler workflow document handle (not the AST body).
struct ActiveWorkflowRef
{
    QString workflowId;
    QString name;
    QString schemaVersion; ///< "1.0" (IR1) | "2.0" (IR2) | "engine2" | empty
    QString fingerprint;   ///< content hash when known
    QString runner; ///< "workflow_run_coordinator" | "pipeline_run_coordinator" | ""

    bool isNull() const { return workflowId.isEmpty(); }
    bool operator==( const ActiveWorkflowRef & ) const = default;
};

struct MissionContext
{
    QString missionId;
    QString missionName;
    QString projectRef; ///< project file path or stable project id string
    QString notes;

    SpatialContext spatial;
    TemporalContext temporal;
    MapViewContext map;
    MissionSelection selection;
    ActiveWorkflowRef activeWorkflow;

    QVector<WorkbenchObjectRef> datasets;
    QVector<WorkbenchObjectRef> assets;
    QVector<WorkbenchObjectRef> layers;
    QVector<WorkbenchObjectRef> results;
    QVector<WorkbenchObjectRef> models;
    QVector<WorkbenchObjectRef> workflowRuns;
    QVector<WorkbenchObjectRef> experiments;
    QVector<WorkbenchObjectRef> artifacts;
    QVector<WorkbenchObjectRef> cartographyProducts;
    QVector<WorkbenchObjectRef> agentDecisions;

    QJsonObject metadata;

    bool isNull() const { return missionId.isEmpty() && projectRef.isEmpty(); }

    bool operator==( const MissionContext & ) const = default;
};

// ---------------------------------------------------------------------------
// Serialization (QJson — no live pointers ever written)
// ---------------------------------------------------------------------------

QJsonObject workbenchObjectRefToJson( const WorkbenchObjectRef &ref );
WorkbenchObjectRef workbenchObjectRefFromJson( const QJsonObject &obj );

QJsonObject missionContextToJson( const MissionContext &ctx );
/// Fail-closed: wrong kind/version or non-object → nullopt semantics via bool.
bool missionContextFromJson( const QJsonObject &doc, MissionContext &out, QString *error = nullptr );

/// SHA-256 hex (64 chars) over canonical compact JSON of the mission
/// (mission_id / timestamps in metadata are excluded from the hash input so
/// identical scientific content hashes equal). Empty mission → empty string.
QString missionContentFingerprint( const MissionContext &ctx );

/// Bounded Agent / UI summary (GOAL §9) — caps list lengths.
QJsonObject missionSummaryJson( const MissionContext &ctx, int maxListItems = 32 );

// ---------------------------------------------------------------------------
// Builders (pure)
// ---------------------------------------------------------------------------

/// Merge selection snapshot lists into a mission (does not touch spatial/map
/// unless already set). Primary object uses ContextRules::primaryObject.
MissionContext missionContextFromSelection( const SelectionContextSnapshot &snapshot,
                                            MissionContext base = {} );

/// Ensure missionId is non-empty (generates a UUID string when missing).
void ensureMissionId( MissionContext &ctx );

/// Upsert a typed ref into the matching mission list (by kind+id). Updates
/// displayName when the id already exists. Does nothing for null refs.
void publishMissionObject( MissionContext &ctx, const WorkbenchObjectRef &ref );

/// Convenience: publish a Result whose id is a stable hash of @p path (or an
/// explicit @p resultId). Path is recorded in metadata["artifact_paths"][id].
WorkbenchObjectRef publishMissionResultFromPath( MissionContext &ctx,
                                                 const QString &path,
                                                 const QString &displayName = {},
                                                 const QString &resultId = {} );

/// Convenience: publish a Layer ref (typically QgsMapLayer::id()).
void publishMissionLayer( MissionContext &ctx, const WorkbenchObjectRef &layerRef );

/// Replace active workflow handle (IR 2.0 / Engine) — shared Agent↔UI identity.
void setMissionActiveWorkflow( MissionContext &ctx, const ActiveWorkflowRef &workflow );

} // namespace sicnu::app
