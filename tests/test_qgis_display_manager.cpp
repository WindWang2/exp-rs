#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaplayerstore.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgsvectorlayer.h>

#include "app/display/qgis_display_manager.h"
#include "data/data_asset.h"
#include "data/data_manager.h"

using sicnu::data::AssetLease;
using sicnu::data::AssetRef;
using sicnu::data::AssetState;
using sicnu::data::AssetUse;
using sicnu::data::DataManager;
using sicnu::data::LeaseKind;
using sicnu::data::RegisterRequest;
using sicnu::data::RelocateRequest;
using sicnu::data::SourceDescriptor;
using sicnu::display::DisplayLayerId;
using sicnu::display::DisplayViewId;
using sicnu::display::DisplayViewSpec;
using sicnu::display::QgisDisplayManager;

namespace {

void ensureQgisApplication() {
  if (QCoreApplication::instance())
    return;

  static int argc = 1;
  static char applicationName[] = "test_qgis_display_manager";
  static char *argv[] = {applicationName, nullptr};
  static auto *application = new QgsApplication(argc, argv, true);
  (void)application;
  QgsApplication::initQgis();
}

QString fixturePath(const QString &relative) {
  const QString here = QFileInfo(__FILE__).absolutePath();
  return QFileInfo(here + QStringLiteral("/../data/") + relative)
      .absoluteFilePath();
}

sicnu::data::AssetId registerRasterAt(DataManager &manager,
                                       const QString &relative) {
  SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource = fixturePath(relative);
  const auto registered = manager.registerSource(RegisterRequest{source});
  REQUIRE_FALSE(registered.assetId.isNull());
  return registered.assetId;
}

sicnu::data::AssetId registerRaster(DataManager &manager) {
  return registerRasterAt(manager, QStringLiteral("samples/dem_sample.tif"));
}

sicnu::data::AssetId registerVector(DataManager &manager) {
  SourceDescriptor source;
  source.providerKey = QStringLiteral("ogr");
  source.canonicalSource = fixturePath(QStringLiteral("test_vectors.geojson"));
  const auto registered = manager.registerSource(RegisterRequest{source});
  REQUIRE_FALSE(registered.assetId.isNull());
  return registered.assetId;
}

DisplayViewId createView(QgisDisplayManager &manager, QgsMapCanvas &canvas,
                         QgsLayerTree &tree, QgsMapLayerStore &store) {
  DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &tree;
  spec.layerStore = &store;
  const auto created = manager.createView(spec);
  REQUIRE(created);
  return created.value();
}

} // namespace

TEST_CASE("A QGIS display view registers its canvas tree and layer store",
          "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);

  DisplayViewSpec spec;
  spec.canvas = &canvas;
  spec.layerTree = &layerTree;
  spec.layerStore = &layerStore;
  const auto created = displayManager.createView(spec);

  REQUIRE(created);
  CHECK_FALSE(created.value().isNull());
  const auto snapshot = displayManager.view(created.value());
  REQUIRE(snapshot.has_value());
  CHECK(snapshot->id() == created.value());
  CHECK(snapshot->layerIds().isEmpty());
}

TEST_CASE("Adding the same Data Asset creates independent QGIS Display Layers",
          "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);
  const sicnu::data::AssetId assetId = registerRaster(dataManager);

  const auto firstAdded = displayManager.addLayer(viewId, assetId);
  const auto secondAdded = displayManager.addLayer(viewId, assetId);
  REQUIRE(firstAdded);
  REQUIRE(secondAdded);
  CHECK_FALSE(firstAdded.value() == secondAdded.value());

  QgsMapLayer *first = displayManager.mapLayer(firstAdded.value());
  QgsMapLayer *second = displayManager.mapLayer(secondAdded.value());
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  CHECK(first != second);
  CHECK(first->id() != second->id());
  CHECK(layerStore.count() == 2);
  CHECK(layerTree.findLayers().size() == 2);
  CHECK(canvas.layers().size() == 2);
  CHECK(dataManager.leaseCount(assetId) == 2);

  CHECK(first->customProperty(QStringLiteral("sicnu/assetId")).toString() ==
        assetId.toString());
  CHECK(first->customProperty(QStringLiteral("sicnu/displayLayerId"))
            .toString() == firstAdded.value().toString());
  CHECK(
      first->customProperty(QStringLiteral("sicnu/displayViewId")).toString() ==
      viewId.toString());

  const auto firstSnapshot = displayManager.layer(firstAdded.value());
  const auto secondSnapshot = displayManager.layer(secondAdded.value());
  REQUIRE(firstSnapshot);
  REQUIRE(secondSnapshot);
  CHECK(firstSnapshot->viewId() == viewId);
  CHECK(secondSnapshot->viewId() == viewId);
  CHECK(firstSnapshot->assetId() == assetId);
  CHECK(secondSnapshot->assetId() == assetId);

  auto *firstRaster = qobject_cast<QgsRasterLayer *>(first);
  auto *secondRaster = qobject_cast<QgsRasterLayer *>(second);
  REQUIRE(firstRaster != nullptr);
  REQUIRE(secondRaster != nullptr);
  REQUIRE(firstRaster->renderer() != nullptr);
  REQUIRE(secondRaster->renderer() != nullptr);
  firstRaster->renderer()->setOpacity(0.25);
  CHECK(firstRaster->renderer()->opacity() == 0.25);
  CHECK(secondRaster->renderer()->opacity() == 1.0);

  const auto viewSnapshot = displayManager.view(viewId);
  REQUIRE(viewSnapshot);
  REQUIRE(viewSnapshot->layerIds().size() == 2);
  CHECK(viewSnapshot->layerIds().at(0) == secondAdded.value());
  CHECK(viewSnapshot->layerIds().at(1) == firstAdded.value());
}

TEST_CASE("Removing a Display Layer releases only its view lease",
          "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);
  const sicnu::data::AssetId assetId = registerRaster(dataManager);
  const auto added = displayManager.addLayer(viewId, assetId);
  REQUIRE(added);
  REQUIRE(dataManager.leaseCount(assetId) == 1);

  const auto removed = displayManager.removeLayer(added.value());
  REQUIRE(removed);
  CHECK(dataManager.leaseCount(assetId) == 0);
  CHECK(dataManager.asset(assetId).has_value());
  CHECK(layerStore.count() == 0);
  CHECK(layerTree.findLayers().isEmpty());
  CHECK(canvas.layers().isEmpty());
  CHECK_FALSE(displayManager.layer(added.value()).has_value());

  const auto viewSnapshot = displayManager.view(viewId);
  REQUIRE(viewSnapshot);
  CHECK(viewSnapshot->layerIds().isEmpty());
}

TEST_CASE("Vector Data Assets materialize through the QGIS OGR adapter",
          "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);
  const sicnu::data::AssetId assetId = registerVector(dataManager);

  const auto added = displayManager.addLayer(viewId, assetId);
  REQUIRE(added);
  auto *vectorLayer =
      qobject_cast<QgsVectorLayer *>(displayManager.mapLayer(added.value()));
  REQUIRE(vectorLayer != nullptr);
  CHECK(vectorLayer->isValid());
  CHECK(vectorLayer->featureCount() == 3);
  CHECK(dataManager.leaseCount(assetId) == 1);
}

TEST_CASE(
    "Cloning preserves presentation while keeping runtime state independent",
    "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas firstCanvas;
  QgsLayerTree firstTree;
  QgsMapLayerStore firstStore;
  QgsMapCanvas secondCanvas;
  QgsLayerTree secondTree;
  QgsMapLayerStore secondStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId firstView =
      createView(displayManager, firstCanvas, firstTree, firstStore);
  const DisplayViewId secondView =
      createView(displayManager, secondCanvas, secondTree, secondStore);
  const sicnu::data::AssetId assetId = registerRaster(dataManager);
  const auto added = displayManager.addLayer(firstView, assetId);
  REQUIRE(added);

  auto *source =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(added.value()));
  REQUIRE(source != nullptr);
  REQUIRE(source->renderer() != nullptr);
  source->renderer()->setOpacity(0.4);

  const auto cloned = displayManager.cloneLayer(added.value(), secondView);
  REQUIRE(cloned);
  auto *copy =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(cloned.value()));
  REQUIRE(copy != nullptr);
  REQUIRE(copy->renderer() != nullptr);
  CHECK(copy != source);
  CHECK(copy->renderer() != source->renderer());
  CHECK(copy->renderer()->opacity() == 0.4);
  CHECK(dataManager.leaseCount(assetId) == 2);

  source->renderer()->setOpacity(0.7);
  CHECK(copy->renderer()->opacity() == 0.4);
  const auto clonedSnapshot = displayManager.layer(cloned.value());
  REQUIRE(clonedSnapshot);
  CHECK(clonedSnapshot->viewId() == secondView);
  CHECK(firstStore.count() == 1);
  CHECK(secondStore.count() == 1);
}

TEST_CASE("Invalid and Missing assets return structured display diagnostics",
          "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);

  const auto unknown = displayManager.addLayer(viewId, sicnu::data::AssetId{});
  REQUIRE_FALSE(unknown);
  REQUIRE_FALSE(unknown.diagnostics().isEmpty());
  CHECK(unknown.diagnostics().first().code ==
        QStringLiteral("display.asset_not_found"));

  SourceDescriptor missingSource;
  missingSource.providerKey = QStringLiteral("gdal");
  missingSource.canonicalSource =
      fixturePath(QStringLiteral("does-not-exist.tif"));
  const auto registered =
      dataManager.registerSource(RegisterRequest{missingSource});
  REQUIRE_FALSE(registered.assetId.isNull());
  const auto missingAsset = dataManager.asset(registered.assetId);
  REQUIRE(missingAsset);
  REQUIRE(missingAsset->state() == AssetState::Missing);

  const auto missing = displayManager.addLayer(viewId, registered.assetId);
  REQUIRE_FALSE(missing);
  REQUIRE_FALSE(missing.diagnostics().isEmpty());
  CHECK(missing.diagnostics().first().code ==
        QStringLiteral("display.asset_not_ready"));

  const auto badView =
      displayManager.addLayer(DisplayViewId{}, registered.assetId);
  REQUIRE_FALSE(badView);
  REQUIRE_FALSE(badView.diagnostics().isEmpty());
  CHECK(badView.diagnostics().first().code ==
        QStringLiteral("display.invalid_view"));
}

TEST_CASE(
    "Cascade unload removes affected Display Layers before the Data Asset",
    "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);
  const sicnu::data::AssetId assetId = registerRaster(dataManager);
  const auto added = displayManager.addLayer(viewId, assetId);
  REQUIRE(added);

  const auto unloaded =
      dataManager.unload(dataManager.planUnload(assetId).confirmedCascade());
  REQUIRE(unloaded);
  CHECK_FALSE(dataManager.asset(assetId).has_value());
  CHECK_FALSE(displayManager.layer(added.value()).has_value());
  CHECK(layerStore.count() == 0);
}

TEST_CASE(
    "Relocating an asset recreates the QGIS layer but keeps display identity and renderer",
    "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);

  // Copy the raster fixture to a second location with identical structure so the
  // relocation validates as compatible.
  QTemporaryDir tempDir;
  REQUIRE(tempDir.isValid());
  const QString originalPath = fixturePath(QStringLiteral("samples/dem_sample.tif"));
  const QString movedPath = tempDir.filePath(QStringLiteral("dem_sample.tif"));
  REQUIRE(QFile::copy(originalPath, movedPath));

  SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource = originalPath;
  const auto registered = dataManager.registerSource(RegisterRequest{source});
  REQUIRE_FALSE(registered.assetId.isNull());
  const sicnu::data::AssetId assetId = registered.assetId;

  const auto added = displayManager.addLayer(viewId, assetId);
  REQUIRE(added);
  const DisplayLayerId layerId = added.value();

  QgsMapLayer *beforeLayer = displayManager.mapLayer(layerId);
  REQUIRE(beforeLayer != nullptr);
  auto *beforeRaster = qobject_cast<QgsRasterLayer *>(beforeLayer);
  REQUIRE(beforeRaster != nullptr);

  // Make the presentation state distinctive so we can verify it survives.
  beforeLayer->setOpacity(0.42);

  const QString beforeQgisLayerId = beforeLayer->id();

  // Relocate the asset's source to the moved copy. The Data Manager emits
  // assetChanged, which the Display Manager observes to rebuild the layer.
  RelocateRequest relocate;
  relocate.id = assetId;
  relocate.replacement = SourceDescriptor{QStringLiteral("gdal"), movedPath, {}, {}, {}};
  const auto relocated = dataManager.relocate(relocate);
  REQUIRE(relocated);

  // Display identity is preserved: same DisplayLayerId, still exactly one layer.
  const auto snapshot = displayManager.layer(layerId);
  REQUIRE(snapshot.has_value());
  CHECK(snapshot->assetId() == assetId);
  const auto viewSnapshot = displayManager.view(viewId);
  REQUIRE(viewSnapshot.has_value());
  CHECK(viewSnapshot->layerIds().size() == 1);
  CHECK(viewSnapshot->layerIds().first() == layerId);

  // The QGIS layer was recreated as a distinct instance with a new layer id.
  QgsMapLayer *afterLayer = displayManager.mapLayer(layerId);
  REQUIRE(afterLayer != nullptr);
  CHECK(afterLayer != beforeLayer);
  CHECK(afterLayer->id() != beforeQgisLayerId);
  CHECK(afterLayer->isValid());

  // The renderer/presentation state survived the replacement.
  CHECK(afterLayer->opacity() == 0.42);

  // Only the replacement layer remains in the store.
  CHECK(layerStore.count() == 1);
  CHECK(layerStore.mapLayer(afterLayer->id()) == afterLayer);
}

TEST_CASE("Relocating an asset that stays missing keeps the Display Layer identity",
          "[qgis_display_manager]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);

  // Copy the raster to a temp location, register, display, then delete the file
  // so a later relocation targets a missing source.
  QTemporaryDir tempDir;
  REQUIRE(tempDir.isValid());
  const QString originalPath = fixturePath(QStringLiteral("samples/dem_sample.tif"));
  const QString movedPath = tempDir.filePath(QStringLiteral("dem_sample.tif"));
  REQUIRE(QFile::copy(originalPath, movedPath));

  SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource = originalPath;
  const auto registered = dataManager.registerSource(RegisterRequest{source});
  REQUIRE_FALSE(registered.assetId.isNull());
  const sicnu::data::AssetId assetId = registered.assetId;

  const auto added = displayManager.addLayer(viewId, assetId);
  REQUIRE(added);
  const DisplayLayerId layerId = added.value();

  // Remove the moved copy so the relocation targets a missing source. The
  // provider cannot resolve a structure for a missing file, so the relocation is
  // rejected as structurally incompatible — the asset and its Display Layer are
  // left untouched.
  REQUIRE(QFile::remove(movedPath));

  RelocateRequest relocate;
  relocate.id = assetId;
  relocate.replacement = SourceDescriptor{QStringLiteral("gdal"), movedPath, {}, {}, {}};
  const auto relocated = dataManager.relocate(relocate);

  // The relocation is rejected; the catalog and the Display Layer are unchanged.
  REQUIRE_FALSE(relocated);
  CHECK(relocated.diagnostics().first().code ==
        QStringLiteral("relocate.structure_mismatch"));
  const auto snapshot = displayManager.layer(layerId);
  REQUIRE(snapshot.has_value());
  CHECK(snapshot->assetId() == assetId);
  CHECK(displayManager.mapLayer(layerId) != nullptr);
}

TEST_CASE("A Display Layer created while the asset is being edited is read-only",
          "[qgis_display_manager][edit_lease]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvas;
  QgsLayerTree layerTree;
  QgsMapLayerStore layerStore;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewId =
      createView(displayManager, canvas, layerTree, layerStore);
  const sicnu::data::AssetId assetId = registerVector(dataManager);

  // Before any edit session, a new Display Layer is writable.
  const auto ownerDisplay = displayManager.addLayer(viewId, assetId);
  REQUIRE(ownerDisplay);
  auto *ownerLayer = qobject_cast<QgsVectorLayer *>(
      displayManager.mapLayer(ownerDisplay.value()));
  REQUIRE(ownerLayer != nullptr);
  CHECK_FALSE(ownerLayer->readOnly());

  // The owner begins an edit session (acquires the exclusive Edit Lease).
  AssetLease editLease = dataManager
                             .acquire(AssetRef{assetId},
                                      AssetUse{LeaseKind::Edit,
                                               QStringLiteral("edit session")})
                             .take();
  REQUIRE(editLease.isValid());
  CHECK(dataManager.hasActiveEditLease(assetId));

  // A second Display Layer of the same asset created during the edit session is
  // read-only: only the Edit Lease owner may modify features.
  const auto nonOwnerDisplay = displayManager.addLayer(viewId, assetId);
  REQUIRE(nonOwnerDisplay);
  auto *nonOwnerLayer = qobject_cast<QgsVectorLayer *>(
      displayManager.mapLayer(nonOwnerDisplay.value()));
  REQUIRE(nonOwnerLayer != nullptr);
  CHECK(nonOwnerLayer->readOnly());

  // A second Edit Lease on the same asset is rejected while the owner edits.
  const auto secondEdit = dataManager.acquire(
      AssetRef{assetId}, AssetUse{LeaseKind::Edit, QStringLiteral("other view")});
  REQUIRE_FALSE(secondEdit);
  CHECK(secondEdit.diagnostics().first().code ==
        QStringLiteral("asset.edit_lease_conflict"));

  // After the owner commits, the Edit Lease is released and a new Display Layer
  // is writable again; the commit also advanced the revision.
  REQUIRE(dataManager.commitEdit(assetId));
  CHECK_FALSE(dataManager.hasActiveEditLease(assetId));

  const auto afterCommitDisplay = displayManager.addLayer(viewId, assetId);
  REQUIRE(afterCommitDisplay);
  auto *afterCommitLayer = qobject_cast<QgsVectorLayer *>(
      displayManager.mapLayer(afterCommitDisplay.value()));
  REQUIRE(afterCommitLayer != nullptr);
  CHECK_FALSE(afterCommitLayer->readOnly());
}

TEST_CASE("createView emits viewAdded and listViews reports creation order",
          "[qgis_display_manager][multi_view]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgisDisplayManager displayManager(&dataManager);

  QSignalSpy addedSpy(&displayManager, &QgisDisplayManager::viewAdded);
  QgsMapCanvas canvasA, canvasB;
  QgsLayerTree treeA, treeB;
  QgsMapLayerStore storeA, storeB;
  const DisplayViewId first = createView(displayManager, canvasA, treeA, storeA);
  const DisplayViewId second = createView(displayManager, canvasB, treeB, storeB);

  // viewAdded fired once per create, carrying the new id.
  REQUIRE(addedSpy.count() == 2);
  CHECK(addedSpy.at(0).first().value<DisplayViewId>() == first);
  CHECK(addedSpy.at(1).first().value<DisplayViewId>() == second);

  // listViews reports the live ids in creation order (NOT UUID-string order).
  const QVector<DisplayViewId> live = displayManager.listViews();
  REQUIRE(live.size() == 2);
  CHECK(live.at(0) == first);
  CHECK(live.at(1) == second);
}

TEST_CASE("removeView drops its layers and leases, then the record",
          "[qgis_display_manager][multi_view]") {
  ensureQgisApplication();
  DataManager dataManager;
  // Declare the QGIS view objects before the display manager so the manager is
  // destroyed before them (the manager's destructor touches live canvases).
  QgsMapCanvas canvas;
  QgsLayerTree tree;
  QgsMapLayerStore store;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId view = createView(displayManager, canvas, tree, store);
  const sicnu::data::AssetId assetId = registerRaster(dataManager);
  const auto added = displayManager.addLayer(view, assetId);
  REQUIRE(added);
  REQUIRE(dataManager.leaseCount(assetId) == 1);

  QSignalSpy aboutSpy(&displayManager, &QgisDisplayManager::viewAboutToBeRemoved);
  QSignalSpy removedSpy(&displayManager, &QgisDisplayManager::viewRemoved);
  REQUIRE(displayManager.removeView(view));

  // viewAboutToBeRemoved fires before the record is gone (canvas still valid);
  // viewRemoved fires after. Both carry the id.
  REQUIRE(aboutSpy.count() == 1);
  CHECK(aboutSpy.at(0).first().value<DisplayViewId>() == view);
  REQUIRE(removedSpy.count() == 1);
  CHECK(removedSpy.at(0).first().value<DisplayViewId>() == view);

  // The view's layer (and its lease) was released; the view is gone.
  CHECK_FALSE(displayManager.view(view).has_value());
  CHECK(dataManager.leaseCount(assetId) == 0);
  CHECK(displayManager.listViews().isEmpty());
}

TEST_CASE("removeView refuses an unknown view",
          "[qgis_display_manager][multi_view]") {
  ensureQgisApplication();
  DataManager dataManager;
  QgisDisplayManager displayManager(&dataManager);

  const auto result = displayManager.removeView(DisplayViewId::generate());
  REQUIRE_FALSE(result);
  CHECK(result.diagnostics().first().code ==
        QStringLiteral("display.invalid_view"));
}

TEST_CASE("Removing a view whose asset is also shown in another view keeps the asset loaded",
          "[qgis_display_manager][multi_view]") {
  // The asset has layers in two views (two leases). Removing one view drops its
  // layer+lease but the other view's lease holds, so the asset stays loaded.
  ensureQgisApplication();
  DataManager dataManager;
  // Declare the QGIS view objects before the display manager so the manager
  // is destroyed before them (the manager's destructor touches live canvases).
  QgsMapCanvas canvasA, canvasB;
  QgsLayerTree treeA, treeB;
  QgsMapLayerStore storeA, storeB;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewA = createView(displayManager, canvasA, treeA, storeA);
  const DisplayViewId viewB = createView(displayManager, canvasB, treeB, storeB);
  const sicnu::data::AssetId assetId = registerRaster(dataManager);

  REQUIRE(displayManager.addLayer(viewA, assetId));
  REQUIRE(displayManager.addLayer(viewB, assetId));
  REQUIRE(dataManager.leaseCount(assetId) == 2);

  REQUIRE(displayManager.removeView(viewA));
  // viewA's lease released; viewB's lease still holds.
  CHECK(dataManager.leaseCount(assetId) == 1);
  CHECK(dataManager.asset(assetId).has_value());
  // viewB's layer is unaffected.
  CHECK(displayManager.view(viewB).has_value());
  CHECK(displayManager.view(viewB)->layerIds().size() == 1);
}

TEST_CASE("Adding one asset to two views via two independent addLayer calls "
          "produces independent layers with isolated renderers",
          "[qgis_display_manager][multi_view]") {
  // The core multi-view invariant: the SAME asset shown in two views through
  // two independent addLayer calls yields two distinct QgsMapLayers in two
  // stores, two leases, and per-view renderer isolation — exactly like
  // cloneLayer (the prior-art case at line 226). A regression here would
  // silently break the "same dataset, independent composition/renderer" use
  // case when layers are created via addLayer rather than clone.
  ensureQgisApplication();
  DataManager dataManager;
  // Declare the QGIS view objects before the display manager so the manager
  // is destroyed before them (the manager's destructor touches live canvases).
  QgsMapCanvas canvasA, canvasB;
  QgsLayerTree treeA, treeB;
  QgsMapLayerStore storeA, storeB;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewA = createView(displayManager, canvasA, treeA, storeA);
  const DisplayViewId viewB = createView(displayManager, canvasB, treeB, storeB);
  const sicnu::data::AssetId assetId = registerRaster(dataManager);

  // Two independent addLayer calls (NOT a clone) — each materializes its own
  // QgsMapLayer from the asset.
  const auto addedA = displayManager.addLayer(viewA, assetId);
  const auto addedB = displayManager.addLayer(viewB, assetId);
  REQUIRE(addedA);
  REQUIRE(addedB);

  // Two distinct DisplayLayerIds.
  CHECK(addedA.value() != addedB.value());
  // Each layer belongs to its own view.
  const auto snapA = displayManager.layer(addedA.value());
  const auto snapB = displayManager.layer(addedB.value());
  REQUIRE(snapA);
  REQUIRE(snapB);
  CHECK(snapA->viewId() == viewA);
  CHECK(snapB->viewId() == viewB);
  // Same underlying asset, different presentation instances.
  CHECK(snapA->assetId() == assetId);
  CHECK(snapB->assetId() == assetId);

  // Two distinct QgsMapLayers, in two distinct stores.
  auto *layerA =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(addedA.value()));
  auto *layerB =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(addedB.value()));
  REQUIRE(layerA != nullptr);
  REQUIRE(layerB != nullptr);
  CHECK(layerA != layerB);
  CHECK(storeA.count() == 1);
  CHECK(storeB.count() == 1);
  CHECK(storeA.mapLayer(layerA->id()) == layerA);
  CHECK(storeB.mapLayer(layerB->id()) == layerB);

  // Two leases on the asset (one per presentation instance).
  CHECK(dataManager.leaseCount(assetId) == 2);

  // Per-view renderer isolation: set opacity on A, B is unaffected. This is
  // the "true-color in one view, false-color in another" guarantee (opacity is
  // one axis of renderer state; band-composition independence is the broader
  // invariant but is out of scope for this test — the spec's Testing Decisions
  // bullet pins opacity, matching the prior-art cloneLayer case).
  REQUIRE(layerA->renderer() != nullptr);
  REQUIRE(layerB->renderer() != nullptr);
  CHECK(layerA->renderer() != layerB->renderer());
  // A freshly materialized renderer defaults to opacity 1.0.
  REQUIRE(layerA->renderer()->opacity() == 1.0);
  REQUIRE(layerB->renderer()->opacity() == 1.0);
  layerA->renderer()->setOpacity(0.3);
  // A's edit persists on A; B stays at its prior value (truly "unaffected",
  // not merely "not equal to 0.3").
  CHECK(layerA->renderer()->opacity() == 0.3);
  CHECK(layerB->renderer()->opacity() == 1.0);
  layerB->renderer()->setOpacity(0.8);
  CHECK(layerA->renderer()->opacity() == 0.3);
  CHECK(layerB->renderer()->opacity() == 0.8);
}

TEST_CASE("addLayer-twice and cloneLayer both yield leaseCount 2 and "
          "independent renderers for the same asset",
          "[qgis_display_manager][multi_view]") {
  // Locks that the core multi-view promise holds regardless of how the second
  // presentation was created (independent addLayer vs cloneLayer): both paths
  // land at leaseCount == 2 with independent renderers. If a future change
  // diverged the two paths (e.g. clone started sharing a layer), this test
  // would catch the asymmetry. Two DISTINCT fixture files are registered so the
  // two paths operate on two distinct Data Assets (registerSource dedups by
  // SourceKey, so the same fixture would collapse to one asset).
  ensureQgisApplication();
  DataManager dataManager;
  QgsMapCanvas canvasA, canvasB;
  QgsLayerTree treeA, treeB;
  QgsMapLayerStore storeA, storeB;
  QgisDisplayManager displayManager(&dataManager);
  const DisplayViewId viewA = createView(displayManager, canvasA, treeA, storeA);
  const DisplayViewId viewB = createView(displayManager, canvasB, treeB, storeB);

  // Path 1: two independent addLayer calls on assetAddTwice.
  const sicnu::data::AssetId assetAddTwice =
      registerRasterAt(dataManager, QStringLiteral("samples/dem_sample.tif"));
  const auto addA = displayManager.addLayer(viewA, assetAddTwice);
  const auto addB = displayManager.addLayer(viewB, assetAddTwice);
  REQUIRE(addA);
  REQUIRE(addB);
  CHECK(dataManager.leaseCount(assetAddTwice) == 2);

  // Path 2: one addLayer + one cloneLayer on a DIFFERENT asset (distinct
  // fixture so it is not deduped into assetAddTwice).
  const sicnu::data::AssetId assetClone =
      registerRasterAt(dataManager, QStringLiteral("samples/landsat_sample.tif"));
  REQUIRE(assetClone != assetAddTwice);
  const auto cloneSource = displayManager.addLayer(viewA, assetClone);
  REQUIRE(cloneSource);
  const auto cloned = displayManager.cloneLayer(cloneSource.value(), viewB);
  REQUIRE(cloned);
  CHECK(dataManager.leaseCount(assetClone) == 2);

  // Both paths land at leaseCount == 2. Renderer isolation holds on each: set
  // opacity on the viewA layer, the viewB layer is unaffected. Uses the exact
  // layer ids captured above (NOT positional .at(0), since viewA now holds both
  // assetAddTwice and assetClone layers).
  auto *rasterAddA =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(addA.value()));
  auto *rasterAddB =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(addB.value()));
  auto *rasterCloneA =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(cloneSource.value()));
  auto *rasterCloneB =
      qobject_cast<QgsRasterLayer *>(displayManager.mapLayer(cloned.value()));
  REQUIRE(rasterAddA != nullptr);
  REQUIRE(rasterAddB != nullptr);
  REQUIRE(rasterCloneA != nullptr);
  REQUIRE(rasterCloneB != nullptr);
  REQUIRE(rasterAddA->renderer() != nullptr);
  REQUIRE(rasterAddB->renderer() != nullptr);
  REQUIRE(rasterCloneA->renderer() != nullptr);
  REQUIRE(rasterCloneB->renderer() != nullptr);

  // addLayer-twice path isolation: distinct renderer objects, A's edit persists
  // on A, B stays at its prior value (symmetric, positive assertions — matching
  // the rigor of the standalone addLayer-twice case above).
  CHECK(rasterAddA->renderer() != rasterAddB->renderer());
  rasterAddA->renderer()->setOpacity(0.5);
  CHECK(rasterAddA->renderer()->opacity() == 0.5);
  CHECK(rasterAddB->renderer()->opacity() == 1.0);
  // cloneLayer path isolation.
  CHECK(rasterCloneA->renderer() != rasterCloneB->renderer());
  rasterCloneA->renderer()->setOpacity(0.6);
  CHECK(rasterCloneA->renderer()->opacity() == 0.6);
  CHECK(rasterCloneB->renderer()->opacity() == 1.0);
}

TEST_CASE( "QgisDisplayManager active view and auto-display tracking", "[display][qgis_display_manager][active_view]" )
{
  ensureQgisApplication();
  QgsProject *project = QgsProject::instance();
  project->clear();

  DataManager dataManager;
  QgisDisplayManager displayManager( &dataManager );

  CHECK( displayManager.activeViewId().isNull() );
  CHECK_FALSE( displayManager.autoDisplayOnAssetAdded() );

  QgsMapCanvas canvas;
  DisplayViewSpec spec{ &canvas, project->layerTreeRoot(), project->layerStore() };

  const auto viewResult = displayManager.createView( spec );
  REQUIRE( viewResult );
  const DisplayViewId viewId = viewResult.value();

  // First created view automatically becomes active view
  CHECK( displayManager.activeViewId() == viewId );

  displayManager.setAutoDisplayOnAssetAdded( true );
  CHECK( displayManager.autoDisplayOnAssetAdded() );

  // Registering asset automatically adds display layer to active view when autoDisplayOnAssetAdded is true
  const sicnu::data::AssetId assetId = registerRaster( dataManager );
  REQUIRE( !assetId.isNull() );
  QCoreApplication::processEvents();

  const auto snapshot = displayManager.view( viewId );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->layerIds().size() == 1 );
}

TEST_CASE( "QgisDisplayManager auto-display emits autoDisplayFailed without an active view", "[display][qgis_display_manager][active_view]" )
{
  ensureQgisApplication();
  QgsProject *project = QgsProject::instance();
  project->clear();

  DataManager dataManager;
  QgisDisplayManager displayManager( &dataManager );

  // No view created: activeViewId stays null.
  REQUIRE( displayManager.activeViewId().isNull() );
  displayManager.setAutoDisplayOnAssetAdded( true );

  QSignalSpy failedSpy( &displayManager, &QgisDisplayManager::autoDisplayFailed );

  const sicnu::data::AssetId assetId = registerRaster( dataManager );
  REQUIRE( !assetId.isNull() );
  QCoreApplication::processEvents();

  // The auto-display policy reports the failure instead of silently no-op'ing.
  REQUIRE( failedSpy.count() == 1 );
  const QList<QVariant> args = failedSpy.takeFirst();
  CHECK( args.at( 0 ).toString() == assetId.toString() );
  CHECK( !args.at( 1 ).toString().isEmpty() );
}

