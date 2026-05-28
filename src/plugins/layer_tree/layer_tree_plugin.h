#pragma once

#include <QObject>
#include "core/interfaces/sicnu_plugin_interface.h"

class QgsLayerTreeView;
class QgsLayerTreeModel;
class QgsMapCanvas;
class SicnuMainWindow;

class LayerTreePlugin : public QObject, public SicnuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SicnuPluginInterface_iid)
    Q_INTERFACES(SicnuPluginInterface)

public:
    explicit LayerTreePlugin(QObject *parent = nullptr);

    QString name() const override { return "LayerTree"; }
    QString description() const override { return "Layer tree panel with context menu"; }
    QString version() const override { return "1.0.0"; }
    QIcon icon() const override;

    bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) override;
    void unload() override;

    QWidget *createWidget(QWidget *parent) override;
    QList<QAction*> menuActions() override;

private:
    QgsMapCanvas *m_canvas = nullptr;
    QgsLayerTreeView *m_layerTree = nullptr;
    QgsLayerTreeModel *m_model = nullptr;
};
