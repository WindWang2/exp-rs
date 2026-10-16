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
                // Save all modified vector layers — fail closed on commit
                // errors (#1084): a dropped return used to discard edits silently.
                for (QgsMapLayer *l : layers)
                {
                    QgsVectorLayer *v = qobject_cast<QgsVectorLayer *>(l);
                    if (v && v->isEditable() && v->isModified())
                    {
                        if ( !v->commitChanges() )
                        {
                            QMessageBox::warning(
                                this, tr( "Save Edits" ),
                                tr( "Failed to save edits on layer '%1':\n%2" )
                                    .arg( v->name(),
                                          v->commitErrors().join( QLatin1Char( '\n' ) ) ) );
                            return false;
                        }
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
    if (vl->selectedFeatureIds().isEmpty()) {
        statusBar()->showMessage(tr("No features selected to cut"), 2000);
        return;
    }
    const int cutCount = vl->selectedFeatureCount();
    m_clipboard->replaceWithCopyOf(vl);
    vl->beginEditCommand(tr("Cut features"));
    const bool deleted = vl->deleteSelectedFeatures();
    if (deleted) {
        vl->endEditCommand();
        statusBar()->showMessage(tr("Cut %1 feature(s)").arg(cutCount), 2000);
    } else {
        // The clipboard holds a copy but the layer still contains the
        // features: claiming "Cut N" here is the classic silent partial
        // failure of the cut contract — report the delete half honestly
        // instead (deliberately without a feature count: the clipboard copy
        // may hold fewer than the selection, e.g. stale deleted features).
        vl->destroyEditCommand();
        QMessageBox::warning(this, tr("Cut Features"),
            tr("Failed to remove the cut features from layer '%1'.\n"
               "The clipboard holds a copy of the selection; the layer still contains the features.")
                .arg(vl->name()));
        statusBar()->showMessage(
            tr("Cut failed — a copy is on the clipboard but the features were not removed from %1")
                .arg(vl->name()), 5000);
    }
}

void QgisDesktopWindow::copyFeatures()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (!vl) {
        statusBar()->showMessage(tr("Select a vector layer to copy features"), 2000);
        return;
    }
    if (vl->selectedFeatureIds().isEmpty()) {
        statusBar()->showMessage(tr("No features selected to copy"), 2000);
        return;
    }
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
    if (features.isEmpty()) {
        statusBar()->showMessage(tr("Clipboard contains no compatible features to paste"), 2000);
        return;
    }
    vl->beginEditCommand(tr("Paste features"));
    int landed = 0;
    for (QgsFeature &f : features) {
        f.setId(FID_NULL);
        if (vl->addFeature(f))
            ++landed;
    }
    if (landed > 0)
        vl->endEditCommand();
    else
        vl->destroyEditCommand(); // nothing landed: no undo step, no "Pasted N"
    if (landed == features.size()) {
        statusBar()->showMessage(tr("Pasted %1 feature(s)").arg(landed), 2000);
    } else {
        QMessageBox::warning(this, tr("Paste Features"),
            tr("Pasted %1 of %2 feature(s) from the clipboard. Features with "
               "incompatible attributes or geometries were rejected by the layer.")
                .arg(landed).arg(features.size()));
        statusBar()->showMessage(
            tr("Pasted %1 of %2 feature(s)").arg(landed).arg(features.size()), 5000);
    }
}

void QgisDesktopWindow::selectAll()
{
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer*>(activeLayer());
    if (vl) {
        vl->selectAll();
        statusBar()->showMessage(tr("Selected %1 feature(s)").arg(vl->selectedFeatureCount()), 2000);
    } else {
        statusBar()->showMessage(tr("Select a vector layer to select features"), 2000);
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

QAction *QgisDesktopWindow::editingAction( const QString &name ) const
{
    for ( QAction *a : m_editingToolActions )
    {
        if ( a && a->objectName() == name )
            return a;
    }
    return nullptr;
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
        {
            // #1084: match toggleEditing()/saveEdits() — surface commitErrors
            // and abort the switch instead of pretending the save succeeded.
            if ( !vl->commitChanges() )
            {
                QMessageBox::warning(
                    this, tr( "Save Edits" ),
                    tr( "Failed to save edits on layer '%1':\n%2" )
                        .arg( vl->name(),
                              vl->commitErrors().join( QLatin1Char( '\n' ) ) ) );
                return false;
            }
        }
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
                if (vlayer->commitChanges()) {
                    commitEditLease(vlayer);
                } else {
                    // #1084 pattern (saveEdits()/confirmSaveEdits()): surface
                    // commitErrors() instead of falling through. QGIS keeps
                    // the layer in edit mode when a commit fails, so the
                    // status must not claim "Stopped editing" while the
                    // edits are still buffered.
                    const QString commitDetails = vlayer->commitErrors().isEmpty()
                        ? tr("The data source rejected the commit.")
                        : vlayer->commitErrors().join(QLatin1Char('\n'));
                    QMessageBox::warning(
                        this, tr("Save Edits"),
                        tr("Failed to save edits on layer '%1':\n%2")
                            .arg(vlayer->name(), commitDetails));
                    updateEditingUI(vlayer);
                    statusBar()->showMessage(
                        tr("Commit failed — still editing %1 (edits are buffered, not saved)")
                            .arg(vlayer->name()), 5000);
                    return;
                }
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
        // APP-9A (#1084 family): the failed commit must not fall through to
        // the "Edits saved" status below — the layer is still in edit mode
        // with the edits buffered, and the status bar must say that.
        statusBar()->showMessage(
            tr("Failed to save edits for %1 — edits are still buffered"),
            5000);
        return;
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
    // Copy by value: deleteFeatures() subtracts the ids from the layer's own
    // selection set, which this reference would alias ("Deleted 0" forever).
    const QgsFeatureIds ids = vlayer->selectedFeatureIds();
    if (ids.isEmpty()) {
        statusBar()->showMessage(tr("No features selected"), 3000);
        return;
    }
    // One edit command for the whole batch: a single "Delete features" undo
    // step (the buffer deletes feature-by-feature otherwise), destroyed and
    // rolled back wholesale when the provider rejects the deletes.
    vlayer->beginEditCommand(tr("Delete features"));
    if (vlayer->deleteFeatures(ids)) {
        vlayer->endEditCommand();
        statusBar()->showMessage(tr("Deleted %1 feature(s)").arg(ids.size()), 3000);
    } else {
        vlayer->destroyEditCommand();
        QMessageBox::warning(this, tr("Delete Features"),
            tr("Failed to delete the selected features from layer '%1'.\n"
               "The delete was rolled back — the layer still contains the features.")
                .arg(vlayer->name()));
        statusBar()->showMessage(
            tr("Failed to delete %1 feature(s) from %2 — the features are still in the layer")
                .arg(ids.size()).arg(vlayer->name()), 5000);
    }
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
