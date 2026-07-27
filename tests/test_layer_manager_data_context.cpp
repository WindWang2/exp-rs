#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgslayertreeview.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>

#include "app/layer_manager.h"
#include "app/project_context.h"

namespace {

QString fixturePath(const QString &relative) {
  const QString here = QFileInfo(__FILE__).absolutePath();
  return QFileInfo(here + QStringLiteral("/../data/") + relative)
      .absoluteFilePath();
}

} // namespace

int main(int argc, char *argv[]) {
  QgsApplication application(argc, argv, true);
  QgsApplication::initQgis();
  const int result = Catch::Session().run(argc, argv);
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE("LayerManager loads a raster through the project Data Context",
          "[layer_manager][data_context]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  LayerManager layerManager(&canvas, &treeView, nullptr,
                            &context->dataManager(), &context->displayManager(),
                            context->mainViewId(), nullptr);
  layerManager.initLayerTree();

  const auto loaded = layerManager.loadRasterLayer(
      fixturePath(QStringLiteral("samples/dem_sample.tif")));

  REQUIRE(loaded);
  CHECK(context->dataManager().assets().size() == 1);
  CHECK(context->dataManager().leaseCount(
            context->dataManager().assets().first().id()) == 1);
  CHECK(project->count() == 1);
  const auto mainView = context->displayManager().view(context->mainViewId());
  REQUIRE(mainView);
  REQUIRE(mainView->layerIds().size() == 1);
  CHECK(mainView->layerIds().first() == loaded.value());
  CHECK(context->displayManager().mapLayer(loaded.value()) != nullptr);
}

TEST_CASE("LayerManager removes presentation without unloading its Data Asset",
          "[layer_manager][data_context]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  LayerManager layerManager(&canvas, &treeView, nullptr,
                            &context->dataManager(), &context->displayManager(),
                            context->mainViewId(), nullptr);
  layerManager.initLayerTree();

  const auto loaded = layerManager.loadRasterLayer(
      fixturePath(QStringLiteral("samples/dem_sample.tif")));
  REQUIRE(loaded);
  const sicnu::data::AssetId assetId =
      context->dataManager().assets().first().id();
  QgsMapLayer *mapLayer = context->displayManager().mapLayer(loaded.value());
  REQUIRE(mapLayer != nullptr);
  treeView.setCurrentLayer(mapLayer);
  REQUIRE(layerManager.selectedLayers().size() == 1);

  layerManager.removeSelectedLayers();

  CHECK(context->dataManager().asset(assetId).has_value());
  CHECK(context->dataManager().leaseCount(assetId) == 0);
  CHECK_FALSE(context->displayManager().layer(loaded.value()).has_value());
  CHECK(project->count() == 0);
}

TEST_CASE("Project Context explicitly clears Data and Display state for a new "
          "project",
          "[layer_manager][data_context]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  LayerManager layerManager(&canvas, &treeView, nullptr,
                            &context->dataManager(), &context->displayManager(),
                            context->mainViewId(), nullptr);
  layerManager.initLayerTree();
  REQUIRE(layerManager.loadRasterLayer(
      fixturePath(QStringLiteral("samples/dem_sample.tif"))));

  const auto cleared = context->clearProject(*project);

  REQUIRE(cleared);
  CHECK(context->dataManager().assets().isEmpty());
  CHECK(project->count() == 0);
  const auto mainView = context->displayManager().view(context->mainViewId());
  REQUIRE(mainView);
  CHECK(mainView->layerIds().isEmpty());

  const auto loadedAgain = layerManager.loadRasterLayer(
      fixturePath(QStringLiteral("samples/dem_sample.tif")));
  REQUIRE(loadedAgain);
  CHECK(context->dataManager().assets().size() == 1);
  CHECK(project->count() == 1);
}

TEST_CASE("Generic legacy loading delegates vector discovery to providers",
          "[layer_manager][data_context]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  LayerManager layerManager(&canvas, &treeView, nullptr,
                            &context->dataManager(), &context->displayManager(),
                            context->mainViewId(), nullptr);
  layerManager.initLayerTree();

  const auto vectorLoaded = layerManager.loadLayer(
      fixturePath(QStringLiteral("test_vectors.geojson")));
  REQUIRE(vectorLoaded);
  REQUIRE(context->dataManager().assets().size() == 1);
  CHECK(context->dataManager().assets().first().kind() ==
        sicnu::data::AssetKind::Vector);
}

TEST_CASE("ENVI path-pair resolution stays inside the GDAL provider",
          "[layer_manager][data_context]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  LayerManager layerManager(&canvas, &treeView, nullptr,
                            &context->dataManager(), &context->displayManager(),
                            context->mainViewId(), nullptr);
  layerManager.initLayerTree();

  const auto enviLoaded =
      layerManager.loadRasterLayer(fixturePath(QStringLiteral("dem.hdr")));
  REQUIRE(enviLoaded);
  REQUIRE(context->dataManager().assets().size() == 1);
  const auto rasterAssets = context->dataManager().assets(
      sicnu::data::AssetQuery{sicnu::data::AssetKind::Raster});
  REQUIRE(rasterAssets.size() == 1);
  CHECK(QFileInfo(rasterAssets.first().source().canonicalSource).fileName() ==
        QStringLiteral("dem.dat"));
}

TEST_CASE("Loaded layer survives event-loop turns (registry bridge re-parenting)",
          "[layer_manager][data_context]") {
  // Regression: loadSource() re-parents the tree node via removeChildNode().
  // QgsLayerTreeRegistryBridge reacts to the removal by *queueing* the layer
  // for removal from QgsProject (Qt::QueuedConnection), so the loss only
  // becomes visible after the event loop spins — synchronous checks pass.
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  QgsLayerTreeView treeView;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  LayerManager layerManager(&canvas, &treeView, nullptr,
                            &context->dataManager(), &context->displayManager(),
                            context->mainViewId(), nullptr);
  layerManager.initLayerTree();

  REQUIRE(layerManager.loadRasterLayer(
      fixturePath(QStringLiteral("samples/dem_sample.tif"))));

  // Spin the event loop so queued registry-bridge removals (if any) execute.
  QCoreApplication::processEvents();
  QCoreApplication::processEvents();

  CHECK(project->count() == 1);
  CHECK(canvas.layerCount() == 1);
  QgsLayerTreeLayer *node =
      project->layerTreeRoot()->findLayer(project->mapLayers().first()->id());
  REQUIRE(node != nullptr);
  CHECK(node->parent() == layerManager.findOrCreateGroup(
                              QStringLiteral("Raster Layers")));
}
