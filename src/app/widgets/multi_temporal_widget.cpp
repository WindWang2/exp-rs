// multi_temporal_widget.cpp — Multi-temporal Analysis Widget
#include "multi_temporal_widget.h"
#include "core/sicnu_logging.h"

#include <qgsrectangle.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>

#include <gdal.h>
#include <cmath>

MultiTemporalWidget::MultiTemporalWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void MultiTemporalWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    // Layer selection
    auto *selLayout = new QHBoxLayout();
    selLayout->addWidget(new QLabel(tr("Date 1:")));
    m_layer1Combo = new QComboBox();
    m_layer1Combo->setToolTip(tr("选择第一个时相（较早影像）。"));
    m_layer1Combo->setStatusTip(tr("选择第一个时相"));
    selLayout->addWidget(m_layer1Combo);
    selLayout->addWidget(new QLabel(tr("Date 2:")));
    m_layer2Combo = new QComboBox();
    m_layer2Combo->setToolTip(tr("选择第二个时相（较晚影像）。"));
    m_layer2Combo->setStatusTip(tr("选择第二个时相"));
    selLayout->addWidget(m_layer2Combo);
    layout->addLayout(selLayout);

    // Analyze button
    auto *btnLayout = new QHBoxLayout();
    auto *analyzeBtn = new QPushButton(tr("变化分析"));
    analyzeBtn->setToolTip(tr("对比两个时相，计算各波段均值变化。"));
    analyzeBtn->setStatusTip(tr("执行变化分析"));
    auto *exportBtn = new QPushButton(tr("导出"));
    exportBtn->setToolTip(tr("把变化统计表导出为文件。"));
    exportBtn->setStatusTip(tr("导出变化统计"));
    btnLayout->addWidget(analyzeBtn);
    btnLayout->addWidget(exportBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    // Summary
    m_summaryLabel = new QLabel(tr("Add raster layers with dates to analyze."));
    layout->addWidget(m_summaryLabel);

    // Stats table
    m_statsTable = new QTableWidget;
    m_statsTable->setColumnCount(5);
    m_statsTable->setHorizontalHeaderLabels({tr("Band"), tr("Mean 1"), tr("Mean 2"), tr("Change"), tr("% Change")});
    m_statsTable->horizontalHeader()->setStretchLastSection(true);
    m_statsTable->setToolTip(tr("各波段在两个时相的均值与变化量。"));
    layout->addWidget(m_statsTable);

    connect(analyzeBtn, &QPushButton::clicked, this, &MultiTemporalWidget::onAnalyze);
    connect(exportBtn, &QPushButton::clicked, this, &MultiTemporalWidget::onExport);
}

void MultiTemporalWidget::addRasterLayer(QgsRasterLayer *layer, const QString &date)
{
    if (!layer) return;

    TemporalEntry entry;
    entry.layer = layer;
    entry.date = date;
    m_entries.append(entry);

    QString label = QString("%1 (%2)").arg(layer->name(), date);
    m_layer1Combo->addItem(label, m_entries.size() - 1);
    m_layer2Combo->addItem(label, m_entries.size() - 1);

    SICNU_LOG_INFO(SicnuLogTags::Widgets,
                   QString("Added temporal layer: %1 (%2)").arg(layer->name(), date));
}

void MultiTemporalWidget::clear()
{
    m_entries.clear();
    m_layer1Combo->clear();
    m_layer2Combo->clear();
    m_statsTable->setRowCount(0);
    m_summaryLabel->setText(tr("Add raster layers with dates to analyze."));
}

void MultiTemporalWidget::onAnalyze()
{
    int idx1 = m_layer1Combo->currentData().toInt();
    int idx2 = m_layer2Combo->currentData().toInt();

    if (idx1 < 0 || idx1 >= m_entries.size() || idx2 < 0 || idx2 >= m_entries.size()) {
        m_summaryLabel->setText(tr("Select two different dates."));
        return;
    }

    if (idx1 == idx2) {
        m_summaryLabel->setText(tr("Select two different dates."));
        return;
    }

    auto *layer1 = m_entries[idx1].layer.data();
    auto *layer2 = m_entries[idx2].layer.data();

    if (!layer1 || !layer2) return;

    m_summaryLabel->setText(tr("Comparing %1 vs %2").arg(m_entries[idx1].date, m_entries[idx2].date));
    computeTemporalStats();
}

void MultiTemporalWidget::computeTemporalStats()
{
    int idx1 = m_layer1Combo->currentData().toInt();
    int idx2 = m_layer2Combo->currentData().toInt();

    auto *layer1 = m_entries[idx1].layer.data();
    auto *layer2 = m_entries[idx2].layer.data();

    int bandCount = std::min(layer1->bandCount(), layer2->bandCount());
    m_statsTable->setRowCount(bandCount);

    for (int b = 0; b < bandCount; ++b) {
        // Read band stats from both layers
        // Sample-capped (#634): uncapped stats scanned both layers fully on
        // the GUI thread per click.
        auto stats1 = layer1->dataProvider()->bandStatistics(
            b + 1, Qgis::RasterBandStatistic::All, QgsRectangle(), 250000);
        auto stats2 = layer2->dataProvider()->bandStatistics(
            b + 1, Qgis::RasterBandStatistic::All, QgsRectangle(), 250000);

        double mean1 = stats1.mean;
        double mean2 = stats2.mean;
        double change = mean2 - mean1;
        double pctChange = (mean1 != 0) ? (change / mean1 * 100.0) : 0.0;

        m_statsTable->setItem(b, 0, new QTableWidgetItem(tr("Band %1").arg(b + 1)));
        m_statsTable->setItem(b, 1, new QTableWidgetItem(QString::number(mean1, 'f', 2)));
        m_statsTable->setItem(b, 2, new QTableWidgetItem(QString::number(mean2, 'f', 2)));
        m_statsTable->setItem(b, 3, new QTableWidgetItem(QString::number(change, 'f', 2)));
        m_statsTable->setItem(b, 4, new QTableWidgetItem(QString::number(pctChange, 'f', 1) + "%"));
    }

    SICNU_LOG_INFO(SicnuLogTags::Widgets,
                   QString("Temporal analysis: %1 vs %2, %3 bands")
                       .arg(m_entries[idx1].date, m_entries[idx2].date).arg(bandCount));
}

void MultiTemporalWidget::onExport()
{
    SICNU_LOG_INFO(SicnuLogTags::Widgets, "Export temporal analysis");
}
