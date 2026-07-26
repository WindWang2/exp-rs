#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFileInfo>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include "app/project_context.h"
#include "data/data_manager.h"

using sicnu::data::AssetState;

namespace {

QString fixturePath(const QString &relative) {
  const QString here = QFileInfo(__FILE__).absolutePath();
  return QFileInfo(here + QStringLiteral("/../data/") + relative)
      .absoluteFilePath();
}

std::unique_ptr<sicnu::app::ProjectContext>
createContext(QgsMapCanvas &canvas, QgsProject &project) {
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project.layerTreeRoot(), project.layerStore()};
  auto created = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(created);
  return created.take();
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

TEST_CASE("A legacy direct QGIS layer is adopted and receives an Asset identity",
          "[adoption][safety_net]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  auto context = createContext(canvas, *project);
  REQUIRE(context->dataManager().assets().isEmpty());

  // Add a local raster directly through QGIS, bypassing the Data Manager seam.
  auto *legacyLayer = new QgsRasterLayer(
      fixturePath(QStringLiteral("samples/dem_sample.tif")),
      QStringLiteral("legacy"), QStringLiteral("gdal"));
  REQUIRE(legacyLayer->isValid());
  project->addMapLayer(legacyLayer);

  // The adoption safety net registered a Data Asset and adopted the layer.
  REQUIRE(context->dataManager().assets().size() == 1);
  const auto asset = context->dataManager().assets().first();
  CHECK(asset.kind() == sicnu::data::AssetKind::Raster);
  CHECK(asset.state() == AssetState::Ready);

  // The live QGIS layer now carries its Data Asset identity.
  CHECK(legacyLayer->customProperty(QStringLiteral("sicnu/assetId"))
            .toString() == asset.id().toString());

  // It became a Display Layer in the main view, holding a view lease.
  const auto mainView =
      context->displayManager().view(context->mainViewId());
  REQUIRE(mainView);
  CHECK(mainView->layerIds().size() == 1);
  CHECK(context->dataManager().leaseCount(asset.id()) == 1);
}

TEST_CASE("Display Manager-created layers do not re-enter adoption",
          "[adoption][safety_net][nonrecursive]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  auto context = createContext(canvas, *project);

  // Register and display through the seam. The Display Manager's layer enters
  // the project store, which fires layersAdded; the safety net must not
  // register a second asset for it.
  sicnu::data::SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource =
      fixturePath(QStringLiteral("samples/dem_sample.tif"));
  const auto registered = context->dataManager().registerSource(
      sicnu::data::RegisterRequest{source});
  REQUIRE_FALSE(registered.assetId.isNull());
  REQUIRE(context->displayManager().addLayer(context->mainViewId(),
                                             registered.assetId));

  // Still exactly one asset — the Display Manager's own layer was not adopted.
  CHECK(context->dataManager().assets().size() == 1);
  const auto mainView =
      context->displayManager().view(context->mainViewId());
  REQUIRE(mainView);
  CHECK(mainView->layerIds().size() == 1);
}

TEST_CASE("Remote and unsupported layers are not adopted",
          "[adoption][safety_net][deferred]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  auto context = createContext(canvas, *project);

  // A remote COG streamed over vsicurl is a raw-URI remote source that bypassed
  // the catalog (not registered as a Remote Map Asset); it must not be adopted
  // as a local raster asset. The isRemoteSource adoption guard (#65) keeps this
  // rejection — a catalog-registered Remote Map Asset is unaffected because
  // registration never routes through adoption.
  auto *remoteLayer = new QgsRasterLayer(
      QStringLiteral("/vsicurl/https://example.invalid/data/cog.tif"),
      QStringLiteral("remote"), QStringLiteral("gdal"));
  project->addMapLayer(remoteLayer, false);

  // An unsupported provider (in-memory vector) stays an External Display Layer.
  auto *memoryLayer = new QgsVectorLayer(
      QStringLiteral("Point?crs=EPSG:4326"), QStringLiteral("memory"),
      QStringLiteral("memory"));
  REQUIRE(memoryLayer->isValid());
  project->addMapLayer(memoryLayer);

  CHECK(context->dataManager().assets().isEmpty());
  CHECK(remoteLayer->customProperty(QStringLiteral("sicnu/assetId"))
            .toString()
            .isEmpty());
  CHECK(memoryLayer->customProperty(QStringLiteral("sicnu/assetId"))
            .toString()
            .isEmpty());
}
