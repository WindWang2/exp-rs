// multi_temporal_widget.cpp — Multi-temporal Analysis Widget
#include "multi_temporal_widget.h"
#include "core/sicnu_logging.h"

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
    selLayout->addWidget(m_layer1Combo);
    selLayout->addWidget(new QLabel(tr("Date 2:")));
    m_layer2Combo = new QComboBox();
    selLayout->addWidget(m_layer2Combo);
    layout->addLayout(selLayout);

    // Analyze button
    auto *btnLayout = new QHBoxLayout();
    auto *analyzeBtn = new QPushButton(tr("Analyze Change"));
    auto *exportBtn = new QPushButton(tr("Export"));
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

    auto *layer1 = m_entries[idx1].layer;
    auto *layer2 = m_entries[idx2].layer;

    if (!layer1 || !layer2) return;

    m_summaryLabel->setText(tr("Comparing %1 vs %2").arg(m_entries[idx1].date, m_entries[idx2].date));
    computeTemporalStats();
}

void MultiTemporalWidget::computeTemporalStats()
{
    int idx1 = m_layer1Combo->currentData().toInt();
    int idx2 = m_layer2Combo->currentData().toInt();

    auto *layer1 = m_entries[idx1].layer;
    auto *layer2 = m_entries[idx2].layer;

    int bandCount = std::min(layer1->bandCount(), layer2->bandCount());
    m_statsTable->setRowCount(bandCount);

    for (int b = 0; b < bandCount; ++b) {
        // Read band stats from both layers
        auto stats1 = layer1->dataProvider()->bandStatistics(b + 1);
        auto stats2 = layer2->dataProvider()->bandStatistics(b + 1);

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
