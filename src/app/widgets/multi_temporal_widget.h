// multi_temporal_widget.h — Multi-temporal Analysis Widget
#pragma once

#include <QWidget>
#include <QVector>

class QComboBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QgsRasterLayer;

/**
 * Widget for comparing raster images from different dates.
 * Supports temporal profile analysis and change statistics.
 */
class MultiTemporalWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MultiTemporalWidget(QWidget *parent = nullptr);

    void addRasterLayer(QgsRasterLayer *layer, const QString &date);
    void clear();

private slots:
    void onAnalyze();
    void onExport();

private:
    void setupUi();
    void computeTemporalStats();

    struct TemporalEntry {
        QgsRasterLayer *layer;
        QString date;
    };

    QComboBox *m_layer1Combo = nullptr;
    QComboBox *m_layer2Combo = nullptr;
    QTableWidget *m_statsTable = nullptr;
    QLabel *m_summaryLabel = nullptr;

    QVector<TemporalEntry> m_entries;
};
