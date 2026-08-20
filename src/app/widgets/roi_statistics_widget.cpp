// roi_statistics_widget.cpp — ROI Statistics Analysis Widget
#include "roi_statistics_widget.h"
#include "processing/algorithms/math_utils.h"
#include "core/sicnu_logging.h"

#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsvectorlayer.h>
#include <qgsgeometry.h>
#include <qgsfeature.h>
#include <qgsfeaturerequest.h>
#include <qgsrectangle.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>

#include <gdal.h>
#include <cmath>

RoiStatisticsWidget::RoiStatisticsWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void RoiStatisticsWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    // Summary label
    m_summaryLabel = new QLabel(tr("Select a raster layer and ROI to compute statistics."));
    layout->addWidget(m_summaryLabel);

    // Stats table
    m_statsTable = new QTableWidget;
    m_statsTable->setToolTip(tr("ROI 区域内各波段的统计（最小/最大/均值/标准差/像元数）。"));
    m_statsTable->setColumnCount(6);
    m_statsTable->setHorizontalHeaderLabels({tr("Band"), tr("Min"), tr("Max"), tr("Mean"), tr("StdDev"), tr("Pixels")});
    m_statsTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_statsTable);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    m_refreshBtn = new QPushButton(tr("Refresh"));
    m_refreshBtn->setToolTip(tr("重新计算当前 ROI 的统计。"));
    m_exportBtn = new QPushButton(tr("Export CSV"));
    m_exportBtn->setToolTip(tr("把统计表导出为 CSV 文件。"));
    btnLayout->addStretch();
    btnLayout->addWidget(m_refreshBtn);
    btnLayout->addWidget(m_exportBtn);
    layout->addLayout(btnLayout);

    connect(m_refreshBtn, &QPushButton::clicked, this, &RoiStatisticsWidget::computeStatistics);
    connect(m_exportBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(this, tr("Export Statistics"), QString(), tr("CSV (*.csv)"));
        if (path.isEmpty()) return;

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Error"), tr("Cannot open file for writing."));
            return;
        }
        QTextStream out(&file);
        out << "Band,Min,Max,Mean,StdDev,Pixels\n";
        for (int i = 0; i < m_stats.size(); ++i) {
            out << (i + 1) << ","
                << m_stats[i].min << ","
                << m_stats[i].max << ","
                << m_stats[i].mean << ","
                << m_stats[i].stddev << ","
                << m_stats[i].pixelCount << "\n";
        }
        file.close();
        QMessageBox::information(this, tr("Export"), tr("Statistics exported to %1").arg(path));
    });
}

void RoiStatisticsWidget::setRasterLayer(QgsRasterLayer *layer)
{
    m_rasterLayer = layer;
    m_stats.clear();
    updateTable();
}

void RoiStatisticsWidget::setRoiLayer(QgsVectorLayer *roiLayer)
{
    m_roiLayer = roiLayer;
}

void RoiStatisticsWidget::computeStatistics()
{
    if (!m_rasterLayer || !m_rasterLayer->dataProvider()) {
        m_summaryLabel->setText(tr("No raster layer selected."));
        return;
    }

    int bandCount = m_rasterLayer->bandCount();
    m_stats.resize(bandCount);

    // Get ROI geometry (union of all features)
    QgsGeometry roiGeom;
    if (m_roiLayer && m_roiLayer->isValid()) {
        QgsFeatureIterator iter = m_roiLayer->getFeatures();
        QgsFeature feat;
        while (iter.nextFeature(feat)) {
            if (feat.hasGeometry()) {
                if (roiGeom.isNull())
                    roiGeom = feat.geometry();
                else
                    roiGeom = roiGeom.combine(feat.geometry());
            }
        }
    }

    // Get bounding box of ROI or full extent
    QgsRectangle bbox = roiGeom.isNull() ? m_rasterLayer->extent() : roiGeom.boundingBox();

    // Read raster data
    GDALDatasetH ds = GDALOpen(m_rasterLayer->source().toUtf8().constData(), GA_ReadOnly);
    if (!ds) return;

    double gt[6];
    if (GDALGetGeoTransform(ds, gt) != CE_None) {
        gt[0] = 0; gt[1] = 1; gt[2] = 0;
        gt[3] = 0; gt[4] = 0; gt[5] = 1;
    }

    double invGt[6];
    if ( !GDALInvGeoTransform( gt, invGt ) ) {
        invGt[0] = 0; invGt[1] = 1; invGt[2] = 0;
        invGt[3] = 0; invGt[4] = 0; invGt[5] = 1;
    }

    const double xs[4] = { bbox.xMinimum(), bbox.xMaximum(), bbox.xMinimum(), bbox.xMaximum() };
    const double ys[4] = { bbox.yMinimum(), bbox.yMinimum(), bbox.yMaximum(), bbox.yMaximum() };
    double pxMin = std::numeric_limits<double>::infinity();
    double pxMax = -std::numeric_limits<double>::infinity();
    double pyMin = std::numeric_limits<double>::infinity();
    double pyMax = -std::numeric_limits<double>::infinity();

    for ( int i = 0; i < 4; ++i ) {
        const double px = invGt[0] + xs[i] * invGt[1] + ys[i] * invGt[2];
        const double py = invGt[3] + xs[i] * invGt[4] + ys[i] * invGt[5];
        pxMin = std::min( pxMin, px );
        pxMax = std::max( pxMax, px );
        pyMin = std::min( pyMin, py );
        pyMax = std::max( pyMax, py );
    }

    int rWidth = GDALGetRasterXSize(ds);
    int rHeight = GDALGetRasterYSize(ds);

    int xOff = std::clamp(static_cast<int>(std::floor(pxMin)), 0, rWidth);
    int yOff = std::clamp(static_cast<int>(std::floor(pyMin)), 0, rHeight);
    int xSize = std::clamp(static_cast<int>(std::ceil(pxMax)) - xOff, 0, rWidth - xOff);
    int ySize = std::clamp(static_cast<int>(std::ceil(pyMax)) - yOff, 0, rHeight - yOff);

    if (xSize <= 0 || ySize <= 0) {
        GDALClose(ds);
        return;
    }

    for (int b = 0; b < bandCount; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
        if (!band) continue;

        std::vector<float> buf(xSize * ySize);
        GDALRasterIO(band, GF_Read, xOff, yOff, xSize, ySize, buf.data(), xSize, ySize, GDT_Float32, 0, 0);

        // Compute statistics using shared utility
        MathUtils::Stats s = MathUtils::computeStats(buf.data(), buf.size());
        m_stats[b].pixelCount = static_cast<int>(s.validCount);
        m_stats[b].mean = s.mean;
        m_stats[b].stddev = s.stddev;
        m_stats[b].min = s.min;
        m_stats[b].max = s.max;
    }
    GDALClose(ds);

    m_summaryLabel->setText(tr("Statistics computed for %1 bands, %2 pixels")
                                .arg(bandCount).arg(m_stats[0].pixelCount));
    updateTable();

    SICNU_LOG_INFO(SicnuLogTags::Widgets,
                   QString("ROI statistics computed: %1 bands, %2 pixels").arg(bandCount).arg(m_stats[0].pixelCount));
}

void RoiStatisticsWidget::updateTable()
{
    m_statsTable->setRowCount(m_stats.size());
    for (int i = 0; i < m_stats.size(); ++i) {
        m_statsTable->setItem(i, 0, new QTableWidgetItem(tr("Band %1").arg(i + 1)));
        m_statsTable->setItem(i, 1, new QTableWidgetItem(QString::number(m_stats[i].min, 'f', 2)));
        m_statsTable->setItem(i, 2, new QTableWidgetItem(QString::number(m_stats[i].max, 'f', 2)));
        m_statsTable->setItem(i, 3, new QTableWidgetItem(QString::number(m_stats[i].mean, 'f', 2)));
        m_statsTable->setItem(i, 4, new QTableWidgetItem(QString::number(m_stats[i].stddev, 'f', 4)));
        m_statsTable->setItem(i, 5, new QTableWidgetItem(QString::number(m_stats[i].pixelCount)));
    }
}
