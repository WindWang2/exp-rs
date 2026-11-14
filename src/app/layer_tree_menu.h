#pragma once

#include <QMenu>
#include <QModelIndex>
#include <QString>

// QGIS includes
#include <layertree/qgslayertreeview.h>
#include <layertree/qgslayertreemodel.h>
#include <layertree/qgslayertreeviewdefaultactions.h>
#include <qgsmapcanvas.h>
#include <qgslayertreenode.h>
#include <qgslayertreelayer.h>
#include <qgslayertreegroup.h>
#include <qgsrasterlayer.h>
#include <qgsrectangle.h>

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

    /**
     * R30-T06 / F1 — the ONE projection of "zoom this group on the active
     * canvas". Pure and testable: it never dereferences a null/expired canvas,
     * skips removed / invalid / non-spatial / empty-extent layers, and treats a
     * failed CRS transform as unusable instead of crashing. Returns a NULL
     * rectangle (and fills \p reason) when the group cannot be zoomed to, so
     * callers can disable the action and explain why instead of silently
     * doing nothing. The 5% margin matches QgsLayerTreeViewDefaultActions.
     */
    static QgsRectangle groupZoomExtent( QgsMapCanvas *canvas,
                                         QgsLayerTreeGroup *group,
                                         QString *reason = nullptr );

private:
    QgsLayerTreeView *mView = nullptr;
    ActiveViewHost *m_activeViewHost = nullptr;
    /// Optional Workbench 5.0 command projection source (weak, not owned).
    sicnu::app::CommandRegistry *m_registry = nullptr;
};
