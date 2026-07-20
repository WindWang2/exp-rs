// main_window_connections.cpp — Signal/slot wiring and canvas updates
#include "main_window.h"

#include "layer_manager.h"
#include "layer_tree_menu.h"
#include "widgets/histogram_stretch_widget.h"

#include <QStatusBar>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgslayertreeview.h>
#include <qgslayertreelayer.h>
#include <qgslayertreenode.h>
#include <qgslayertreegroup.h>
#include <qgsmapcanvas.h>
#include <qgsmaptoolidentify.h>
#include <qgscoordinatereferencesystem.h>

#include <QPainter>

void QgisDesktopWindow::setupConnections()
{
    // Map canvas signals
    connect(m_mapCanvas, &QgsMapCanvas::xyCoordinates,
            this, &QgisDesktopWindow::showCoordinates);
    connect(m_mapCanvas, &QgsMapCanvas::scaleChanged,
            this, &QgisDesktopWindow::updateScale);
    connect(m_mapCanvas, &QgsMapCanvas::extentsChanged,
            this, &QgisDesktopWindow::updateExtents);
    connect(m_mapCanvas, &QgsMapCanvas::renderComplete,
            this, &QgisDesktopWindow::onRenderComplete);
    m_renderTimer.start();

    // Task manager — update ready status when tasks start/end
    connect(QgsApplication::taskManager(), &QgsTaskManager::statusChanged,
            this, [this](long taskId, int status) {
        Q_UNUSED(taskId);
        Q_UNUSED(status);
        if (m_readyLabel) {
            int activeCount = QgsApplication::taskManager()->countActiveTasks();
            if (activeCount > 0) {
                m_readyLabel->setText(tr("Processing (%1 tasks)...").arg(activeCount));
                m_readyLabel->setStyleSheet("color: #e67e22; font-weight: bold;");
            } else {
                m_readyLabel->setText(tr("Ready"));
                m_readyLabel->setStyleSheet("");
            }
        }
    });

    // Identify tool results
    connect(m_identifyTool, &CustomIdentifyTool::identifyCompleted,
            this, &QgisDesktopWindow::onIdentifyResults);

    // Project signals
    connect(QgsProject::instance(), &QgsProject::readProject,
            this, &QgisDesktopWindow::onProjectRead);
    connect(QgsProject::instance(), &QgsProject::writeProject,
            this, &QgisDesktopWindow::onProjectWrite);
}

void QgisDesktopWindow::initLayerTree()
{
    // Delegate layer tree model + bridge setup to LayerManager
    m_layerManager->initLayerTree();
    m_layerTreeModel = m_layerManager->layerTreeModel();

    // Connect layer tree signals (UI events remain in the window)
    connect(m_layerTreeView, &QgsLayerTreeView::clicked,
            this, &QgisDesktopWindow::onLayerTreeClicked);
    connect(m_layerTreeView, &QgsLayerTreeView::doubleClicked,
            this, &QgisDesktopWindow::onLayerTreeDoubleClicked);

    // Connect project signals for CRS updates
    connect(QgsProject::instance(), &QgsProject::crsChanged,
            this, &QgisDesktopWindow::updateCrsDisplay);

    // Set up native QGIS context menu for layer tree
    m_layerTreeMenuProvider = new LayerTreeMenuProvider(m_layerTreeView, m_mapCanvas, this);
    m_layerTreeView->setMenuProvider(m_layerTreeMenuProvider);
}
void QgisDesktopWindow::showCoordinates(const QgsPointXY &point)
{
    m_coordinatesLabel->setText(QString("%1, %2")
        .arg(point.x(), 0, 'f', 2)
        .arg(point.y(), 0, 'f', 2));
}

void QgisDesktopWindow::updateScale()
{
    double s = m_mapCanvas->scale();
    m_scaleLabel->setText(QString("Scale: 1:%1").arg(static_cast<int>(s)));
}

void QgisDesktopWindow::updateExtents()
{
    QgsRectangle ext = m_mapCanvas->extent();
    statusBar()->showMessage(
        QString("Extent: %1,%2 - %3,%4")
            .arg(ext.xMinimum(), 0, 'f', 1)
            .arg(ext.yMinimum(), 0, 'f', 1)
            .arg(ext.xMaximum(), 0, 'f', 1)
            .arg(ext.yMaximum(), 0, 'f', 1),
        5000);
}

void QgisDesktopWindow::updateCrsDisplay()
{
    QgsCoordinateReferenceSystem crs = QgsProject::instance()->crs();
    m_crsLabel->setText(crs.authid());
    if (m_crsSelector) {
        m_crsSelector->setCrs(crs);
    }
}

void QgisDesktopWindow::onRenderComplete(QPainter *painter)
{
    Q_UNUSED(painter);
    // Display render time
    if (m_renderTimeLabel)
    {
        qint64 elapsed = m_renderTimer.elapsed();
        m_renderTimeLabel->setText(tr("Render: %1 ms").arg(elapsed));
    }
    m_renderTimer.start(); // Restart for next render

    // Update cache label with approximate memory usage from loaded raster layers
    if (m_cacheLabel)
    {
        qint64 totalBytes = 0;
        const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
        for (QgsMapLayer *layer : layers)
        {
            QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>(layer);
            if (rl && rl->isValid())
            {
                totalBytes += static_cast<qint64>(rl->width()) * rl->height()
                              * rl->bandCount() * 4; // approximate: float32 per band
            }
        }
        double mb = totalBytes / (1024.0 * 1024.0);
        m_cacheLabel->setText(QString("Cache: %1 MB").arg(mb, 0, 'f', 1));
    }
}

void QgisDesktopWindow::onProjectRead(const QDomDocument &doc)
{
    Q_UNUSED(doc);
    refreshCanvasLayers();
    updateCrsDisplay();

    // Sync editing UI: check if any vector layer is in edit mode
    QgsVectorLayer *activeVl = nullptr;
    const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for (QgsMapLayer *layer : layers)
    {
        QgsVectorLayer *vl = qobject_cast<QgsVectorLayer *>(layer);
        if (vl)
        {
            if (!activeVl)
                activeVl = vl;
        }
    }
    updateEditingUI(activeVl);

    statusBar()->showMessage("Project loaded", 3000);
}

void QgisDesktopWindow::onProjectWrite(QDomDocument &doc)
{
    Q_UNUSED(doc);
    statusBar()->showMessage("Project saved", 2000);
}

void QgisDesktopWindow::onCrsChanged(const QgsCoordinateReferenceSystem &crs)
{
    QgsProject::instance()->setCrs(crs);
    m_mapCanvas->setDestinationCrs(crs);
    m_mapCanvas->refresh();
    updateCrsDisplay();
}

// ── Layer Tree Events ─────────────────────────────────────────────────────
void QgisDesktopWindow::onLayerTreeClicked(const QModelIndex &index)
{
    // Check if the current layer has unsaved edits before switching
    QgsVectorLayer *currentVl = currentVectorLayer();
    if (currentVl && currentVl->isEditable()) {
        QgsLayerTreeNode *node = m_layerTreeView->index2node(index);
        QgsMapLayer *newLayer = nullptr;
        if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
            auto *layerNode = static_cast<QgsLayerTreeLayer *>(node);
            newLayer = layerNode->layer();
        }
        // Only prompt if actually switching to a different layer
        if (newLayer != currentVl && !confirmSaveEdits(currentVl))
            return;
    }

    QgsLayerTreeNode *node = m_layerTreeView->index2node(index);
    if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer *>(node);
        QgsMapLayer *layer = layerNode->layer();
        m_mapCanvas->setCurrentLayer(layer);
        updateEditingUI(qobject_cast<QgsVectorLayer *>(layer));

        if (m_histogramStretch) {
            if (auto *rl = qobject_cast<QgsRasterLayer *>(layer))
                m_histogramStretch->setRasterLayer(rl);
        }
    } else {
        m_mapCanvas->setCurrentLayer(nullptr);
        updateEditingUI(nullptr);
    }
}

void QgisDesktopWindow::onLayerTreeDoubleClicked(const QModelIndex &index)
{
    QgsLayerTreeNode *node = m_layerTreeView->index2node(index);
    if (node && node->nodeType() == QgsLayerTreeNode::NodeLayer) {
        QgsLayerTreeLayer *layerNode = static_cast<QgsLayerTreeLayer*>(node);
        if (layerNode->layer()) {
            showLayerProperties(layerNode->layer());
        }
    }
}
