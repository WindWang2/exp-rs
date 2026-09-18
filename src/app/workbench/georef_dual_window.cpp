// georef_dual_window.cpp — D14 Package G implementation (ADR 0159).
#include "app/workbench/georef_dual_window.h"

#include "app/map_tools/swipe_map_tool.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QSplitter>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <qgsmapcanvas.h>
#include <qgsmaptool.h>
#include <qgspointxy.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>
#include <qgsrubberband.h>

#include <cmath>

namespace rs::app {

namespace {

constexpr int kThrottleMilliseconds = 16; // 60 fps cap

/// Combo index -> transform model (labelled in buildUi).
rs::algorithms::TransformModel modelForIndex(int index)
{
    switch (index) {
    case 0: return rs::algorithms::TransformModel::Translation;
    case 1: return rs::algorithms::TransformModel::Similarity;
    case 2: return rs::algorithms::TransformModel::Affine;
    case 3: return rs::algorithms::TransformModel::Polynomial2;
    case 4: return rs::algorithms::TransformModel::Polynomial3;
    default: return rs::algorithms::TransformModel::Projective;
    }
}

} // namespace

GeorefDualWindow::GeorefDualWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();

    mSyncThrottleTimer.setSingleShot(true);
    mSyncThrottleTimer.setInterval(kThrottleMilliseconds);
    connect(&mSyncThrottleTimer, &QTimer::timeout,
            this, &GeorefDualWindow::applyPendingExtentSync);

    if (mSourceCanvas) {
        connect(mSourceCanvas, &QgsMapCanvas::extentsChanged, this, [this] {
            scheduleExtentSync(true);
        });
    }
    if (mReferenceCanvas) {
        connect(mReferenceCanvas, &QgsMapCanvas::extentsChanged, this, [this] {
            scheduleExtentSync(false);
        });
        // Cursor crosshair mirroring.
        mReferenceCanvas->viewport()->installEventFilter(this);
    }
    if (mSourceCanvas)
        mSourceCanvas->viewport()->installEventFilter(this);
}

GeorefDualWindow::~GeorefDualWindow()
{
    // Destroy the swipe tool while the canvas scene is still alive: its
    // destructor deletes a canvas item owned by that scene, so it must not
    // outlive QgsMapCanvas teardown (deleteLater would run too late).
    if (mSwipeTool) {
        delete mSwipeTool;
        mSwipeTool = nullptr;
    }
}

void GeorefDualWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    mSourceCanvas = new QgsMapCanvas(splitter);
    mSourceCanvas->setCanvasColor(Qt::black);
    mReferenceCanvas = new QgsMapCanvas(splitter);
    mReferenceCanvas->setCanvasColor(Qt::black);
    splitter->addWidget(mSourceCanvas);
    splitter->addWidget(mReferenceCanvas);
    layout->addWidget(splitter, /*stretch=*/1);

    auto* controls = new QWidget(central);
    auto* controlsLayout = new QHBoxLayout(controls);
    mModelCombo = new QComboBox(controls);
    mModelCombo->addItems({tr("Translation"), tr("Similarity"), tr("Affine"),
                           tr("Polynomial 2"), tr("Polynomial 3"), tr("Projective")});
    mModelCombo->setCurrentIndex(2); // Affine
    connect(mModelCombo, &QComboBox::currentIndexChanged,
            this, &GeorefDualWindow::onTransformModelChanged);
    controlsLayout->addWidget(mModelCombo);

    mRmseLabel = new QLabel(tr("RMSE: -"), controls);
    controlsLayout->addWidget(mRmseLabel);

    layout->addWidget(controls);

    mGcpTable = new QTableWidget(0, 6, central);
    mGcpTable->setHorizontalHeaderLabels({tr("ID"), tr("Src X"), tr("Src Y"),
                                          tr("Ref X"), tr("Ref Y"), tr("Residual")});
    mGcpTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(mGcpTable);

    setCentralWidget(central);
}

void GeorefDualWindow::loadSourceImage(const QString& filePath)
{
    if (filePath.isEmpty())
        return;
    auto* layer = new QgsRasterLayer(filePath, QStringLiteral("source"));
    if (!layer->isValid()) {
        delete layer;
        return;
    }
    layer->setParent(this); // window object tree owns the layer
    if (mSourceCanvas) {
        mSourceCanvas->stopRenderingAndSettle();
        mSourceCanvas->setLayers({layer});
        mSourceCanvas->zoomToFullExtent();
    }
    if (mSourceLayer)
        mSourceLayer->deleteLater(); // canvas does not take ownership
    mSourceLayer = layer;
}

void GeorefDualWindow::loadReferenceImage(const QString& filePath)
{
    if (filePath.isEmpty())
        return;
    auto* layer = new QgsRasterLayer(filePath, QStringLiteral("reference"));
    if (!layer->isValid()) {
        delete layer;
        return;
    }
    layer->setParent(this); // window object tree owns the layer
    if (mReferenceCanvas) {
        mReferenceCanvas->stopRenderingAndSettle();
        mReferenceCanvas->setLayers({layer});
        mReferenceCanvas->zoomToFullExtent();
    }
    if (mReferenceLayer)
        mReferenceLayer->deleteLater(); // canvas does not take ownership
    mReferenceLayer = layer;
}

void GeorefDualWindow::setSyncMode(ViewportSyncMode mode)
{
    mSyncMode = mode;
}

ViewportSyncMode GeorefDualWindow::syncMode() const noexcept
{
    return mSyncMode;
}

void GeorefDualWindow::setResidualAmplification(double factor)
{
    mResidualAmplification = std::max(0.0, factor);
    updateResidualBand();
}

double GeorefDualWindow::residualAmplification() const noexcept
{
    return mResidualAmplification;
}

QgsMapCanvas* GeorefDualWindow::sourceCanvas() const noexcept
{
    return mSourceCanvas.data();
}

QgsMapCanvas* GeorefDualWindow::referenceCanvas() const noexcept
{
    return mReferenceCanvas.data();
}

int GeorefDualWindow::gcpTableRowCount() const
{
    return mGcpTable ? mGcpTable->rowCount() : 0;
}

double GeorefDualWindow::displayedGlobalRmse() const
{
    return mDisplayedRmse;
}

bool GeorefDualWindow::isApplyingSync() const noexcept
{
    return mApplyingSync;
}

bool GeorefDualWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseMove &&
        (mSyncMode == ViewportSyncMode::CursorCrosshairSync ||
         mSyncMode == ViewportSyncMode::FullLock)) {
        auto* viewport = qobject_cast<QWidget*>(watched);
        QgsMapCanvas* moved = nullptr;
        if (mSourceCanvas && viewport == mSourceCanvas->viewport())
            moved = mSourceCanvas;
        else if (mReferenceCanvas && viewport == mReferenceCanvas->viewport())
            moved = mReferenceCanvas;
        if (moved) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const QgsPointXY point = moved->getCoordinateTransform()->toMapCoordinates(
                mouse->position().toPoint());
            updateCrosshair(moved, point);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void GeorefDualWindow::updateCrosshair(QgsMapCanvas* moved, const QgsPointXY& point)
{
    if (!moved)
        return;
    if (moved == mSourceCanvas) {
        emit crosshairPositionChanged(point, QgsPointXY());
    } else {
        emit crosshairPositionChanged(QgsPointXY(), point);
    }
}

void GeorefDualWindow::scheduleExtentSync(bool fromSource)
{
    if (mApplyingSync)
        return; // strict reentrancy guard: drop the echo event
    if (mSyncMode != ViewportSyncMode::ExtentSync && mSyncMode != ViewportSyncMode::FullLock)
        return;
    mApplyingSync = true;
    mPendingFromSource = fromSource;
    mSyncThrottleTimer.start();
}

void GeorefDualWindow::applyPendingExtentSync()
{
    // The guard stays up while setExtent fires the mirrored extentsChanged,
    // so the echo handler drops the event instead of recursing.
    QgsMapCanvas* source = mPendingFromSource ? mSourceCanvas.data() : mReferenceCanvas.data();
    QgsMapCanvas* target = mPendingFromSource ? mReferenceCanvas.data() : mSourceCanvas.data();
    if (source && target) {
        const QgsRectangle extent = source->extent();
        target->setExtent(extent, true); // preserve scale
        target->refresh();
    }
    mApplyingSync = false;
}

void GeorefDualWindow::onAddGcpPoint(const QgsPointXY& srcPt, const QgsPointXY& refPt)
{
    rs::core::GcpPoint point;
    // Monotonic serial: size()+1 would collide after any deletion and
    // silently wedge the add-point flow.
    point.id = QStringLiteral("GCP_%1").arg(mNextGcpSerial++);
    while (mGcpManager.findPoint(point.id).has_value())
        point.id = QStringLiteral("GCP_%1").arg(mNextGcpSerial++);
    point.sourceX = srcPt.x();
    point.sourceY = srcPt.y();
    point.targetX = refPt.x();
    point.targetY = refPt.y();
    if (!mGcpManager.addPoint(point))
        return;
    refitTransform();
    refreshGcpTable();
    updateResidualBand();
}

void GeorefDualWindow::onDeleteGcpPoint(int rowIndex)
{
    if (!mGcpTable || rowIndex < 0 || rowIndex >= mGcpTable->rowCount())
        return;
    const QString id = mGcpTable->item(rowIndex, 0)->text();
    if (!mGcpManager.removePoint(id))
        return;
    refitTransform();
    refreshGcpTable();
    updateResidualBand();
}

void GeorefDualWindow::onTransformModelChanged(int modelIndex)
{
    mModel = modelForIndex(modelIndex);
    refitTransform();
    refreshGcpTable();
    updateResidualBand();
}

void GeorefDualWindow::refitTransform()
{
    const auto src = mGcpManager.activePoints();
    std::vector<std::pair<double, double>> sourcePts;
    std::vector<std::pair<double, double>> targetPts;
    sourcePts.reserve(src.size());
    targetPts.reserve(src.size());
    for (const auto& point : src) {
        sourcePts.emplace_back(point.sourceX, point.sourceY);
        targetPts.emplace_back(point.targetX, point.targetY);
    }

    mDisplayedRmse = 0.0;
    mFitted = rs::algorithms::TransformResult{};
    if (sourcePts.size() >= static_cast<size_t>(
                                rs::algorithms::GeometricTransform::minPointsRequired(mModel))) {
        try {
            mFitted = rs::algorithms::GeometricTransform::solve(mModel, sourcePts, targetPts);
        } catch (const std::invalid_argument&) {
            mFitted = rs::algorithms::TransformResult{};
        }
        if (mFitted.success) {
            std::vector<std::pair<double, double>> transformed;
            transformed.reserve(sourcePts.size());
            for (const auto& [u, v] : sourcePts)
                transformed.push_back(
                    rs::algorithms::GeometricTransform::applyForward(mFitted, u, v));
            mGcpManager.updateResiduals(transformed);
            mDisplayedRmse = mGcpManager.computeGlobalRmse();
        }
    }
    if (mRmseLabel)
        mRmseLabel->setText(tr("RMSE: %1 px").arg(mDisplayedRmse, 0, 'f', 4));
}

void GeorefDualWindow::refreshGcpTable()
{
    if (!mGcpTable)
        return;
    const auto points = mGcpManager.allPoints();
    const double rmse = mGcpManager.computeGlobalRmse();

    mGcpTable->setRowCount(static_cast<int>(points.size()));
    for (int row = 0; row < static_cast<int>(points.size()); ++row) {
        const auto& point = points[static_cast<size_t>(row)];
        const auto cell = [this, row](int column, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            mGcpTable->setItem(row, column, item);
            return item;
        };
        cell(0, point.id);
        cell(1, QString::number(point.sourceX, 'f', 3));
        cell(2, QString::number(point.sourceY, 'f', 3));
        cell(3, QString::number(point.targetX, 'f', 3));
        cell(4, QString::number(point.targetY, 'f', 3));
        auto* residualItem = cell(5, QString::number(point.residualTotal, 'f', 4));
        // Gross-blunder highlight: residual beyond three sigma of the fit.
        if (point.enabled && rmse > 0.0 && point.residualTotal > 3.0 * rmse)
            residualItem->setBackground(Qt::red);
        else
            residualItem->setBackground(Qt::white);
    }
}

void GeorefDualWindow::updateResidualBand()
{
    if (!mReferenceCanvas)
        return;
    if (!mResidualBand) {
        mResidualBand = new QgsRubberBand(mReferenceCanvas, Qgis::GeometryType::Line);
        mResidualBand->setColor(Qt::red);
        mResidualBand->setWidth(2);
    }
    mResidualBand->reset(Qgis::GeometryType::Line);
    if (mResidualAmplification <= 0.0) {
        mReferenceCanvas->refresh();
        return;
    }
    for (const auto& point : mGcpManager.activePoints()) {
        const QgsPointXY base(point.targetX, point.targetY);
        const QgsPointXY tip(point.targetX + mResidualAmplification * point.residualX,
                             point.targetY + mResidualAmplification * point.residualY);
        mResidualBand->addPoint(base, false);
        mResidualBand->addPoint(tip, true);
    }
    mReferenceCanvas->refresh();
}

void GeorefDualWindow::onExecuteWarpClicked()
{
    // Refit and report the achieved RMSE. GeoTIFF export is deliberately NOT
    // performed here: warp execution and product writing are owned by the
    // georeferencing session / task pipeline (ADR 0020 executor seam), so the
    // emitted path stays empty until that hand-off exists.
    refitTransform();
    emit rectificationFinished(QString(), mDisplayedRmse);
}

void GeorefDualWindow::onToggleSwipeComparison(bool enabled)
{
    if (!mSourceCanvas || !mReferenceCanvas)
        return;
    if (enabled && !mSwipeTool) {
        mSwipeTool = new SwipeMapTool(mReferenceCanvas);
        if (mSourceLayer)
            static_cast<SwipeMapTool*>(mSwipeTool.data())->setBaseLayer(mSourceLayer);
        if (mReferenceLayer)
            static_cast<SwipeMapTool*>(mSwipeTool.data())->setCompareLayer(mReferenceLayer);
    }
    if (enabled) {
        if (mSwipeTool)
            mReferenceCanvas->setMapTool(mSwipeTool);
    } else if (mSwipeTool) {
        mReferenceCanvas->unsetMapTool(mSwipeTool);
    }
}

} // namespace rs::app
