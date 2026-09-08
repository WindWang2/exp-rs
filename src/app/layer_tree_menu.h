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

namespace sicnu::app
{
class CommandRegistry;
}

class LayerTreeMenuProvider : public QgsLayerTreeViewMenuProvider
{
public:
    LayerTreeMenuProvider(QgsLayerTreeView *view, ActiveViewHost *activeViewHost,
                          sicnu::app::CommandRegistry *commandRegistry = nullptr);

    QMenu *createContextMenu() override;

private:
    QgsLayerTreeView *mView = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
    /// Optional Workbench 5.0 command projection source (weak, not owned).
    sicnu::app::CommandRegistry *m_registry = nullptr;
};
