#pragma once

#include "display/qgis_display_manager.h"

#include <QWidget>

#include <memory>

class QLabel;
class QPushButton;
class QToolButton;
class QgsMapCanvas;
class QgsMapLayerStore;
class QgsLayerTree;
class QgsLayerTreeModel;
class QgsLayerTreeView;
class QgsMapToolPan;

/**
 * Shell host for one secondary Display View (Wave D).
 *
 * Owns {canvas, independent layer tree, layer store} for
 * ProjectContext::createSecondaryView. Not the main QGIS-interop view.
 */
class SecondaryMapViewWidget : public QWidget
{
    Q_OBJECT
  public:
    explicit SecondaryMapViewWidget( QWidget *parent = nullptr );
    ~SecondaryMapViewWidget() override;

    QgsMapCanvas *canvas() const { return m_canvas; }
    QgsLayerTree *layerTree() const { return m_layerTree.get(); }
    QgsMapLayerStore *layerStore() const { return m_layerStore; }

    sicnu::display::DisplayViewSpec viewSpec() const;

    void setViewId( sicnu::display::DisplayViewId id );
    sicnu::display::DisplayViewId viewId() const { return m_viewId; }

    void setActiveHighlight( bool active );

  signals:
    void activateRequested();
    void closeRequested();
    void syncFromMainRequested();

  protected:
    void mousePressEvent( QMouseEvent *event ) override;

  private:
    QgsMapCanvas *m_canvas = nullptr;
    QgsMapLayerStore *m_layerStore = nullptr;
    std::unique_ptr<QgsLayerTree> m_layerTree;
    QgsLayerTreeModel *m_layerTreeModel = nullptr;
    QgsLayerTreeView *m_layerTreeView = nullptr;
    QgsMapToolPan *m_panTool = nullptr;

    QLabel *m_titleLabel = nullptr;
    QToolButton *m_activateBtn = nullptr;
    QToolButton *m_syncBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;

    sicnu::display::DisplayViewId m_viewId;
};
