#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>

#include <vector>

#include <gdal.h>
#include <cpl_conv.h>

#include <qgsapplication.h>
#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>

#include "app/data_project_serializer.h"
#include "app/project_context.h"
#include "data/data_manager.h"

using sicnu::data::AssetId;
using sicnu::data::AssetRevision;
using sicnu::data::AssetState;
using sicnu::data::RegisterRequest;
using sicnu::data::RelocateRequest;
using sicnu::data::SourceDescriptor;

namespace {

// Synthesise a small GeoTIFF once and cache it (plus its holding temp dir) so
// the test does not depend on a committed sample raster under data/samples/.
QString syntheticSample() {
  static QTemporaryDir dir;
  static const QString cached = []() {
    GDALAllRegister();
    const QString path = dir.path() + QLatin1Char('/') + QStringLiteral("sample.tif");
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    constexpr int W = 16, H = 16;
    GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);
    double gt[6] = {0.0, 1.0, 0.0, static_cast<double>(H), 0.0, -1.0};
    GDALSetGeoTransform(ds, gt);
    GDALSetProjection(
        ds, "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
            "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    std::vector<float> line(W, 1.0f);
    for (int row = 0; row < H; ++row)
      GDALRasterIO(band, GF_Write, 0, row, W, 1, line.data(), W, 1, GDT_Float32, 0, 0);
    GDALClose(ds);
    return path;
  }();
  return cached;
}

QString fixturePath(const QString &relative) {
  if (relative.startsWith(QLatin1String("samples/")) ||
      relative == QLatin1String("phr_xs.tif")) {
    return syntheticSample();
  }
  const QString here = QFileInfo(__FILE__).absolutePath();
  return QFileInfo(here + QStringLiteral("/../data/") + relative)
      .absoluteFilePath();
}

SourceDescriptor gdalSource(const QString &path) {
  SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource = path;
  return source;
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

// End-to-end Phase-1 lifecycle: one asset, two independently styled Display
// Layers, one layer removal, unload rejection, project round trip, a missing
// source, and relocation — exercised through the Data Manager / Display Manager
// / ProjectContext seams only (no Widgets dependency in the data path).
TEST_CASE("Phase 1 first-deliverable data/display separation lifecycle",
          "[phase1][acceptance]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  auto context = createContext(canvas, *project);

  // -- 1. One raster opened twice: one asset, two independently styled layers.
  const auto registered = context->dataManager().registerSource(
      RegisterRequest{gdalSource(fixturePath(QStringLiteral("samples/dem_sample.tif")))});
  REQUIRE_FALSE(registered.assetId.isNull());
  const AssetId assetId = registered.assetId;

  const auto firstDisplay = context->displayManager().addLayer(
      context->mainViewId(), assetId);
  const auto secondDisplay = context->displayManager().addLayer(
      context->mainViewId(), assetId);
  REQUIRE(firstDisplay);
  REQUIRE(secondDisplay);
  REQUIRE(firstDisplay.value() != secondDisplay.value());

  auto *firstLayer = qobject_cast<QgsRasterLayer *>(
      context->displayManager().mapLayer(firstDisplay.value()));
  auto *secondLayer = qobject_cast<QgsRasterLayer *>(
      context->displayManager().mapLayer(secondDisplay.value()));
  REQUIRE(firstLayer != nullptr);
  REQUIRE(secondLayer != nullptr);
  REQUIRE(firstLayer != secondLayer);
  REQUIRE(firstLayer->renderer() != nullptr);
  REQUIRE(secondLayer->renderer() != nullptr);
  firstLayer->renderer()->setOpacity(0.25);
  secondLayer->renderer()->setOpacity(0.85);
  CHECK(context->dataManager().assets().size() == 1);
  CHECK(context->dataManager().leaseCount(assetId) == 2);

  // -- 2. Removing one Display Layer leaves the Data Asset registered.
  REQUIRE(context->displayManager().removeLayer(firstDisplay.value()));
  CHECK(context->dataManager().asset(assetId).has_value());
  CHECK(context->dataManager().leaseCount(assetId) == 1);
  CHECK(context->displayManager().mapLayer(secondDisplay.value()) ==
        secondLayer);

  // -- 3. Normal unload is rejected while leased; cascade removes once.
  const auto blockedPlan = context->dataManager().planUnload(assetId);
  CHECK(blockedPlan.activeLeases().size() == 1);
  const auto blocked = context->dataManager().unload(blockedPlan);
  REQUIRE_FALSE(blocked);
  CHECK(blocked.diagnostics().first().code ==
        QStringLiteral("unload.leased"));
  CHECK(context->dataManager().asset(assetId).has_value());

  const auto cascaded = context->dataManager().unload(
      context->dataManager().planUnload(assetId).confirmedCascade());
  REQUIRE(cascaded);
  CHECK_FALSE(context->dataManager().asset(assetId).has_value());
  CHECK(context->dataManager().assets().isEmpty());

  // -- 4. Save/reopen preserves identity, and standard QGIS layer definitions
  //       parse alongside the SICNU extension.
  QTemporaryDir ioDir;
  REQUIRE(ioDir.isValid());
  const QString rasterCopy = ioDir.filePath(QStringLiteral("scene.tif"));
  REQUIRE(QFile::copy(fixturePath(QStringLiteral("samples/dem_sample.tif")),
                      rasterCopy));

  const auto persisted = context->dataManager().registerSource(
      RegisterRequest{gdalSource(rasterCopy)});
  REQUIRE_FALSE(persisted.assetId.isNull());
  const auto persistedDisplay = context->displayManager().addLayer(
      context->mainViewId(), persisted.assetId);
  REQUIRE(persistedDisplay);
  auto *persistedLayer = qobject_cast<QgsRasterLayer *>(
      context->displayManager().mapLayer(persistedDisplay.value()));
  REQUIRE(persistedLayer != nullptr);
  persistedLayer->renderer()->setOpacity(0.66);

  sicnu::app::DataProjectSerializer serializer;
  QObject signalReceiver;
  bool readOk = false;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &doc) {
                     REQUIRE(static_cast<bool>(serializer.write(doc, *context)));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &doc) {
                     readOk = static_cast<bool>(
                         serializer.read(doc, *project, *context));
                   });

  const QString projectPath = ioDir.filePath(QStringLiteral("acceptance.qgs"));
  REQUIRE(project->write(projectPath));

  // Standard QGIS layer definitions are present and well-formed in the saved
  // document (the SICNU extension coexists without breaking them).
  QFile projectFile(projectPath);
  REQUIRE(projectFile.open(QIODevice::ReadOnly));
  QDomDocument savedDoc;
  REQUIRE(savedDoc.setContent(&projectFile));
  CHECK_FALSE(savedDoc.documentElement()
                  .firstChildElement(QStringLiteral("projectlayers"))
                  .isNull());
  CHECK_FALSE(savedDoc.documentElement()
                  .firstChildElement(QStringLiteral("sicnuDataManager"))
                  .isNull());

  // -- 5. Move the source away, reopen, and confirm Missing survives with the
  //       Display Layer record; then relocate to recover.
  const QString movedRaster = ioDir.filePath(QStringLiteral("scene-moved.tif"));
  REQUIRE(QFile::rename(rasterCopy, movedRaster));

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readOk);

  const auto missingAsset = context->dataManager().asset(persisted.assetId);
  REQUIRE(missingAsset);
  CHECK(missingAsset->state() == AssetState::Missing);
  const auto missingView =
      context->displayManager().view(context->mainViewId());
  REQUIRE(missingView);
  REQUIRE(missingView->layerIds().size() == 1);
  CHECK(missingView->layerIds().first() == persistedDisplay.value());

  RelocateRequest relocate;
  relocate.id = persisted.assetId;
  relocate.replacement = gdalSource(movedRaster);
  REQUIRE(context->dataManager().relocate(relocate));

  const auto recovered = context->dataManager().asset(persisted.assetId);
  REQUIRE(recovered);
  CHECK(recovered->id() == persisted.assetId);
  CHECK(recovered->state() == AssetState::Ready);
  CHECK(recovered->revision() == AssetRevision::initial().next());

  // The relocated Display Layer kept its identity and restored its renderer.
  auto *recoveredLayer = qobject_cast<QgsRasterLayer *>(
      context->displayManager().mapLayer(persistedDisplay.value()));
  REQUIRE(recoveredLayer != nullptr);
  CHECK(recoveredLayer->isValid());
  CHECK(recoveredLayer->renderer() != nullptr);
  CHECK(recoveredLayer->renderer()->opacity() ==
        Catch::Approx(0.66).margin(0.001));
}
