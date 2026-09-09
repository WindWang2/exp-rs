/***************************************************************************
 * selection_context.h — Professional Workbench 5.0 unified selection context
 *
 * One projection of "what is the user operating on right now", aggregated
 * from the authoritative sources (QGIS layer tree + canvas current layer,
 * Data Manager catalog selection, governance Results selection, active
 * workbench). The context never copies business state: every field is a
 * re-queryable projection; availability rules live in ContextRules as pure
 * functions so command enablement is testable without widgets.
 *
 * Change notification is coalesced on a short debounce so bursty updates
 * (layer tree rebuilds, catalog refresh storms) cost one refresh, not N.
 ***************************************************************************/
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;
class QgsMapCanvas;
class QgsLayerTreeView;
class QgsMapLayer;
class QgsVectorLayer;
class QgsRasterLayer;

namespace sicnu::app
{

class WorkbenchHost;

/// Immutable projection value. Pointers are borrowed from the authoritative
/// owners (QGIS project / Data Manager); consumers must not keep them across
/// event-loop turns — re-query the context instead.
struct SelectionContextSnapshot
{
    QString workbenchId;                 ///< active workbench id (empty = none)
    QgsMapLayer *activeLayer = nullptr;  ///< canvas current layer (authoritative)
    QList<QgsMapLayer *> selectedLayers; ///< layer-tree selection (contains active)
    bool hasRaster = false;
    bool hasVector = false;
    bool hasRemoteMap = false;
    /// SAR-specific tooling hint. Conservative heuristic (product tokens in the
    /// source path / layer name) unless setSarPredicate() overrides it.
    bool hasSar = false;
    bool hasTemporal = false;
    bool hasBroken = false;              ///< any selected layer invalid/missing
    bool activeEditable = false;         ///< vector edit session open
    bool activeModified = false;
    QStringList selectedAssetIds;        ///< Data Manager catalog selection
    QStringList selectedResultIds;       ///< governance Results selection
    int layerCount = 0;                  ///< total layers on the active view

    bool hasLayerSelection() const { return activeLayer || !selectedLayers.isEmpty(); }
    bool hasGovernanceSelection() const
    {
        return !selectedAssetIds.isEmpty() || !selectedResultIds.isEmpty();
    }
    /// The first selected vector layer, for edit-oriented commands.
    QgsVectorLayer *firstVectorLayer() const;
    /// The first selected raster layer, for band/style oriented commands.
    QgsRasterLayer *firstRasterLayer() const;
};

/// Pure availability rules — no widget access, fully unit-testable.
namespace ContextRules
{
/// Structured projection of the facts availability derives from (Milestone E).
/// Help/Hint surfaces consume these instead of re-deriving context state, and
/// unavailabilityReason() is expressed over exactly these facts so a disabled
/// command always has a deterministic explanation.
struct ContextFacts
{
    QString workbenchId;      ///< active workbench (empty = none)
    bool hasLayerSelection = false;
    bool hasRaster = false;
    bool hasVector = false;
    bool hasSar = false;
    bool editable = false;    ///< a selected vector CAN start an edit session
    bool editing = false;     ///< an edit session is open
    bool hasGovernanceResult = false;
    bool hasGovernanceAsset = false;
};
ContextFacts prerequisiteFacts( const SelectionContextSnapshot &s );

/// Any raster (or multi-raster selection) selected → band tools, style,
/// histogram, statistics, processing, classification entry points.
bool rasterSelected( const SelectionContextSnapshot &s );
/// Any vector selected → attributes, geometry tools, vector style.
bool vectorSelected( const SelectionContextSnapshot &s );
/// Vector with an open edit session → save-edits / digitizing commands.
bool editingActive( const SelectionContextSnapshot &s );
/// A vector that *can* start an edit session (toggle-edit availability).
bool editingAvailable( const SelectionContextSnapshot &s );
/// SAR raster selected → SAR-specific tool group.
bool sarSelected( const SelectionContextSnapshot &s );
/// Any map layer selected (generic layer commands: zoom, remove, properties).
bool layerSelected( const SelectionContextSnapshot &s );
/// A governed result row is selected in the workspace browser.
bool resultSelected( const SelectionContextSnapshot &s );
/// A governed asset row is selected in the data manager.
bool assetSelected( const SelectionContextSnapshot &s );
/// Human-readable reason a command is unavailable (palette / tooltips).
QString unavailabilityReason( const SelectionContextSnapshot &s, const QString &commandId );
} // namespace ContextRules

/**
 * Aggregates authoritative selection sources into SelectionContextSnapshot
 * values. Attach the live sources once during shell setup; the context then
 * keeps itself current and re-broadcasts a coalesced `changed` signal.
 *
 * The context owns no business state and never reaches into another panel's
 * widgets: panels expose their own selection signals, this class only
 * subscribes.
 */
class SelectionContext : public QObject
{
    Q_OBJECT
  public:
    explicit SelectionContext( QObject *parent = nullptr );

    void attachCanvas( QgsMapCanvas *canvas );
    void attachLayerTree( QgsLayerTreeView *tree );
    void attachWorkbenchHost( WorkbenchHost *host );

    /// Push-style catalog/governance selection updates (the shell connects
    /// panel signals to these — the context never includes panel types).
    void notifyAssetSelection( const QStringList &assetIds );
    void notifyGovernanceSelection( const QStringList &entityIds );

    /// Override the conservative SAR heuristic (product-token match).
    using SarPredicate = std::function<bool( QgsMapLayer * )>;
    void setSarPredicate( SarPredicate predicate );

    /// Last computed projection (recomputed lazily on first query).
    SelectionContextSnapshot snapshot() const;

    /// Immediate recompute + broadcast (tests / forced refresh).
    void refreshNow();

  signals:
    /// Coalesced (≤150 ms) re-projection of every attached source.
    void changed( const sicnu::app::SelectionContextSnapshot &snapshot );

  private:
    SelectionContextSnapshot computeSnapshot() const;
    void scheduleRefresh();
    /// #778: a layer announced its removal — purge it from every cached
    /// projection immediately and re-broadcast so consumers never observe the
    /// doomed pointer.
    void handleLayerWillBeRemoved( QgsMapLayer *layer );

    QTimer *m_debounce = nullptr;
    QPointer<WorkbenchHost> m_workbenchHost;
    QPointer<QgsMapCanvas> m_canvas;
    QPointer<QgsLayerTreeView> m_layerTree;
    SarPredicate m_sarPredicate;
    mutable SelectionContextSnapshot m_cached;
    /// #778: liveness mirror of the cached layer pointers — a null entry means
    /// the layer object died without (or before) a removal signal, and the
    /// cache must be recomputed before it is handed out again.
    mutable QList<QPointer<QgsMapLayer>> m_cachedLayerGuard;
    /// #778: layers that announced layerWillBeRemoved and must stay invisible
    /// to snapshots even though they may briefly still be reachable from the
    /// canvas / layer tree while they die. The guard tracks the doomed object;
    /// raw is kept so a canvas still reporting the (now destroyed) pointer as
    /// its current layer keeps the entry filtering instead of resurrecting a
    /// dangling pointer.
    struct DyingLayer
    {
        QPointer<QgsMapLayer> guard;
        QgsMapLayer *raw = nullptr;
    };
    QList<DyingLayer> m_dyingLayers;
    mutable bool m_cacheValid = false;
    QStringList m_selectedAssetIds;
    QStringList m_selectedResultIds;
};

} // namespace sicnu::app

Q_DECLARE_METATYPE( sicnu::app::SelectionContextSnapshot )
