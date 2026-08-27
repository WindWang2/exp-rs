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
#include <qgsgeometryengine.h>
#include <qgspoint.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QThreadPool>

#include <gdal.h>
#include <QMetaObject>

#include <cmath>
#include <limits>
#include <memory>

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
    // #625: this used to read the FULL raster window and run a per-pixel
    // GEOS predicate (one QgsGeometry allocation per pixel) synchronously in
    // the Refresh slot - an unbounded GUI freeze (10 GB/band and up to 2.5e9
    // predicate calls on a 50k x 50k scene). Now: the heavy work runs on the
    // global thread pool with a staleness guard (HistogramWidget pattern),
    // the ROI test uses ONE prepared geometry engine, and a missing ROI
    // reads a decimated window instead of the whole scene.
    if (m_computing)
        return;
    if (!m_rasterLayer || !m_rasterLayer->dataProvider()) {
        m_summaryLabel->setText(tr("No raster layer selected."));
        return;
    }

    const QString source = m_rasterLayer->source();
    const int bandCount = m_rasterLayer->bandCount();
    if (source.isEmpty() || bandCount <= 0) {
        m_summaryLabel->setText(tr("No raster layer selected."));
        return;
    }

    // ROI union is computed on the GUI thread (a drawn ROI layer is small);
    // the worker only consumes the resulting WKT + bbox.
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
    const QString roiWkt = roiGeom.isNull() ? QString() : roiGeom.asWkt();
    const QgsRectangle layerExtent = m_rasterLayer->extent();

    m_computing = true;
    m_refreshBtn->setEnabled(false);
    m_summaryLabel->setText(tr("Computing…"));

    const uint64_t reqId = ++m_requestEpoch;
    QPointer<RoiStatisticsWidget> self = this;
    QThreadPool::globalInstance()->start([self, source, bandCount, roiWkt, layerExtent, reqId]() {
        QVector<BandStats> stats(bandCount);
        QString error;

        QgsGeometry roi = roiWkt.isEmpty() ? QgsGeometry() : QgsGeometry::fromWkt(roiWkt);
        const QgsRectangle bbox = roi.isNull() ? layerExtent : roi.boundingBox();

        GDALDatasetH ds = GDALOpen(source.toUtf8().constData(), GA_ReadOnly);
        if (!ds) {
            QMetaObject::invokeMethod(qApp, [self, reqId, error = tr("Cannot open raster.")]() {
                if (!self || self->m_requestEpoch != reqId)
                    return;
                self->m_computing = false;
                self->m_refreshBtn->setEnabled(true);
                self->m_summaryLabel->setText(error);
            });
            return;
        }

        double gt[6];
        if (GDALGetGeoTransform(ds, gt) != CE_None) {
            gt[0] = 0; gt[1] = 1; gt[2] = 0;
            gt[3] = 0; gt[4] = 0; gt[5] = 1;
        }
        double invGt[6];
        if (!GDALInvGeoTransform(gt, invGt)) {
            invGt[0] = 0; invGt[1] = 1; invGt[2] = 0;
            invGt[3] = 0; invGt[4] = 0; invGt[5] = 1;
        }

        const double xs[4] = {bbox.xMinimum(), bbox.xMaximum(), bbox.xMinimum(), bbox.xMaximum()};
        const double ys[4] = {bbox.yMinimum(), bbox.yMinimum(), bbox.yMaximum(), bbox.yMaximum()};
        double pxMin = std::numeric_limits<double>::infinity();
        double pxMax = -std::numeric_limits<double>::infinity();
        double pyMin = std::numeric_limits<double>::infinity();
        double pyMax = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < 4; ++i) {
            const double px = invGt[0] + xs[i] * invGt[1] + ys[i] * invGt[2];
            const double py = invGt[3] + xs[i] * invGt[4] + ys[i] * invGt[5];
            pxMin = std::min(pxMin, px);
            pxMax = std::max(pxMax, px);
            pyMin = std::min(pyMin, py);
            pyMax = std::max(pyMax, py);
        }

        const int rWidth = GDALGetRasterXSize(ds);
        const int rHeight = GDALGetRasterYSize(ds);
        const int xOff = std::clamp(static_cast<int>(std::floor(pxMin)), 0, rWidth);
        const int yOff = std::clamp(static_cast<int>(std::floor(pyMin)), 0, rHeight);
        const int xSize = std::clamp(static_cast<int>(std::ceil(pxMax)) - xOff, 0, rWidth - xOff);
        const int ySize = std::clamp(static_cast<int>(std::ceil(pyMax)) - yOff, 0, rHeight - yOff);

        if (xSize <= 0 || ySize <= 0) {
            GDALClose(ds);
            QMetaObject::invokeMethod(qApp, [self, reqId, error = tr("ROI outside raster extent.")]() {
                if (!self || self->m_requestEpoch != reqId)
                    return;
                self->m_computing = false;
                self->m_refreshBtn->setEnabled(true);
                self->m_summaryLabel->setText(error);
            });
            return;
        }

        // Decimation budget: without a ROI the window is the whole scene -
        // read a downsampled grid (documented approximation) instead of
        // materializing multi-GB full resolution. With a ROI the window is
        // bounded by the drawn geometry at full resolution.
        constexpr double kMaxSamplePixels = 4.0e6;
        int bufW = xSize;
        int bufH = ySize;
        if (roi.isNull() && static_cast<double>(xSize) * ySize > kMaxSamplePixels) {
            const double scale = std::sqrt(kMaxSamplePixels / (static_cast<double>(xSize) * ySize));
            bufW = std::max(1, static_cast<int>(xSize * scale));
            bufH = std::max(1, static_cast<int>(ySize * scale));
        }

        // ONE prepared geometry engine for the whole ROI test (GEOS index),
        // stack-allocated points per pixel instead of QgsGeometry heap
        // allocations (the old path allocated + GEOS-parsed per pixel).
        std::unique_ptr<QgsGeometryEngine> engine;
        if (!roi.isNull() && roi.constGet()) {
            engine.reset(QgsGeometry::createGeometryEngine(roi.constGet()));
            engine->prepareGeometry();
        }
        const QgsRectangle roiBBox = roi.isNull() ? bbox : roi.boundingBox();

        std::vector<float> buf(static_cast<size_t>(bufW) * bufH);
        std::vector<float> roiPixels;
        for (int b = 0; b < bandCount; ++b) {
            GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
            if (!band) continue;
            if (GDALRasterIO(band, GF_Read, xOff, yOff, xSize, ySize,
                             buf.data(), bufW, bufH, GDT_Float32, 0, 0) != CE_None) {
                continue;
            }

            int hasNoData = 0;
            const double noDataVal = GDALGetRasterNoDataValue(band, &hasNoData);

            roiPixels.clear();
            roiPixels.reserve(buf.size());
            if (!engine) {
                roiPixels.assign(buf.begin(), buf.end());
            } else {
                for (int y = 0; y < bufH; ++y) {
                    const double rCenter = yOff + (y + 0.5) * ySize / bufH;
                    for (int x = 0; x < bufW; ++x) {
                        const double cCenter = xOff + (x + 0.5) * xSize / bufW;
                        const double mapX = gt[0] + cCenter * gt[1] + rCenter * gt[2];
                        const double mapY = gt[3] + cCenter * gt[4] + rCenter * gt[5];
                        if (!roiBBox.contains(mapX, mapY))
                            continue;
                        QgsPoint pt(mapX, mapY);
                        if (engine->intersects(&pt))
                            roiPixels.push_back(buf[static_cast<size_t>(y) * bufW + x]);
                    }
                }
            }

            MathUtils::Stats st;
            if (hasNoData && std::isfinite(noDataVal)) {
                st = MathUtils::computeStatsWithNodata(roiPixels.data(), roiPixels.size(),
                                                       static_cast<float>(noDataVal));
            } else {
                st = MathUtils::computeStats(roiPixels.data(), roiPixels.size());
            }
            stats[b].pixelCount = static_cast<int>(st.validCount);
            stats[b].mean = st.mean;
            stats[b].stddev = st.stddev;
            stats[b].min = st.min;
            stats[b].max = st.max;
        }
        GDALClose(ds);

        QMetaObject::invokeMethod(qApp, [self, reqId, stats = std::move(stats)]() mutable {
            if (!self || self->m_requestEpoch != reqId)
                return;  // superseded by a newer request or the widget died
            self->m_stats = std::move(stats);
            self->m_computing = false;
            self->m_refreshBtn->setEnabled(true);
            const int displayPixels = self->m_stats.isEmpty() ? 0 : self->m_stats[0].pixelCount;
            self->m_summaryLabel->setText(
                RoiStatisticsWidget::tr("Statistics computed for %1 bands, %2 pixels")
                    .arg(self->m_stats.size())
                    .arg(displayPixels));
            self->updateTable();
            SICNU_LOG_INFO(SicnuLogTags::Widgets,
                           QString("ROI statistics computed: %1 bands, %2 pixels")
                               .arg(self->m_stats.size())
                               .arg(displayPixels));
        });
    });
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
