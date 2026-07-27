// main_window_vector.cpp — Vector layer editing actions
#include "main_window.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>

#include <qgsproject.h>
#include <qgsvectorlayer.h>
#include <qgsmaplayer.h>
#include <qgsmapcanvas.h>
#include <qgsnewvectorlayerdialog.h>
#include <qgsattributetabledialog.h>
#include <qgsattributetablefiltermodel.h>
#include <qgslayertreegroup.h>

#include "active_view_host.h"
#include "project_context.h"
#include "qgsclipboard.h"

#include "data/data_manager.h"

#include "qgsmaptooladdfeature.h"
#include "qgsmaptooladdpart.h"
#include "qgsmaptooladdring.h"
#include "qgsmaptoolmovefeature.h"
#include "qgsmaptoolrotatefeature.h"
#include "qgsmaptoolscalefeature.h"
#include "qgsmaptooloffsetcurve.h"
#include "qgsmaptoolreshape.h"
#include "qgsmaptoolsplitfeatures.h"
#include "qgsmaptoolsplitparts.h"
#include "qgsmaptoolsimplify.h"
#include "qgsmaptoolreverseline.h"
#include "qgsmaptoolfillring.h"
#include "qgsmaptooldeletepart.h"
#include "qgsmaptooldeletering.h"
#include "qgsmaptooltrimextendfeature.h"
#include "qgsmaptoolchamferfillet.h"
#include "qgsmaptoolfeaturearray.h"
#include "selecttools/qgsmaptoolselect.h"
#include "vertextool/qgsvertextool.h"

bool QgisDesktopWindow::checkUnsavedChanges()
{
    // Check for unsaved vector edits
    const QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for (QgsMapLayer *layer : layers)
    {
        QgsVectorLayer *vl = qobject_cast<QgsVectorLayer *>(layer);
        if (vl && vl->isEditable() && vl->isModified())
        {
            int res = QMessageBox::warning(this, tr("Unsaved Changes"),
                tr("Layer '%1' has unsaved edits. Save before proceeding?").arg(vl->name()),
                QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel);
            if (res == QMessageBox::Cancel)
                return false;
            if (res == QMessageBox::SaveAll)
            {
                // Save all modified vector layers
                for (QgsMapLayer *l : layers)
                {
                    QgsVectorLayer *v = qobject_cast<QgsVectorLayer *>(l);
                    if (v && v->isEditable() && v->isModified())
                    {
                        v->commitChanges();
                    }
                }
            }
            else
            {
                // Discard all
                for (QgsMapLayer *l : layers)
                {
                    QgsVectorLayer *v = qobject_cast<QgsVectorLayer *>(l);
                    if (v && v->isEditable())
                        v->rollBack();
                }
            }
            break;
        }
    }

    // Check for unsaved project (prompt even when no path yet — unsaved new projects)
    if (QgsProject::instance()->isDirty())
    {
        int res = QMessageBox::question(this, tr("Save Project"),
            tr("The project has been modified. Save changes?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (res == QMessageBox::Cancel)
            return false;
        if (res == QMessageBox::Save)
        {
            // Empty path → Save As; if user cancels Save As, abort the close/action.
            if (QgsProject::instance()->fileName().isEmpty())
            {
                saveProjectAs();
                if (QgsProject::instance()->fileName().isEmpty())
                    return false;
            }
            else
            {
                QgsProject::instance()->write();
            }
        }
    }

    return true;
}
void QgisDesktopWindow::undo()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (vl && vl->undoStack())
        vl->undoStack()->undo();
    else
        statusBar()->showMessage(tr("Nothing to undo"), 2000);
}

void QgisDesktopWindow::redo()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (vl && vl->undoStack())
        vl->undoStack()->redo();
    else
        statusBar()->showMessage(tr("Nothing to redo"), 2000);
}
void QgisDesktopWindow::cutFeatures()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (!vl || !vl->isEditable()) {
        statusBar()->showMessage(tr("Select an editable vector layer to cut"), 2000);
        return;
    }
    if (vl->selectedFeatureIds().isEmpty()) return;
    m_clipboard->replaceWithCopyOf(vl);
    vl->beginEditCommand(tr("Cut features"));
    vl->deleteSelectedFeatures();
    vl->endEditCommand();
}

void QgisDesktopWindow::copyFeatures()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (!vl) return;
    if (vl->selectedFeatureIds().isEmpty()) return;
    m_clipboard->replaceWithCopyOf(vl);
    statusBar()->showMessage(tr("Copied %1 feature(s)").arg(vl->selectedFeatureCount()), 2000);
}

void QgisDesktopWindow::pasteFeatures()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (!vl || !vl->isEditable()) {
        statusBar()->showMessage(tr("Select an editable vector layer to paste"), 2000);
        return;
    }
    QgsFeatureList features = m_clipboard->transformedCopyOf(vl->crs(), vl->fields());
    if (features.isEmpty()) return;
    vl->beginEditCommand(tr("Paste features"));
    for (QgsFeature &f : features) {
        f.setId(FID_NULL);
        vl->addFeature(f);
    }
    vl->endEditCommand();
    statusBar()->showMessage(tr("Pasted %1 feature(s)").arg(features.size()), 2000);
}

void QgisDesktopWindow::selectAll()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (vl) {
        vl->selectAll();
        statusBar()->showMessage(tr("Selected %1 feature(s)").arg(vl->selectedFeatureCount()), 2000);
    }
}
// ── Vector Editing Actions ─────────────────────────────────────────────────

void QgisDesktopWindow::newVectorLayer()
{
    QString errorMessage;
    QString enc;
    QString fileName = QgsNewVectorLayerDialog::execAndCreateLayer(errorMessage, this, QString(), &enc);
    if (fileName.isEmpty())
        return;

    // Register and display through the project Data Context so the new vector
    // layer becomes a Data Asset with a main-view Display Layer.
    const auto loaded = m_activeViewHost->loadLayer( fileName );
    if ( loaded )
    {
        statusBar()->showMessage(tr("Created new layer: %1").arg(fileName), 5000);
    }
    else
    {
        QMessageBox::warning(this, tr("New Vector Layer"), tr("Failed to create layer: %1").arg(errorMessage));
    }
}

QgsVectorLayer *QgisDesktopWindow::currentVectorLayer()
{
    QgsMapLayer *layer = m_mapCanvas->currentLayer();
    return qobject_cast<QgsVectorLayer*>(layer);
}

void QgisDesktopWindow::updateEditingUI(QgsVectorLayer *vlayer)
{
    bool editing = vlayer && vlayer->isEditable();
    if (m_toggleEditingAction) m_toggleEditingAction->setChecked(editing);
    if (m_saveEditsAction) m_saveEditsAction->setEnabled(editing);
    for (QAction *a : m_editingToolActions)
        a->setEnabled(editing);
}

bool QgisDesktopWindow::confirmSaveEdits(QgsVectorLayer *vl)
{
    if (!vl || !vl->isEditable())
        return true;

    if (vl->isModified()) {
        int res = QMessageBox::question(this, tr("Unsaved Edits"),
            tr("Layer '%1' has unsaved edits. Save changes before switching?").arg(vl->name()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (res == QMessageBox::Cancel)
            return false;
        if (res == QMessageBox::Save)
            vl->commitChanges();
        else
            vl->rollBack();
    } else {
        vl->rollBack();
    }
    updateEditingUI(vl);
    return true;
}

void QgisDesktopWindow::toggleEditing()
{
    QgsVectorLayer *vlayer = currentVectorLayer();
    if (!vlayer) {
        // Check if any layer is selected in the tree
        for (QgsMapLayer *layer : selectedLayers()) {
            vlayer = qobject_cast<QgsVectorLayer*>(layer);
            if (vlayer) break;
        }
    }
    if (!vlayer) {
        statusBar()->showMessage(tr("No vector layer selected"), 3000);
        return;
    }

    if (vlayer->isEditable()) {
        if (vlayer->isModified()) {
            int res = QMessageBox::question(this, tr("Stop Editing"),
                tr("Do you want to save changes to %1?").arg(vlayer->name()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (res == QMessageBox::Cancel) return;
            if (res == QMessageBox::Save) {
                if (vlayer->commitChanges())
                    commitEditLease(vlayer);
            }
            else {
                vlayer->rollBack();
                rollbackEditLease(vlayer);
            }
        }
        else
        {
            vlayer->rollBack();
            rollbackEditLease(vlayer);
        }
        vlayer->triggerRepaint();
    } else {
        // Acquire the exclusive Edit Lease before entering edit mode. A Display
        // Layer backed by a Data Asset may only be edited by one view at a time;
        // layers without an Asset ID (external/standard) keep QGIS behavior.
        if (!acquireEditLease(vlayer))
            return;
        vlayer->startEditing();
    }

    updateEditingUI(vlayer);
    statusBar()->showMessage(tr("%1 editing %2")
        .arg(vlayer->isEditable() ? "Started" : "Stopped")
        .arg(vlayer->name()), 3000);
}

void QgisDesktopWindow::saveEdits()
{
    QgsVectorLayer *vlayer = currentVectorLayer();
    if (!vlayer || !vlayer->isEditable()) return;

    if (!vlayer->commitChanges()) {
        QMessageBox::warning(this, tr("Save Edits"),
            tr("Failed to save edits: %1").arg(vlayer->commitErrors().join("\n")));
    }
    else {
        // Committing advances the Asset Revision and releases the Edit Lease;
        // re-acquire it because saving stays in edit mode.
        commitEditLease(vlayer);
        ( void ) acquireEditLease(vlayer, /*showConflictWarning=*/false);
    }
    vlayer->startEditing();
    statusBar()->showMessage(tr("Edits saved for %1").arg(vlayer->name()), 3000);
}

void QgisDesktopWindow::addFeature() { m_mapCanvas->setMapTool(m_addFeatureTool); }
void QgisDesktopWindow::moveFeature() { m_mapCanvas->setMapTool(m_moveFeatureTool); }
void QgisDesktopWindow::rotateFeature() { m_mapCanvas->setMapTool(m_rotateFeatureTool); }
void QgisDesktopWindow::scaleFeature() { m_mapCanvas->setMapTool(m_scaleFeatureTool); }
void QgisDesktopWindow::offsetCurve() { m_mapCanvas->setMapTool(m_offsetCurveTool); }
void QgisDesktopWindow::reshapeGeometry() { m_mapCanvas->setMapTool(m_reshapeTool); }
void QgisDesktopWindow::splitFeatures() { m_mapCanvas->setMapTool(m_splitFeaturesTool); }
void QgisDesktopWindow::splitParts() { m_mapCanvas->setMapTool(m_splitPartsTool); }
void QgisDesktopWindow::simplifyFeature() { m_mapCanvas->setMapTool(m_simplifyTool); }
void QgisDesktopWindow::reverseLine() { m_mapCanvas->setMapTool(m_reverseLineTool); }
void QgisDesktopWindow::addRing() { m_mapCanvas->setMapTool(m_addRingTool); }
void QgisDesktopWindow::addPart() { m_mapCanvas->setMapTool(m_addPartTool); }
void QgisDesktopWindow::fillRing() { m_mapCanvas->setMapTool(m_fillRingTool); }
void QgisDesktopWindow::deletePart() { m_mapCanvas->setMapTool(m_deletePartTool); }
void QgisDesktopWindow::deleteRing() { m_mapCanvas->setMapTool(m_deleteRingTool); }
void QgisDesktopWindow::trimExtendFeature() { m_mapCanvas->setMapTool(m_trimExtendTool); }
void QgisDesktopWindow::chamferFillet() { m_mapCanvas->setMapTool(m_chamferFilletTool); }
void QgisDesktopWindow::featureArray() { m_mapCanvas->setMapTool(m_featureArrayTool); }
void QgisDesktopWindow::vertexTool() { m_mapCanvas->setMapTool(m_vertexTool); }
void QgisDesktopWindow::selectFeatures() { m_mapCanvas->setMapTool(m_selectTool); }

void QgisDesktopWindow::deleteSelectedFeatures()
{
    QgsVectorLayer *vlayer = currentVectorLayer();
    if (!vlayer) {
        // Try selected layers from tree
        for (QgsMapLayer *layer : selectedLayers()) {
            vlayer = qobject_cast<QgsVectorLayer*>(layer);
            if (vlayer) break;
        }
    }
    if (!vlayer || !vlayer->isEditable()) {
        statusBar()->showMessage(tr("Select an editable vector layer first"), 3000);
        return;
    }
    const QgsFeatureIds &ids = vlayer->selectedFeatureIds();
    if (ids.isEmpty()) {
        statusBar()->showMessage(tr("No features selected"), 3000);
        return;
    }
    vlayer->deleteFeatures(ids);
    statusBar()->showMessage(tr("Deleted %1 feature(s)").arg(ids.size()), 3000);
}

void QgisDesktopWindow::openAttributeTable()
{
    QgsVectorLayer *vlayer = currentVectorLayer();
    if (!vlayer) {
        for (QgsMapLayer *layer : selectedLayers()) {
            vlayer = qobject_cast<QgsVectorLayer*>(layer);
            if (vlayer) break;
        }
    }
    if (!vlayer) {
        statusBar()->showMessage(tr("No vector layer selected"), 3000);
        return;
    }
    QgsAttributeTableDialog *dlg = new QgsAttributeTableDialog(vlayer, QgsAttributeTableFilterModel::ShowAll, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// --- Edit Lease helpers -----------------------------------------------------
// A Display Layer backed by a Data Asset carries its Asset ID as a custom
// property. Editing such a layer requires the exclusive Edit Lease from the
// Data Manager; layers without an Asset ID keep native QGIS editing behavior.
//
// The layer that acquires the Edit Lease is the edit owner and stays editable.
// Every other Display Layer of the same asset is read-only for the duration of
// the edit session, per the one-edit-lease-per-asset rule.

void QgisDesktopWindow::setAssetLayersReadOnly( const sicnu::data::AssetId &assetId,
                                                QgsVectorLayer *exceptLayer,
                                                bool readOnly )
{
    if ( assetId.isNull() )
        return;

    for ( QgsMapLayer *layer : QgsProject::instance()->mapLayers() )
    {
        auto *otherVector = qobject_cast<QgsVectorLayer *>( layer );
        if ( !otherVector || otherVector == exceptLayer )
            continue;
        const auto otherAsset = sicnu::data::AssetId::fromString(
            otherVector->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() );
        if ( otherAsset && *otherAsset == assetId )
            otherVector->setReadOnly( readOnly );
    }
}

bool QgisDesktopWindow::acquireEditLease( QgsVectorLayer *vlayer,
                                          bool showConflictWarning )
{
    if ( !m_projectContext || !vlayer )
        return true; // No data context: nothing to lease against.

    const auto assetId = sicnu::data::AssetId::fromString(
        vlayer->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() );
    if ( !assetId )
        return true; // External / standard layer: native QGIS editing.

    sicnu::data::DataManager &dataManager = m_projectContext->dataManager();
    const auto asset = dataManager.asset( *assetId );
    if ( !asset )
        return true;

    auto acquired = dataManager.acquire(
        sicnu::data::AssetRef{ *assetId, asset->revision() },
        sicnu::data::AssetUse{ sicnu::data::LeaseKind::Edit,
                               QStringLiteral( "Vector edit session" ) } );
    if ( acquired )
    {
        // This layer becomes the edit owner; all other Display Layers of the
        // same asset become read-only.
        setAssetLayersReadOnly( *assetId, vlayer, true );
        return true;
    }

    if ( showConflictWarning )
    {
        QMessageBox::warning(
            this, tr( "Start Editing" ),
            tr( "%1 is being edited in another view.\n"
                "Commit or roll back that edit session before editing here." )
                .arg( vlayer->name() ) );
    }
    return false;
}

void QgisDesktopWindow::commitEditLease( QgsVectorLayer *vlayer )
{
    if ( !m_projectContext || !vlayer )
        return;

    const auto assetId = sicnu::data::AssetId::fromString(
        vlayer->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() );
    if ( !assetId )
        return;

    ( void ) m_projectContext->dataManager().commitEdit( *assetId );
    // The edit session ended; other Display Layers of the asset are editable again.
    setAssetLayersReadOnly( *assetId, nullptr, false );
}

void QgisDesktopWindow::rollbackEditLease( QgsVectorLayer *vlayer )
{
    if ( !m_projectContext || !vlayer )
        return;

    const auto assetId = sicnu::data::AssetId::fromString(
        vlayer->customProperty( QStringLiteral( "sicnu/assetId" ) ).toString() );
    if ( !assetId )
        return;

    ( void ) m_projectContext->dataManager().rollbackEdit( *assetId );
    setAssetLayersReadOnly( *assetId, nullptr, false );
}
