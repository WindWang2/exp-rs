// roi_statistics_widget.h — ROI Statistics Analysis Widget
#pragma once

#include <QWidget>
#include <QVector>

class QTableWidget;
class QPushButton;
class QLabel;
class QgsRasterLayer;
class QgsVectorLayer;

/**
 * Widget for computing and displaying statistics for selected ROIs.
 * Shows min, max, mean, stddev, histogram per band for the selected region.
 */
class RoiStatisticsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RoiStatisticsWidget(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);
    void setRoiLayer(QgsVectorLayer *roiLayer);

    /** Compute statistics for the current ROI. */
    void computeStatistics();

private:
    void setupUi();
    void updateTable();

    QTableWidget *m_statsTable = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QPushButton *m_refreshBtn = nullptr;

    QgsRasterLayer *m_rasterLayer = nullptr;
    QgsVectorLayer *m_roiLayer = nullptr;

    struct BandStats {
        double min = 0.0;
        double max = 0.0;
        double mean = 0.0;
        double stddev = 0.0;
        int pixelCount = 0;
    };

    QVector<BandStats> m_stats;
};
