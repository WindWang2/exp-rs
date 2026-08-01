#pragma once

#include <QMenu>
#include <QModelIndex>

// QGIS includes
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsmapcanvas.h>
#include <qgslayertreenode.h>
#include <qgslayertreelayer.h>
#include <qgsrasterlayer.h>

class ActiveViewHost;

class LayerTreeMenuProvider : public QgsLayerTreeViewMenuProvider
{
public:
    LayerTreeMenuProvider(QgsLayerTreeView *view, ActiveViewHost *activeViewHost);

    QMenu *createContextMenu() override;

private:
    QgsLayerTreeView *mView = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
};
