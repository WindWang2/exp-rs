/***************************************************************************
 * object_identity.h — Workbench 10.0 unified object identity
 *
 * One typed reference shape for every object the workbench can select:
 * Data assets, map layers, governed results, datasets, experiment runs,
 * models and workflow runs. The ref does NOT mint a new id space — `id` is
 * the authoritative id of the owning store (AssetId, QgsMapLayer::id(),
 * governance entity id, DatasetStore id, ExperimentStore run id,
 * ModelCatalog name, WorkflowRunCoordinator run id) and every consumer
 * re-queries the owner instead of caching the ref across event-loop turns.
 *
 * Also hosts the shared provenance-target resolution that used to live
 * inline in ProvenanceSection: result id → governed entity → catalog asset,
 * layer source path → catalog asset. One resolver, three consumers
 * (provenance inspector, agent context projection, linked brushing).
 ***************************************************************************/
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <json/json.h>

class QgsMapLayer;

namespace sicnu::data
{
class DataManager;
class AssetId;
} // namespace sicnu::data

namespace sicnu::workspace
{
class WorkspaceService;
} // namespace sicnu::workspace

namespace sicnu::app
{

struct SelectionContextSnapshot;

/// Kind of a selectable workbench object. Token strings are the stable wire
/// form used by the agent context projection (`workbench:context`).
enum class ObjectKind
{
    None,
    Layer,
    Asset,
    Result,
    Dataset,
    ExperimentRun,
    Model,
    WorkflowRun,
};

/// Wire token for a kind ("layer", "asset", …); "none" for ObjectKind::None.
QString objectKindToken( ObjectKind kind );

struct WorkbenchObjectRef
{
    ObjectKind kind = ObjectKind::None;
    QString id;          ///< authoritative id in the owning store (empty = null)
    QString displayName; ///< best-effort projection (may be empty)

    bool isNull() const { return kind == ObjectKind::None || id.isEmpty(); }
    bool operator==( const WorkbenchObjectRef & ) const = default;
};

namespace ContextRules
{

/// Deterministic primary-object rule. Catalog/domain selections outrank the
/// ambient map-layer selection; among them the rarest, most deliberate
/// surfaces win. Priority (first non-empty):
///   WorkflowRun > ExperimentRun > Dataset > Model > Result > Asset > Layer.
/// Layer identity is the active layer's QgsMapLayer::id(); when no layer is
/// active the first selected layer is used. Returns a null ref when nothing
/// anywhere is selected.
WorkbenchObjectRef primaryObject( const SelectionContextSnapshot &snapshot );

bool experimentSelected( const SelectionContextSnapshot &snapshot );
bool datasetSelected( const SelectionContextSnapshot &snapshot );
bool modelSelected( const SelectionContextSnapshot &snapshot );
bool workflowRunSelected( const SelectionContextSnapshot &snapshot );

/// The layer (if any) a Layer-kind primary object resolves to. Returns null
/// when the primary selection is not a layer or the id does not match any
/// selected/active layer any more (the ref is a projection, never a handle).
QgsMapLayer *resolvePrimaryLayer( const SelectionContextSnapshot &snapshot );

} // namespace ContextRules

// ---------------------------------------------------------------------------
// Shared provenance-target resolution (single resolver, see file header)
// ---------------------------------------------------------------------------

/// Hard cap mirroring the provenance inspector's bounded multi-selection
/// summary. Exposed so callers can render the same truthful truncation.
inline constexpr int kObjectLinkMaxTargets = 4;

/// Resolves a selection snapshot into catalog asset ids, reproducing the
/// provenance inspector's documented cascade (first non-empty source wins):
///   1. selected asset ids (taken as-is),
///   2. selected governance entity ids → GovernedAsset → asset id / source
///      path → catalog asset,
///   3. selected/active map layers → source path → catalog asset.
/// Duplicates are dropped; the result is bounded by kObjectLinkMaxTargets.
QVector<sicnu::data::AssetId> resolveSelectionAssetTargets(
    const SelectionContextSnapshot &snapshot,
    sicnu::data::DataManager *dataManager,
    sicnu::workspace::WorkspaceService *workspace );

// ---------------------------------------------------------------------------
// Agent context projection (read-only seam for the Pi/MCP surface)
// ---------------------------------------------------------------------------

/// Projects a snapshot into the `workbench:context` tool payload:
/// active workbench, primary object (typed + wire token), every selection
/// list, ContextFacts and the unavailability-free command ids the agent may
/// run. Pure — unit-testable without widgets. Never includes paths outside
/// what the snapshot already carries (the MCP workspace policy still applies
/// to any tool the agent runs afterwards).
Json::Value workbenchContextToJson( const SelectionContextSnapshot &snapshot,
                                    const QStringList &availableCommandIds );

} // namespace sicnu::app
