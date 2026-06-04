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

class QgisDesktopWindow;

class LayerTreeMenuProvider : public QgsLayerTreeViewMenuProvider
{
public:
    LayerTreeMenuProvider(QgsLayerTreeView *view, QgsMapCanvas *canvas, QgisDesktopWindow *window);

    QMenu *createContextMenu() override;

private:
    QgsLayerTreeView *mView;
    QgsMapCanvas *mCanvas;
    QgisDesktopWindow *mWindow;
};
