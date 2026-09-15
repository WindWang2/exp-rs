// rs_edit_session.h — F11 Package A: aggregated vector-editing authority.
//
// One session tracks every QgsVectorLayer under edit: per-layer edit state
// (editing / modified / undo-redo depth / feature & selection counts), an
// explicit lock, undo/redo delegation, and commit/rollback that REPORTS
// failures instead of discarding commitChanges()'s return value (the
// legacy main_window_vector.cpp bug class).
//
// Authority boundaries:
//   * The session owns STATE and LIFECYCLE, never geometry truth. The
//     QgsVectorLayer edit buffer stays the only geometry authority.
//   * Layers are held via QPointer and de-tracked on
//     QgsProject::layerWillBeRemoved — no dangling entries, no callbacks
//     into removed layers.
//   * A locked layer refuses new edit commands and undo/redo through the
//     session; commit/rollback stay available (never risk data loss by
//     blocking save paths).
//   * detachLayer() rolls back uncommitted changes by default so the layer
//     ends in a consistent non-editing state; project removal cannot be
//     blocked, so removal always just drops tracking.
//
// Not thread-safe by design: construct and use on the GUI thread only
// (QGIS layers are GUI-thread objects).
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QgsProject;
class QgsVectorLayer;

/// Immutable snapshot of one attached layer's edit state. featureCount is
/// maintained incrementally (attach counts once; featureAdded/Deleted adjust)
/// so state refresh on a 100k-feature layer is O(1), never a full recount.
struct RsLayerEditState
{
    QString layerId;
    bool editing = false;      // layer is in edit mode
    bool modified = false;     // uncommitted changes exist
    bool locked = false;       // session-level edit lock
    int undoDepth = 0;         // undoable commands on the layer stack
    int redoDepth = 0;
    qlonglong featureCount = -1; // -1 = unknown (non-spatial / not counted yet)
    int selectedCount = 0;

    bool operator==( const RsLayerEditState &other ) const
    {
        return editing == other.editing && modified == other.modified
               && locked == other.locked && undoDepth == other.undoDepth
               && redoDepth == other.redoDepth && featureCount == other.featureCount
               && selectedCount == other.selectedCount;
    }
    bool operator!=( const RsLayerEditState &other ) const { return !( *this == other ); }
};

class RsEditSession : public QObject
{
    Q_OBJECT

  public:
    explicit RsEditSession( QObject *parent = nullptr );
    ~RsEditSession() override;

    /// Attach a layer. With \a startEditing the session calls
    /// startEditing() and FAILS CLOSED (returns an error) when the layer
    /// cannot enter edit mode (invalid layer, read-only provider, already
    /// tracked). Attaching a layer that is ALREADY editing adopts its state
    /// instead of failing. Empty error string on success.
    QString attachLayer( QgsVectorLayer *layer, bool startEditing = true );

    /// Detach. When the layer is dirty and \a rollbackDirty, uncommitted
    /// changes are rolled back and editing stopped first. Returns an error
    /// string (empty on success; unknown layer is not an error).
    QString detachLayer( const QString &layerId, bool rollbackDirty = true );

    QStringList attachedLayerIds() const;
    bool isAttached( const QString &layerId ) const;
    QgsVectorLayer *layer( const QString &layerId ) const;
    RsLayerEditState state( const QString &layerId ) const;

    /// True when ANY attached layer carries uncommitted changes.
    bool isDirty() const;

    /// Session-level lock: while locked, edit command guards and undo/redo
    /// through the session are refused for the layer. Commit/rollback are
    /// deliberately never locked (save paths must stay available).
    bool setLocked( const QString &layerId, bool locked );
    bool isLocked( const QString &layerId ) const;

    /// Undo/redo delegation onto the layer's own undo stack. Returns false
    /// (without side effects) when the layer is unknown, locked, or has
    /// nothing on that side of the stack.
    bool undo( const QString &layerId );
    bool redo( const QString &layerId );
    /// Undo/redo every attached layer that can; returns how many layers moved.
    int undoAll();
    int redoAll();

    /// Commit one layer. On failure \a error receives the provider/layer
    /// commit errors (a layer that rejects the commit without naming a
    /// reason yields an explicit fallback message — never a silent false).
    bool commit( const QString &layerId, QString *error = nullptr );
    /// Commit every dirty attached layer; per-layer failures are appended to
    /// \a errors as "layerId: error". Returns the number of committed layers.
    int commitAll( QStringList *errors = nullptr );

    /// Discard uncommitted changes of one layer (stops editing).
    bool rollback( const QString &layerId, QString *error = nullptr );
    /// Roll back every attached layer; returns how many were rolled back.
    int rollbackAll();

  signals:
    /// Emitted whenever any tracked fact changed (coalesced per event).
    void stateChanged();
    void layerStateChanged( const QString &layerId );
    /// After a commit attempt: ok=false carries the collected error.
    void commitFinished( const QString &layerId, bool ok, const QString &error );

  private:
    struct Entry
    {
        QPointer<QgsVectorLayer> layer;
        bool locked = false;
        // Connections specific to this layer, disconnected on detach.
        QList<QMetaObject::Connection> connections;
    };

    void connectLayer( const QString &layerId, QgsVectorLayer *layer );
    void refresh( const QString &layerId );
    void dropLayer( const QString &layerId );
    void handleProjectLayerRemoved( const QString &layerId );

    QHash<QString, Entry> mEntries;
    QHash<QString, RsLayerEditState> mStates;
};
