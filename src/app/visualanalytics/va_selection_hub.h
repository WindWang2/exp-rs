/***************************************************************************
 * va_selection_hub.h — Linked Visual Analytics 11.0 selection authority
 *
 * The process-level bus the 10.0 platform anticipated (va_chart_widget.h
 * named a "VaSelectionHub" that never existed): every linked-visual
 * surface — map views, charts, brushing filters — publishes typed
 * selection events HERE and subscribes to everyone else's. The hub owns
 * the event identity contract:
 *
 *   * subjects are typed (view point/region, layer, feature, pixel,
 *     chart point/category/range) and carry the OWNING stores'
 *     authoritative ids (DisplayViewId / DisplayLayerId / AssetId /
 *     QgsFeatureId / payload index) — the hub mints no new id space;
 *   * every event is stamped with its ORIGIN surface token and a
 *     hub-assigned monotonic GENERATION, so consumers can tell echoes
 *     from fresh intent and async workers can drop stale generations;
 *   * loop suppression: a publish re-entering during a dispatch with the
 *     SAME ORIGIN as the dispatch on the stack is an echo of that dispatch
 *     — it is counted and dropped, never re-broadcast (Oracle 1: no
 *     feedback loops). Cross-surface republication is new intent and
 *     passes; nested cross-origin dispatches restore the outer context on
 *     return, so A↔B relay pairs cannot recurse unboundedly.
 *
 * The hub is a GUI-thread object (like SelectionContext). History is a
 * bounded ring (diagnostics only — consumers must never treat it as
 * state authority). Subjects are validated and clamped on publish:
 * non-finite coordinates and over-long free text are rejected so a
 * hostile producer cannot poison subscribers.
 ***************************************************************************/
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace sicnu::app::va
{

/// What kind of object the event selects. Wire-stable; tests pin the
/// numeric order (append-only — never renumber).
enum class VaSelectionKind
{
    ViewPoint,     ///< a map point on a view (x0,y0 in crs WKT authority)
    ViewRegion,    ///< a map rectangle (x0,y0,x1,y1 in crs WKT authority)
    Layer,         ///< a display layer (layerId + cross-view assetId)
    Feature,       ///< a vector feature (layerId + featureId)
    Pixel,         ///< a raster pixel (layerId + row/column)
    ChartPoint,    ///< a scatter/series point (chartId + payload index)
    ChartCategory, ///< a bar/box/matrix-row category (chartId + index)
    ChartRange,    ///< a brush x-range (chartId + x0..x1 in data coords)
};

/// Typed identity of one selected thing. Ids are the owning stores'
/// authoritative tokens; coordinate pairs carry their CRS WKT authority
/// (empty = chart data coordinates, which have no CRS). All string
/// fields are bounded by the hub on publish.
struct VaSelectionSubject
{
    VaSelectionKind kind = VaSelectionKind::ViewPoint;

    QString viewId;   ///< DisplayViewId token ("" = not view-scoped)
    QString layerId;  ///< DisplayLayerId token ("" = none)
    QString assetId;  ///< catalog AssetId token — the CROSS-VIEW identity
    qint64 featureId = -1; ///< QgsFeatureId (-1 = none)
    qint64 index = -1;     ///< chart payload index (-1 = none)
    qint64 row = -1;       ///< raster pixel row (-1 = none)
    qint64 column = -1;    ///< raster pixel column (-1 = none)

    /// Point: (x0,y0). Range/Region: (x0,y0)-(x1,y1). Data coords for
    /// chart subjects, CRS coords for view subjects.
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    QString crsWkt;  ///< coordinate authority for view subjects ("" = data)
    QString chartId; ///< stable emitting-chart token ("" = not a chart)

    bool operator==( const VaSelectionSubject & ) const = default;
};

/// One published selection. `generation` is 0 only for default-constructed
/// values — the hub never issues 0.
struct VaSelectionEvent
{
    VaSelectionSubject subject;
    QString origin;        ///< publishing surface token (e.g. "map.main")
    quint64 generation = 0;

    bool operator==( const VaSelectionEvent & ) const = default;
};

class VaSelectionHub : public QObject
{
    Q_OBJECT
  public:
    /// Diagnostics history capacity (bounded; oldest evicted).
    static constexpr int kHistoryCapacity = 64;
    /// Clamp for every free-text field of a subject.
    static constexpr int kMaxTextChars = 256;

    struct Stats
    {
        quint64 published = 0;        ///< accepted + broadcast
        quint64 suppressedEchoes = 0; ///< loop-suppressed re-entrant echoes
        quint64 rejected = 0;         ///< invalid subjects (non-finite etc.)
    };

    explicit VaSelectionHub( QObject *parent = nullptr );

    /// Validates @p subject, stamps (origin, generation) and broadcasts.
    /// Returns the assigned generation; 0 means the subject was rejected.
    /// Re-entrant calls from a subscriber slot with the SAME origin and
    /// the generation being dispatched are echoes: dropped, counted in
    /// Stats::suppressedEchoes, and 0 is returned.
    quint64 publish( const VaSelectionSubject &subject, const QString &origin );

    /// Bounded copy of the most recent events (newest first), for
    /// diagnostics/UI. Never authoritative state.
    QVector<VaSelectionEvent> history() const { return m_history; }

    Stats stats() const { return m_stats; }
    void resetStats() { m_stats = Stats{}; }

    /// True while a publish dispatch is on the stack (test/lifecycle hook).
    bool isDispatching() const { return m_dispatchDepth > 0; }

  signals:
    void selectionPublished( const sicnu::app::va::VaSelectionEvent &event );

  private:
    static VaSelectionSubject clampSubject( VaSelectionSubject subject );
    static bool isValidSubject( const VaSelectionSubject &subject );

    quint64 m_generation = 0;
    int m_dispatchDepth = 0;
    QString m_dispatchOrigin;
    quint64 m_dispatchGeneration = 0;
    QVector<VaSelectionEvent> m_history; ///< newest first, ≤ kHistoryCapacity
    Stats m_stats;
};

} // namespace sicnu::app::va

Q_DECLARE_METATYPE( sicnu::app::va::VaSelectionSubject )
Q_DECLARE_METATYPE( sicnu::app::va::VaSelectionEvent )
