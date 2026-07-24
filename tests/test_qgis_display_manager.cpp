#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFileInfo>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaplayerstore.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgsvectorlayer.h>

#include "app/display/qgis_display_manager.h"
#include "data/data_manager.h"

using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::RegisterRequest;
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

sicnu::data::AssetId registerRaster(DataManager &manager) {
  SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource =
      fixturePath(QStringLiteral("samples/dem_sample.tif"));
  const auto registered = manager.registerSource(RegisterRequest{source});
  REQUIRE_FALSE(registered.assetId.isNull());
  return registered.assetId;
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
