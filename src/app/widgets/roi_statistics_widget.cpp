// roi_statistics_widget.cpp — ROI Statistics Analysis Widget
#include "roi_statistics_widget.h"
#include "processing/algorithms/math_utils.h"
#include "core/sicnu_logging.h"
#include "rs_scan_pool.h"
#include "durable_file_write.h"

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

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

QThreadPool *RoiStatisticsWidget::analysisThreadPool()
{
    static QThreadPool *s_pool = []() {
        auto *p = new QThreadPool();
        p->setMaxThreadCount( 2 );
        return p;
    }();
    return s_pool;
}

RoiStatisticsWidget::RoiStatisticsWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

RoiStatisticsWidget::~RoiStatisticsWidget()
{
    ++m_requestEpoch;
    // #797: cancel the in-flight scan so the bounded pool stops reading GDAL
    // sources for a widget that is gone (results are dropped by QPointer
    // anyway; this ends the work itself). Pass this as the owner — matching
    // nextGeneration()/isStale(). The owner record itself is erased by the
    // pool's QObject::destroyed hook (rs_scan_pool, #1453), not by cancel
    // (F-07: cancel keeps the record so the SP-2 canceled-stays-stale
    // guarantee holds across cancel-set overflow).
    if ( m_scanGeneration )
        sicnu::app::RsScanPool::instance().cancel( m_scanGeneration, this );
}

void RoiStatisticsWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    // Summary label
    m_summaryLabel = new QLabel(tr("Select a raster layer and ROI to compute statistics."));
    layout->addWidget(m_summaryLabel);

    // Stats table
    m_statsTable = new QTableWidget;
    m_statsTable->setToolTip(tr("Per-band statistics inside the ROI (min/max/mean/stddev/pixel count)."));
    m_statsTable->setColumnCount(6);
    m_statsTable->setHorizontalHeaderLabels({tr("Band"), tr("Min"), tr("Max"), tr("Mean"), tr("StdDev"), tr("Pixels")});
    m_statsTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_statsTable);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    m_refreshBtn = new QPushButton(tr("Refresh"));
    m_refreshBtn->setObjectName(QStringLiteral("rsRoiStatsRefreshBtn"));
    m_refreshBtn->setToolTip(tr("Recomputes statistics for the current ROI."));
    m_exportBtn = new QPushButton(tr("Export CSV"));
    m_exportBtn->setToolTip(tr("Exports the statistics table as a CSV file."));
    btnLayout->addStretch();
    btnLayout->addWidget(m_refreshBtn);
    btnLayout->addWidget(m_exportBtn);
    layout->addLayout(btnLayout);

    connect(m_refreshBtn, &QPushButton::clicked, this, &RoiStatisticsWidget::computeStatistics);
    connect(m_exportBtn, &QPushButton::clicked, this, [this]() {
        if (m_stats.isEmpty()) {
            QMessageBox::information(this, tr("Export"),
                                     tr("No statistics yet — click Refresh first."));
            return;
        }
        QString path = QFileDialog::getSaveFileName(this, tr("Export Statistics"), QString(), tr("CSV (*.csv)"));
        if (path.isEmpty()) return;

        // APP-10 (R14-D WP-B): serialize then publish through the shared
        // crash-or-valid authority; the success message is shown only when the
        // bytes actually landed.
        QByteArray bytes;
        QTextStream out(&bytes, QIODevice::WriteOnly);
        // CSV numbers must use '.' decimals regardless of the user's locale —
        // a German default locale would emit "1234,56" and shift every column.
        out.setLocale(QLocale::c());
        out << "Band,Min,Max,Mean,StdDev,Pixels\n";
        for (int i = 0; i < m_stats.size(); ++i) {
            const auto csvNum = [](double v) {
                return std::isfinite(v) ? QString::number(v, 'f', 6)
                                        : QString(); // all-NoData band → empty cell
            };
            out << (i + 1) << ","
                << csvNum(m_stats[i].min) << ","
                << csvNum(m_stats[i].max) << ","
                << csvNum(m_stats[i].mean) << ","
                << csvNum(m_stats[i].stddev) << ","
                << m_stats[i].pixelCount << "\n";
        }
        out.flush();
#if defined(Q_OS_WIN)
        // The old lane opened the file with QIODevice::Text (LF->CRLF on
        // Windows); keep the published bytes byte-identical.
        bytes.replace('\n', "\r\n");
#endif
        const sicnu::DurableWriteResult result = sicnu::writeBytesDurably(path, bytes);
        if (!result.ok) {
            QMessageBox::warning(this, tr("Error"),
                                 tr("Cannot write file:\n%1\n%2").arg(path, result.error));
            return;
        }
        QMessageBox::information(this, tr("Export"), tr("Statistics exported to %1").arg(path));
    });
}

void RoiStatisticsWidget::setRasterLayer(QgsRasterLayer *layer)
{
    ++m_requestEpoch;
    m_computing = false;
    m_rasterLayer = layer;
    m_stats.clear();
    // A layer switch is a fresh, idle state: the in-flight worker's landing
    // will early-return on the epoch guard and never re-enable the button or
    // rewrite the summary, so restore both here (disabled by computeStatistics).
    m_refreshBtn->setEnabled(true);
    m_summaryLabel->setText(tr("Select a raster layer and ROI to compute statistics."));
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
    // dedicated bounded thread pool (#797) with a staleness guard (HistogramWidget pattern),
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
    // #797: run the GDAL scan on the dedicated bounded scan pool (never the
    // global pool) and carry a cancellation generation — the worker exits at
    // band boundaries when a newer request supersedes it.
    const quint64 scanGeneration = sicnu::app::RsScanPool::instance().nextGeneration( this );
    m_scanGeneration = scanGeneration;
    QPointer<RoiStatisticsWidget> self = this;
    sicnu::app::RsScanPool::instance().pool().start([self, source, bandCount, roiWkt, layerExtent, reqId, scanGeneration]() {
        QVector<BandStats> stats(bandCount);
        QString error;

        if (sicnu::app::RsScanPool::instance().isStale(scanGeneration, self.data()) || !self || self->m_requestEpoch != reqId) {
            QMetaObject::invokeMethod(qApp, [self, reqId]() {
                if (!self || self->m_requestEpoch != reqId)
                    return;
                self->m_computing = false;
                self->m_refreshBtn->setEnabled(true);
                self->m_summaryLabel->setText(self->tr("Superseded."));
            });
            return;
        }

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
            // Cooperative cancellation (#797): a superseded scan abandons the
            // remaining bands promptly instead of monopolising a scan worker.
            if (sicnu::app::RsScanPool::instance().isStale(scanGeneration, self.data()) || !self || self->m_requestEpoch != reqId) {
                GDALClose(ds);
                QMetaObject::invokeMethod(qApp, [self, reqId]() {
                    if (!self || self->m_requestEpoch != reqId)
                        return;
                    self->m_computing = false;
                    self->m_refreshBtn->setEnabled(true);
                    self->m_summaryLabel->setText(self->tr("Superseded."));
                });
                return;
            }
            GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
            if (!band || GDALRasterIO(band, GF_Read, xOff, yOff, xSize, ySize,
                                      buf.data(), bufW, bufH, GDT_Float32, 0, 0) != CE_None) {
                // Unreadable band: no measurement — render as no-data, not 0.00.
                stats[b].pixelCount = 0;
                stats[b].mean = std::numeric_limits<double>::quiet_NaN();
                stats[b].stddev = std::numeric_limits<double>::quiet_NaN();
                stats[b].min = std::numeric_limits<double>::quiet_NaN();
                stats[b].max = std::numeric_limits<double>::quiet_NaN();
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
            if (st.validCount == 0) {
                // All-NoData / empty ROI: zeros would read as a legitimate
                // all-zero measurement. NaN renders as "—" in the table and
                // is skipped by CSV consumers.
                stats[b].mean = std::numeric_limits<double>::quiet_NaN();
                stats[b].stddev = std::numeric_limits<double>::quiet_NaN();
                stats[b].min = std::numeric_limits<double>::quiet_NaN();
                stats[b].max = std::numeric_limits<double>::quiet_NaN();
            } else {
                stats[b].mean = st.mean;
                stats[b].stddev = st.stddev;
                stats[b].min = st.min;
                stats[b].max = st.max;
            }
        }
        GDALClose(ds);

        QMetaObject::invokeMethod(qApp, [self, reqId, stats = std::move(stats)]() mutable {
            if (!self || self->m_requestEpoch != reqId)
                return;  // superseded by a newer request or the widget died
            self->m_stats = std::move(stats);
            self->m_computing = false;
            self->m_refreshBtn->setEnabled(true);
            // Per-band pixel counts can differ (a band whose read failed has
            // none): summarize the widest band so one unreadable band cannot
            // claim "no valid pixels" while another has data.
            int displayPixels = 0;
            for ( const auto &band : self->m_stats )
                displayPixels = std::max( displayPixels, band.pixelCount );
            self->m_summaryLabel->setText(
                displayPixels > 0
                    ? RoiStatisticsWidget::tr("Statistics computed for %1 bands, %2 pixels")
                          .arg(self->m_stats.size())
                          .arg(displayPixels)
                    : RoiStatisticsWidget::tr("Computed %1 bands — no valid pixels in the ROI (all NoData or empty)")
                          .arg(self->m_stats.size()));
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
    // NaN stats (all-NoData / empty ROI) must not render as "0.00" — that
    // reads as a real measurement of zero. "—" says "no value".
    const auto statText = [](double v, int precision) {
        return std::isfinite(v) ? QString::number(v, 'f', precision)
                                : QStringLiteral("—");
    };
    m_statsTable->setRowCount(m_stats.size());
    for (int i = 0; i < m_stats.size(); ++i) {
        m_statsTable->setItem(i, 0, new QTableWidgetItem(tr("Band %1").arg(i + 1)));
        m_statsTable->setItem(i, 1, new QTableWidgetItem(statText(m_stats[i].min, 2)));
        m_statsTable->setItem(i, 2, new QTableWidgetItem(statText(m_stats[i].max, 2)));
        m_statsTable->setItem(i, 3, new QTableWidgetItem(statText(m_stats[i].mean, 2)));
        m_statsTable->setItem(i, 4, new QTableWidgetItem(statText(m_stats[i].stddev, 4)));
        m_statsTable->setItem(i, 5, new QTableWidgetItem(QString::number(m_stats[i].pixelCount)));
    }
}
