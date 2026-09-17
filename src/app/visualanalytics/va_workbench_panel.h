/***************************************************************************
 * va_workbench_panel.h — Visual Analytics workbench surface
 *
 * Workbench 10.0: three chart hosts fed by three VaDataSource jobs over
 * the SAME selected raster — band histogram, two-band scatter, per-band
 * mean curve. The panel is a thin composition of platform pieces; it owns
 * no store, does no GUI-thread I/O and reports every estimate honestly.
 *
 * Linked Visual Analytics 11.0 adds the linked surface: the panel both
 * publishes to and consumes from the process-level VaSelectionHub
 * (brushing between charts, scatter pick → map marker) and turns map
 * cursor moves into an asynchronous VaCursorProbe readout. All payloads
 * stay bounded; every truncation flag stays truthful.
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include "va_cursor_probe.h"
#include "va_source.h"

#include <QPointer>

#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QgsMapCanvas;
class QgsRasterLayer;
class QgsVertexMarker;

namespace sicnu::app::va
{

class VaChartWidget;
class VaSelectionHub;
struct VaSelectionSubject;
struct VaSelectionEvent;

class VaWorkbenchPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    /// Current raster source path from the unified selection (empty = none).
    using RasterPathProvider = std::function<QString()>;
    /// The map canvas pick markers are drawn on (re-queried; null = none).
    using CanvasProvider = std::function<QgsMapCanvas *()>;
    /// The raster layer the cursor probe samples (re-queried; null = none).
    using RasterLayerProvider = std::function<QgsRasterLayer *()>;

    explicit VaWorkbenchPanel( RasterPathProvider provider,
                               VaSelectionHub *hub = nullptr,
                               CanvasProvider canvasProvider = {},
                               RasterLayerProvider rasterLayerProvider = {},
                               QWidget *parent = nullptr );

  public slots:
    /// Re-runs the three sources for the current raster/bands.
    void refreshCharts();
    /// Linked-visual 11.0: raw cursor geometry from a registered view
    /// (ViewLinkController::cursorMoved) — updates the readout and asks the
    /// async probe for a value.
    void onViewCursorMoved( const QString &viewId, double x, double y,
                            const QString &crsWkt );
    /// The pointer left the view — the readout goes quiet.
    void onViewCursorLeft( const QString &viewId );

  private:
    void buildUi();
    void requestHistogram();
    void requestScatter();
    void requestProfile();
    /// Applies a brush x-range to the scatter payload (client-side over the
    /// bounded snapshot — never a rescan).
    void applyScatterRangeFilter( double x0, double x1, const QString &originChartId );
    /// Shows the pick marker at the full-resolution pixel of @p index in the
    /// last scatter payload (geotransform arithmetic, no I/O).
    void showPickMarker( int index );
    void publishToHub( const VaSelectionSubject &subject );
    void consumeHubEvent( const VaSelectionEvent &event );

    RasterPathProvider m_provider;
    CanvasProvider m_canvasProvider;
    RasterLayerProvider m_rasterLayerProvider;
    QPointer<VaSelectionHub> m_hub;
    /// This panel's hub origin token (per-instance) — its own broadcasts
    /// are never re-consumed by itself.
    QString m_origin;

    QComboBox *m_bandA = nullptr;
    QComboBox *m_bandB = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_cursorLabel = nullptr;
    VaChartWidget *m_histogramChart = nullptr;
    VaChartWidget *m_scatterChart = nullptr;
    VaChartWidget *m_profileChart = nullptr;

    VaDataSource m_histogramSource;
    VaDataSource m_scatterSource;
    VaDataSource m_profileSource;
    VaCursorProbe m_probe;

    /// Bounded payloads kept for linked filtering (scatter re-projection is
    /// client-side over this snapshot, never a rescan). m_lastScatter is the
    /// newest FULL payload from the source; m_displayedScatter is what the
    /// widget currently shows (possibly brush-filtered) — picks MUST resolve
    /// against the displayed payload or a post-filter index maps to the
    /// wrong point.
    VaData m_lastScatter;
    VaData m_displayedScatter;
    double m_filterMin = 0;
    double m_filterMax = 0;
    bool m_hasFilter = false;

    QPointer<QgsMapCanvas> m_markerCanvas;
    /// Marker is a canvas CHILD (QGraphicsItem, not a QObject) — raw
    /// pointer, recreated whenever the provider returns a different canvas;
    /// only used behind a live provider() lookup.
    QgsVertexMarker *m_pickMarker = nullptr;
};

} // namespace sicnu::app::va
