/***************************************************************************
 * va_workbench_panel.h — Workbench 10.0 Visual Analytics workbench surface
 *
 * The consumer that proves the VA platform end-to-end: three chart hosts
 * fed by three VaDataSource jobs over the SAME selected raster —
 *   * band histogram (band A)            [sampled estimate, labeled]
 *   * two-band scatter (bands A × B)     [bounded point sample]
 *   * per-band mean curve                [sampled estimate, labeled]
 *
 * Linked filtering is implemented HERE, between real charts: brushing an
 * x-range on the histogram filters the scatter points into that value
 * range (client-side over the bounded payloads — no rescan). The panel is
 * a thin composition of platform pieces; it owns no store, does no GUI-
 * thread I/O and reports every estimate honestly.
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include "va_source.h"

#include <functional>

class QComboBox;
class QLabel;
class QPushButton;

namespace sicnu::app::va
{

class VaChartWidget;

class VaWorkbenchPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    /// Current raster source path from the unified selection (empty = none).
    using RasterPathProvider = std::function<QString()>;

    explicit VaWorkbenchPanel( RasterPathProvider provider, QWidget *parent = nullptr );

  public slots:
    /// Re-runs the three sources for the current raster/bands.
    void refreshCharts();

  private:
    void buildUi();
    void requestHistogram();
    void requestScatter();
    void requestProfile();

    RasterPathProvider m_provider;

    QComboBox *m_bandA = nullptr;
    QComboBox *m_bandB = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    VaChartWidget *m_histogramChart = nullptr;
    VaChartWidget *m_scatterChart = nullptr;
    VaChartWidget *m_profileChart = nullptr;

    VaDataSource m_histogramSource;
    VaDataSource m_scatterSource;
    VaDataSource m_profileSource;

    /// Bounded payloads kept for linked filtering (scatter re-projection is
    /// client-side over this snapshot, never a rescan).
    VaData m_lastScatter;
    double m_filterMin = 0;
    double m_filterMax = 0;
    bool m_hasFilter = false;
};

} // namespace sicnu::app::va
