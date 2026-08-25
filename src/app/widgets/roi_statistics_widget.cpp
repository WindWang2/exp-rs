// roi_statistics_widget.cpp — ROI Statistics Analysis Widget
#include "roi_statistics_widget.h"
#include "processing/algorithms/math_utils.h"
#include "core/sicnu_logging.h"

#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsvectorlayer.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>
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
#include <QPointer>
#include <QTextStream>
#include <QMessageBox>
#include <QtConcurrent>

#include <gdal.h>
#include <cmath>

namespace {

struct RoiStatsResult
{
    QVector<RoiStatisticsWidget::BandStats> stats;
    int bandCount = 0;
};

} // namespace

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

    // Snapshot inputs for the background computation; the heavy GDAL scan and
    // per-pixel GEOS containment tests must not run on the GUI thread (#515).
    const QString rasterSource = m_rasterLayer->source();

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
    const QgsRectangle bbox = roiGeom.isNull() ? m_rasterLayer->extent() : roiGeom.boundingBox();

    m_refreshBtn->setEnabled(false);
    m_summaryLabel->setText(tr("Computing statistics..."));

    auto *watcher = new QFutureWatcher<RoiStatsResult>(this);
    connect(watcher, &QFutureWatcher<RoiStatsResult>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        if (m_refreshBtn)
            m_refreshBtn->setEnabled(true);
        RoiStatsResult result = watcher->result();
        m_stats = result.stats;

        const int displayPixels = m_stats.isEmpty() ? 0 : m_stats[0].pixelCount;
        m_summaryLabel->setText(tr("Statistics computed for %1 bands, %2 pixels")
                                    .arg(result.bandCount).arg(displayPixels));
        updateTable();

        SICNU_LOG_INFO(SicnuLogTags::Widgets,
                       QString("ROI statistics computed: %1 bands, %2 pixels").arg(result.bandCount).arg(displayPixels));
    });

    watcher->setFuture(QtConcurrent::run([rasterSource, roiGeom, bbox]() -> RoiStatsResult {
        RoiStatsResult out;
        // Read raster data
        GDALDatasetH ds = GDALOpen(rasterSource.toUtf8().constData(), GA_ReadOnly);
        if (!ds) return out;

        const int bandCount = GDALGetRasterCount(ds);
        out.bandCount = bandCount;
        out.stats.resize(bandCount);

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

        const int rWidth = GDALGetRasterXSize(ds);
        const int rHeight = GDALGetRasterYSize(ds);

        int xOff = std::clamp(static_cast<int>(std::floor(pxMin)), 0, rWidth);
        int yOff = std::clamp(static_cast<int>(std::floor(pyMin)), 0, rHeight);
        int xSize = std::clamp(static_cast<int>(std::ceil(pxMax)) - xOff, 0, rWidth - xOff);
        int ySize = std::clamp(static_cast<int>(std::ceil(pyMax)) - yOff, 0, rHeight - yOff);

        if (xSize <= 0 || ySize <= 0 || bandCount <= 0) {
            GDALClose(ds);
            return out;
        }

        // Rasterize / test point-in-polygon mask if ROI geometry is present
        const bool hasRoi = !roiGeom.isNull();
        std::vector<uint8_t> inside(static_cast<size_t>(xSize) * ySize, 1);
        if (hasRoi) {
            for (int y = 0; y < ySize; ++y) {
                const int py = yOff + y;
                const double rCenter = py + 0.5;
                for (int x = 0; x < xSize; ++x) {
                    const int px = xOff + x;
                    const double cCenter = px + 0.5;
                    const double mapX = gt[0] + cCenter * gt[1] + rCenter * gt[2];
                    const double mapY = gt[3] + cCenter * gt[4] + rCenter * gt[5];
                    // intersects (not contains): GEOS Contains excludes points
                    // exactly on the boundary; include boundary-center pixels,
                    // consistent with the rasterizer's boundary handling (#449).
                    inside[static_cast<size_t>(y) * xSize + x] =
                        roiGeom.intersects(QgsGeometry::fromPointXY(QgsPointXY(mapX, mapY))) ? 1 : 0;
                }
            }
        }

        for (int b = 0; b < bandCount; ++b) {
            GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
            if (!band) continue;

            std::vector<float> buf(static_cast<size_t>(xSize) * ySize);
            GDALRasterIO(band, GF_Read, xOff, yOff, xSize, ySize, buf.data(), xSize, ySize, GDT_Float32, 0, 0);

            int hasNoData = 0;
            double noDataVal = GDALGetRasterNoDataValue(band, &hasNoData);

            std::vector<float> roiPixels;
            roiPixels.reserve(buf.size());
            for (size_t i = 0; i < buf.size(); ++i) {
                if (inside[i]) {
                    roiPixels.push_back(buf[i]);
                }
            }

            // Compute statistics using shared utility
            MathUtils::Stats s;
            if (hasNoData && std::isfinite(noDataVal)) {
                s = MathUtils::computeStatsWithNodata(roiPixels.data(), roiPixels.size(), static_cast<float>(noDataVal));
            } else {
                s = MathUtils::computeStats(roiPixels.data(), roiPixels.size());
            }

            out.stats[b].pixelCount = static_cast<int>(s.validCount);
            out.stats[b].mean = s.mean;
            out.stats[b].stddev = s.stddev;
            out.stats[b].min = s.min;
            out.stats[b].max = s.max;
        }
        GDALClose(ds);
        return out;
    }));
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
