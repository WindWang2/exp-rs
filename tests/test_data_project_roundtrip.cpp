#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QMap>
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
#include <qgsvectorlayer.h>

#include "app/data_project_serializer.h"
#include "app/project_context.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/virtual_raster_recipe.h"

using sicnu::data::AssetState;
using sicnu::data::RelocateRequest;

namespace {

// Synthesise a small GeoTIFF per distinct `relative` path and cache them (plus
// the holding temp dir) for the process lifetime, so tests do not depend on a
// committed sample raster under data/samples/. Distinct relative paths yield
// distinct files so the Data Manager does not dedup them by SourceKey.
QString syntheticSample(const QString &relative) {
  static QTemporaryDir dir;
  static QMap<QString, QString> cache;
  auto it = cache.constFind(relative);
  if (it != cache.constEnd())
    return it.value();

  GDALAllRegister();
  const QString path = dir.path() + QLatin1Char('/') +
                       QString::number(cache.size()) + QStringLiteral(".tif");
  GDALDriverH driver = GDALGetDriverByName("GTiff");
  REQUIRE(driver != nullptr);
  constexpr int W = 16, H = 16;
  GDALDatasetH ds =
      GDALCreate(driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr);
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
  cache.insert(relative, path);
  return path;
}

QString fixturePath(const QString &relative) {
  // Sample rasters under data/samples/ are no longer committed; redirect those
  // to a synthesised sample (one per distinct path). Other paths (e.g.
  // does-not-exist.tif) resolve to the real data tree so they stay missing.
  if (relative.startsWith(QLatin1String("samples/")) ||
      relative == QLatin1String("phr_xs.tif")) {
    return syntheticSample(relative);
  }
  const QString here = QFileInfo(__FILE__).absolutePath();
  return QFileInfo(here + QStringLiteral("/../data/") + relative)
      .absoluteFilePath();
}

sicnu::data::RegisterResult
registerRaster(sicnu::app::ProjectContext &context) {
  sicnu::data::SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource =
      fixturePath(QStringLiteral("samples/dem_sample.tif"));
  source.dataOptions.insert(QStringLiteral("openMode"),
                            QStringLiteral("read-only"));
  source.dataOptions.insert(QStringLiteral("password"),
                            QStringLiteral("must-not-be-persisted"));
  source.authConfigId = QStringLiteral("safe-auth-reference");
  return context.dataManager().registerSource(
      sicnu::data::RegisterRequest{source});
}

QgsMapLayer *findLayerByDisplayId(QgsProject &project,
                                  sicnu::display::DisplayLayerId id) {
  for (QgsMapLayer *layer : project.mapLayers()) {
    if (layer && layer->customProperty(QStringLiteral("sicnu/displayLayerId"))
                         .toString() == id.toString())
      return layer;
  }
  return nullptr;
}

void moveLayerToGroup(QgsProject &project, QgsMapLayer &layer,
                      QgsLayerTreeGroup &group) {
  QgsLayerTreeLayer *node = project.layerTreeRoot()->findLayer(layer.id());
  REQUIRE(node != nullptr);
  QgsLayerTreeGroup *parent = qobject_cast<QgsLayerTreeGroup *>(node->parent());
  REQUIRE(parent != nullptr);
  parent->removeChildNode(node);
  group.addLayer(&layer);
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

TEST_CASE("SICNU project round trip preserves Data and Display identities",
          "[project][data_roundtrip]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  const sicnu::data::RegisterResult registered = registerRaster(*context);
  REQUIRE_FALSE(registered.assetId.isNull());
  const auto displayed = context->displayManager().addLayer(
      context->mainViewId(), registered.assetId);
  REQUIRE(displayed);

  sicnu::app::DataProjectSerializer serializer;
  QObject signalReceiver;
  bool writeSucceeded = false;
  bool readSucceeded = false;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     writeSucceeded = static_cast<bool>(
                         serializer.write(document, *context));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  QTemporaryDir temporaryDirectory;
  REQUIRE(temporaryDirectory.isValid());
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("roundtrip.qgs"));

  REQUIRE(project->write(projectPath));
  REQUIRE(writeSucceeded);

  QFile projectFile(projectPath);
  REQUIRE(projectFile.open(QIODevice::ReadOnly));
  QDomDocument savedDocument;
  REQUIRE(savedDocument.setContent(&projectFile));
  const QDomElement extension =
      savedDocument.documentElement().firstChildElement(
          QStringLiteral("sicnuDataManager"));
  REQUIRE_FALSE(extension.isNull());
  CHECK(extension.attribute(QStringLiteral("version")) == QStringLiteral("1"));
  const QString savedXml = savedDocument.toString();
  CHECK(savedXml.contains(QStringLiteral("openMode")));
  CHECK(savedXml.contains(QStringLiteral("safe-auth-reference")));
  CHECK_FALSE(savedXml.contains(QStringLiteral("must-not-be-persisted")));
  CHECK_FALSE(savedDocument.documentElement()
                  .firstChildElement(QStringLiteral("projectlayers"))
                  .isNull());

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  const auto restoredAsset = context->dataManager().asset(registered.assetId);
  REQUIRE(restoredAsset);
  CHECK(restoredAsset->id() == registered.assetId);

  const auto restoredView =
      context->displayManager().view(context->mainViewId());
  REQUIRE(restoredView);
  REQUIRE(restoredView->layerIds().size() == 1);
  CHECK(restoredView->layerIds().first() == displayed.value());
  const QgsMapLayer *restoredLayer =
      context->displayManager().mapLayer(displayed.value());
  REQUIRE(restoredLayer != nullptr);
  CHECK(restoredLayer->customProperty(QStringLiteral("sicnu/assetId"))
            .toString() == registered.assetId.toString());
  CHECK(restoredLayer->customProperty(QStringLiteral("sicnu/displayLayerId"))
            .toString() == displayed.value().toString());
}

TEST_CASE("QGIS presentation state stays authoritative across SICNU round trip",
          "[project][data_roundtrip]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  const sicnu::data::RegisterResult registered = registerRaster(*context);
  REQUIRE_FALSE(registered.assetId.isNull());
  const auto firstDisplay = context->displayManager().addLayer(
      context->mainViewId(), registered.assetId,
      sicnu::display::AddLayerOptions{QStringLiteral("Low opacity"), false});
  const auto secondDisplay = context->displayManager().addLayer(
      context->mainViewId(), registered.assetId,
      sicnu::display::AddLayerOptions{QStringLiteral("High opacity"), false});
  REQUIRE(firstDisplay);
  REQUIRE(secondDisplay);

  auto *firstRaster = qobject_cast<QgsRasterLayer *>(
      context->displayManager().mapLayer(firstDisplay.value()));
  auto *secondRaster = qobject_cast<QgsRasterLayer *>(
      context->displayManager().mapLayer(secondDisplay.value()));
  REQUIRE(firstRaster != nullptr);
  REQUIRE(secondRaster != nullptr);
  REQUIRE(firstRaster->renderer() != nullptr);
  REQUIRE(secondRaster->renderer() != nullptr);
  firstRaster->renderer()->setOpacity(0.2);
  secondRaster->renderer()->setOpacity(0.8);

  QgsLayerTreeGroup *firstGroup =
      project->layerTreeRoot()->addGroup(QStringLiteral("Reference"));
  QgsLayerTreeGroup *secondGroup =
      project->layerTreeRoot()->addGroup(QStringLiteral("Analysis"));
  REQUIRE(firstGroup != nullptr);
  REQUIRE(secondGroup != nullptr);
  moveLayerToGroup(*project, *firstRaster, *firstGroup);
  moveLayerToGroup(*project, *secondRaster, *secondGroup);

  sicnu::app::DataProjectSerializer serializer;
  QObject signalReceiver;
  bool writeSucceeded = false;
  bool readSucceeded = false;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     writeSucceeded = static_cast<bool>(
                         serializer.write(document, *context));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  QTemporaryDir temporaryDirectory;
  REQUIRE(temporaryDirectory.isValid());
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("presentation.qgs"));
  REQUIRE(project->write(projectPath));
  REQUIRE(writeSucceeded);
  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  auto *restoredFirst = qobject_cast<QgsRasterLayer *>(
      findLayerByDisplayId(*project, firstDisplay.value()));
  auto *restoredSecond = qobject_cast<QgsRasterLayer *>(
      findLayerByDisplayId(*project, secondDisplay.value()));
  REQUIRE(restoredFirst != nullptr);
  REQUIRE(restoredSecond != nullptr);
  REQUIRE(restoredFirst->renderer() != nullptr);
  REQUIRE(restoredSecond->renderer() != nullptr);
  CHECK(restoredFirst->renderer()->opacity() == Catch::Approx(0.2));
  CHECK(restoredSecond->renderer()->opacity() == Catch::Approx(0.8));

  QgsLayerTreeLayer *restoredFirstNode =
      project->layerTreeRoot()->findLayer(restoredFirst->id());
  QgsLayerTreeLayer *restoredSecondNode =
      project->layerTreeRoot()->findLayer(restoredSecond->id());
  REQUIRE(restoredFirstNode != nullptr);
  REQUIRE(restoredSecondNode != nullptr);
  CHECK(restoredFirstNode->parent()->name() == QStringLiteral("Reference"));
  CHECK(restoredSecondNode->parent()->name() == QStringLiteral("Analysis"));

  const QList<QgsMapLayer *> restoredOrder =
      project->layerTreeRoot()->layerOrder();
  REQUIRE(restoredOrder.size() == 2);
  CHECK(restoredOrder.at(0) == restoredFirst);
  CHECK(restoredOrder.at(1) == restoredSecond);
}

TEST_CASE("Standard QGIS layers are adopted once by source capability",
          "[project][data_roundtrip][adoption]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  const QString rasterPath =
      fixturePath(QStringLiteral("samples/dem_sample.tif"));
  auto *firstStandard = new QgsRasterLayer(
      rasterPath, QStringLiteral("Standard one"), QStringLiteral("gdal"));
  auto *secondStandard = new QgsRasterLayer(
      rasterPath, QStringLiteral("Standard two"), QStringLiteral("gdal"));
  auto *unsupported = new QgsVectorLayer(QStringLiteral("Point?crs=EPSG:4326"),
                                         QStringLiteral("External memory"),
                                         QStringLiteral("memory"));
  REQUIRE(firstStandard->isValid());
  REQUIRE(secondStandard->isValid());
  REQUIRE(unsupported->isValid());
  project->addMapLayer(firstStandard);
  project->addMapLayer(secondStandard);
  project->addMapLayer(unsupported);

  QTemporaryDir temporaryDirectory;
  REQUIRE(temporaryDirectory.isValid());
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("standard.qgs"));
  REQUIRE(project->write(projectPath));
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();
  sicnu::app::DataProjectSerializer serializer;
  QObject signalReceiver;
  bool readSucceeded = false;
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);
  REQUIRE(context->dataManager().assets().size() == 1);
  const sicnu::data::AssetId adoptedAsset =
      context->dataManager().assets().first().id();
  const auto adoptedView =
      context->displayManager().view(context->mainViewId());
  REQUIRE(adoptedView);
  REQUIRE(adoptedView->layerIds().size() == 2);
  CHECK(context->dataManager().leaseCount(adoptedAsset) == 2);
  CHECK(adoptedView->layerIds().at(0) != adoptedView->layerIds().at(1));

  QgsMapLayer *externalLayer = nullptr;
  for (QgsMapLayer *layer : project->mapLayers()) {
    if (layer && layer->providerType() == QStringLiteral("memory"))
      externalLayer = layer;
  }
  REQUIRE(externalLayer != nullptr);
  CHECK(externalLayer->customProperty(QStringLiteral("sicnu/assetId"))
            .toString()
            .isEmpty());

  QFile projectFile(projectPath);
  REQUIRE(projectFile.open(QIODevice::ReadOnly));
  QDomDocument standardDocument;
  REQUIRE(standardDocument.setContent(&projectFile));
  REQUIRE(serializer.read(standardDocument, *project, *context));
  const auto reconciledView =
      context->displayManager().view(context->mainViewId());
  REQUIRE(reconciledView);
  CHECK(reconciledView->layerIds().size() == 2);
  CHECK(context->dataManager().assets().size() == 1);
  CHECK(context->dataManager().leaseCount(adoptedAsset) == 2);
}

TEST_CASE("Reopening after moving a source preserves the Asset and Display records",
          "[project][data_roundtrip][missing]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QTemporaryDir dataDirectory;
  REQUIRE(dataDirectory.isValid());
  const QString originalRaster =
      dataDirectory.filePath(QStringLiteral("scene.tif"));
  REQUIRE(QFile::copy(fixturePath(QStringLiteral("samples/dem_sample.tif")),
                      originalRaster));

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  sicnu::data::SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource = originalRaster;
  const auto registered = context->dataManager().registerSource(
      sicnu::data::RegisterRequest{source});
  REQUIRE_FALSE(registered.assetId.isNull());
  const auto displayed = context->displayManager().addLayer(
      context->mainViewId(), registered.assetId);
  REQUIRE(displayed);

  sicnu::app::DataProjectSerializer serializer;
  QObject signalReceiver;
  bool readSucceeded = false;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     REQUIRE(static_cast<bool>(
                         serializer.write(document, *context)));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  const QString projectPath =
      dataDirectory.filePath(QStringLiteral("moving.qgs"));
  REQUIRE(project->write(projectPath));

  // Move the source away, then reopen the project.
  const QString movedRaster =
      dataDirectory.filePath(QStringLiteral("scene-moved.tif"));
  REQUIRE(QFile::rename(originalRaster, movedRaster));

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  // The Asset ID and its persisted identity survive the missing source.
  const auto missingAsset = context->dataManager().asset(registered.assetId);
  REQUIRE(missingAsset);
  CHECK(missingAsset->id() == registered.assetId);
  CHECK(missingAsset->state() == AssetState::Missing);

  // The Display Layer record is preserved even though the source is missing.
  const auto missingView =
      context->displayManager().view(context->mainViewId());
  REQUIRE(missingView);
  CHECK(missingView->layerIds().size() == 1);
  CHECK(missingView->layerIds().first() == displayed.value());

  // Relocating to the moved source recovers the asset and advances the revision.
  RelocateRequest relocate;
  relocate.id = registered.assetId;
  relocate.replacement = sicnu::data::SourceDescriptor{
      QStringLiteral("gdal"), movedRaster, {}, {}, {}};
  const auto relocated = context->dataManager().relocate(relocate);
  REQUIRE(relocated);

  const auto recoveredAsset = context->dataManager().asset(registered.assetId);
  REQUIRE(recoveredAsset);
  CHECK(recoveredAsset->id() == registered.assetId);
  CHECK(recoveredAsset->state() == AssetState::Ready);
  CHECK(recoveredAsset->revision() == sicnu::data::AssetRevision::initial().next());
}

TEST_CASE("A promoted temporary asset round-trips into the saved project",
          "[project][data_roundtrip][promote]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  // Register a SESSION-TEMPORARY asset. The serializer filters out
  // non-ProjectPersistent assets, so before promotion this asset would NOT be
  // saved. Promote it so it survives the round trip.
  sicnu::data::SourceDescriptor source;
  source.providerKey = QStringLiteral("gdal");
  source.canonicalSource =
      fixturePath(QStringLiteral("samples/dem_sample.tif"));
  sicnu::data::RegisterRequest request{source};
  request.persistence = sicnu::data::PersistencePolicy::SessionTemporary;
  const sicnu::data::RegisterResult registered =
      context->dataManager().registerSource(request);
  REQUIRE_FALSE(registered.assetId.isNull());
  CHECK(context->dataManager().asset(registered.assetId)->persistence() ==
        sicnu::data::PersistencePolicy::SessionTemporary);

  // Attach provenance so the round trip can prove it survives promote + reopen.
  sicnu::data::DerivationRecord derivation;
  derivation.algorithmId = QStringLiteral("sicnu:ndvi");
  REQUIRE(context->dataManager().attachDerivationRecord(registered.assetId,
                                                        derivation));

  REQUIRE(context->dataManager().promote(registered.assetId));
  CHECK(context->dataManager().asset(registered.assetId)->persistence() ==
        sicnu::data::PersistencePolicy::ProjectPersistent);

  sicnu::app::DataProjectSerializer serializer;
  bool writeSucceeded = false;
  bool readSucceeded = false;
  QObject signalReceiver;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     writeSucceeded = static_cast<bool>(
                         serializer.write(document, *context));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  QTemporaryDir temporaryDirectory;
  REQUIRE(temporaryDirectory.isValid());
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("promote_roundtrip.qgs"));

  REQUIRE(project->write(projectPath));
  REQUIRE(writeSucceeded);

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  // The promoted asset is restored with its original identity and the
  // ProjectPersistent policy it was promoted to.
  const auto restoredAsset = context->dataManager().asset(registered.assetId);
  REQUIRE(restoredAsset);
  CHECK(restoredAsset->id() == registered.assetId);
  CHECK(restoredAsset->persistence() ==
        sicnu::data::PersistencePolicy::ProjectPersistent);

  // Provenance survives promote + save + reopen.
  const auto restoredProvenance =
      context->dataManager().provenance(registered.assetId);
  REQUIRE(restoredProvenance);
  CHECK(restoredProvenance->algorithmId == QStringLiteral("sicnu:ndvi"));
  CHECK(restoredProvenance->outputAssetId == registered.assetId);
}

TEST_CASE("A Data Collection and its children round-trip into the saved project",
          "[project][data_roundtrip][collection]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  // Two distinct staged rasters as collection children.
  QTemporaryDir dir;
  const auto stagedPath = [&dir](const QString &name) {
    const QString p = dir.filePath(name);
    REQUIRE(QFile::copy(
        fixturePath(QStringLiteral("samples/dem_sample.tif")), p));
    return p;
  };

  sicnu::data::SourceDescriptor sourceA;
  sourceA.providerKey = QStringLiteral("gdal");
  sourceA.canonicalSource = stagedPath(QStringLiteral("a.tif"));
  const sicnu::data::RegisterResult childA =
      context->dataManager().registerSource({sourceA});

  sicnu::data::SourceDescriptor sourceB;
  sourceB.providerKey = QStringLiteral("gdal");
  sourceB.canonicalSource = stagedPath(QStringLiteral("b.tif"));
  const sicnu::data::RegisterResult childB =
      context->dataManager().registerSource({sourceB});

  // Create a collection with product metadata and add both children.
  sicnu::data::ProductMetadata metadata;
  metadata.platform = QStringLiteral("Landsat-8");
  metadata.sensor = QStringLiteral("OLI");
  metadata.productLevel = QStringLiteral("L1TP");
  metadata.acquisitionDate = QStringLiteral("2026-07-25");
  metadata.processingLevel = QStringLiteral("DN");
  metadata.attributes.insert(QStringLiteral("path"), QStringLiteral("125"));
  metadata.attributes.insert(QStringLiteral("row"), QStringLiteral("034"));

  const sicnu::data::CollectionCreateResult collection =
      context->dataManager().createCollection(
          {QStringLiteral("Landsat-8 scene 125/034"), metadata});
  REQUIRE_FALSE(collection.collectionId.isNull());
  REQUIRE(context->dataManager().addChildToCollection(
      collection.collectionId, childA.assetId));
  REQUIRE(context->dataManager().addChildToCollection(
      collection.collectionId, childB.assetId));

  sicnu::app::DataProjectSerializer serializer;
  bool writeSucceeded = false;
  bool readSucceeded = false;
  QObject signalReceiver;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     writeSucceeded =
                         static_cast<bool>(serializer.write(document, *context));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  QTemporaryDir temporaryDirectory;
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("collection_roundtrip.qgs"));
  REQUIRE(project->write(projectPath));
  REQUIRE(writeSucceeded);

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  // The collection is restored with its original id, name, metadata, and
  // ordered children.
  const auto restored =
      context->dataManager().collection(collection.collectionId);
  REQUIRE(restored);
  CHECK(restored->displayName == QStringLiteral("Landsat-8 scene 125/034"));
  CHECK(restored->metadata.platform == QStringLiteral("Landsat-8"));
  CHECK(restored->metadata.sensor == QStringLiteral("OLI"));
  CHECK(restored->metadata.productLevel == QStringLiteral("L1TP"));
  CHECK(restored->metadata.acquisitionDate == QStringLiteral("2026-07-25"));
  CHECK(restored->metadata.attributes.value(QStringLiteral("path")) ==
        QStringLiteral("125"));
  CHECK(restored->metadata.attributes.value(QStringLiteral("row")) ==
        QStringLiteral("034"));
  REQUIRE(restored->childAssetIds.size() == 2);
  CHECK(restored->childAssetIds.first() == childA.assetId);
  CHECK(restored->childAssetIds.last() == childB.assetId);

  // Children carry their parent collection id.
  CHECK(context->dataManager().asset(childA.assetId)->parentCollectionId() ==
        collection.collectionId);
  CHECK(context->dataManager().asset(childB.assetId)->parentCollectionId() ==
        collection.collectionId);
}

TEST_CASE("A project with no collections round-trips correctly",
          "[project][data_roundtrip][collection]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  // Register a standalone asset (no collection).
  const sicnu::data::RegisterResult asset = registerRaster(*context);
  REQUIRE_FALSE(asset.assetId.isNull());
  CHECK(context->dataManager().collections().isEmpty());

  sicnu::app::DataProjectSerializer serializer;
  bool writeSucceeded = false;
  bool readSucceeded = false;
  QObject signalReceiver;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     writeSucceeded =
                         static_cast<bool>(serializer.write(document, *context));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  QTemporaryDir temporaryDirectory;
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("no_collection.qgs"));
  REQUIRE(project->write(projectPath));
  REQUIRE(writeSucceeded);

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  // The standalone asset round-trips; no collections appear.
  const auto restored = context->dataManager().asset(asset.assetId);
  REQUIRE(restored);
  CHECK(restored->id() == asset.assetId);
  CHECK_FALSE(restored->parentCollectionId().has_value());
  CHECK(context->dataManager().collections().isEmpty());
}

TEST_CASE("A virtual raster and its dependencies round-trip into the saved project",
          "[project][data_roundtrip][virtual_raster]") {
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  // Two staged rasters as the virtual raster's inputs.
  QTemporaryDir dir;
  const auto stagedPath = [&dir](const QString &name) {
    const QString p = dir.filePath(name);
    REQUIRE(QFile::copy(
        fixturePath(QStringLiteral("samples/dem_sample.tif")), p));
    return p;
  };

  sicnu::data::SourceDescriptor sourceA;
  sourceA.providerKey = QStringLiteral("gdal");
  sourceA.canonicalSource = stagedPath(QStringLiteral("a.tif"));
  const sicnu::data::RegisterResult inputA =
      context->dataManager().registerSource({sourceA});

  sicnu::data::SourceDescriptor sourceB;
  sourceB.providerKey = QStringLiteral("gdal");
  sourceB.canonicalSource = stagedPath(QStringLiteral("b.tif"));
  const sicnu::data::RegisterResult inputB =
      context->dataManager().registerSource({sourceB});

  // Promote both inputs to ProjectPersistent so they survive the save.
  REQUIRE(context->dataManager().promote(inputA.assetId));
  REQUIRE(context->dataManager().promote(inputB.assetId));

  sicnu::data::VirtualRasterRecipe recipe;
  recipe.inputs = {sicnu::data::BandRef{inputA.assetId, 1},
                   sicnu::data::BandRef{inputB.assetId, 1}};
  const sicnu::data::Result<sicnu::data::AssetId> created =
      context->dataManager().createVirtualRaster(recipe);
  REQUIRE(created);
  const sicnu::data::AssetId virtualId = created.value();

  sicnu::app::DataProjectSerializer serializer;
  bool writeSucceeded = false;
  bool readSucceeded = false;
  QObject signalReceiver;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     writeSucceeded =
                         static_cast<bool>(serializer.write(document, *context));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  QTemporaryDir temporaryDirectory;
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("virtual_roundtrip.qgs"));
  REQUIRE(project->write(projectPath));
  REQUIRE(writeSucceeded);

  // The recipe - not the scratch .vrt path - is persisted; verify the .vrt
  // path does NOT appear in the written project.
  QFile written(projectPath);
  REQUIRE(written.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString xml = QString::fromUtf8(written.readAll());
  written.close();
  CHECK_FALSE(xml.contains(QStringLiteral(".vrt")));
  CHECK(xml.contains(QStringLiteral("<virtualRasters")));

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  // Identity: same AssetId, same recipe, same edges in both directions.
  const auto restored = context->dataManager().asset(virtualId);
  REQUIRE(restored);
  CHECK(restored->id() == virtualId);
  CHECK(restored->kind() == sicnu::data::AssetKind::VirtualRaster);

  const std::optional<sicnu::data::VirtualRasterRecipe> restoredRecipe =
      context->dataManager().virtualRasterRecipe(virtualId);
  REQUIRE(restoredRecipe);
  REQUIRE(restoredRecipe->inputs.size() == 2);
  CHECK(restoredRecipe->inputs.first().asset == inputA.assetId);
  CHECK(restoredRecipe->inputs.last().asset == inputB.assetId);

  CHECK(context->dataManager().strongDependenciesOf(virtualId) ==
        QVector<sicnu::data::AssetId>{inputA.assetId, inputB.assetId});
  CHECK(context->dataManager().strongDependentsOf(inputA.assetId) ==
        QVector<sicnu::data::AssetId>{virtualId});
}

TEST_CASE("A virtual raster whose input was not saved is restored, not dropped",
          "[project][data_roundtrip][virtual_raster]") {
  // The virtual raster references a SessionTemporary input that is NOT
  // persisted; on restore the input is gone. The spec requires the virtual
  // asset to remain in the catalog (missing dependencies do not drop assets),
  // in a non-Ready state, with the edge skipped and a Warning surfaced.
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  QTemporaryDir dir;
  // One persistent input that survives, one session-temporary that does not.
  sicnu::data::SourceDescriptor sourcePersistent;
  sourcePersistent.providerKey = QStringLiteral("gdal");
  sourcePersistent.canonicalSource = dir.filePath(QStringLiteral("a.tif"));
  REQUIRE(QFile::copy(
      fixturePath(QStringLiteral("samples/dem_sample.tif")),
      sourcePersistent.canonicalSource));
  const sicnu::data::RegisterResult persistentInput =
      context->dataManager().registerSource({sourcePersistent});
  REQUIRE(context->dataManager().promote(persistentInput.assetId));

  sicnu::data::SourceDescriptor sourceSession;
  sourceSession.providerKey = QStringLiteral("gdal");
  sourceSession.canonicalSource = dir.filePath(QStringLiteral("b.tif"));
  REQUIRE(QFile::copy(
      fixturePath(QStringLiteral("samples/dem_sample.tif")),
      sourceSession.canonicalSource));
  const sicnu::data::RegisterResult sessionInput =
      context->dataManager().registerSource(
          {sourceSession, sicnu::data::PersistencePolicy::SessionTemporary});

  sicnu::data::VirtualRasterRecipe recipe;
  recipe.inputs = {sicnu::data::BandRef{persistentInput.assetId, 1},
                   sicnu::data::BandRef{sessionInput.assetId, 1}};
  const sicnu::data::Result<sicnu::data::AssetId> created =
      context->dataManager().createVirtualRaster(recipe);
  REQUIRE(created);
  const sicnu::data::AssetId virtualId = created.value();

  sicnu::app::DataProjectSerializer serializer;
  sicnu::data::Result<void> readResult =
      sicnu::data::Result<void>::failure(
          sicnu::data::Diagnostic{QStringLiteral("test.not_run"),
                                  QStringLiteral("read hook has not fired")});
  QObject signalReceiver;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     ( void ) serializer.write(document, *context);
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readResult = serializer.read(document, *project, *context);
                   });

  QTemporaryDir temporaryDirectory;
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("virtual_missing_input.qgs"));
  REQUIRE(project->write(projectPath));
  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readResult);

  // The virtual asset is NOT dropped; it is restored in UnavailableSource
  // state (the ticket's named state for a dependency that did not survive).
  const auto restored = context->dataManager().asset(virtualId);
  REQUIRE(restored);
  CHECK(restored->id() == virtualId);
  CHECK(restored->state() ==
        sicnu::data::AssetState::UnavailableSource);
  // The recipe is preserved (including the missing-input AssetId reference).
  const std::optional<sicnu::data::VirtualRasterRecipe> restoredRecipe =
      context->dataManager().virtualRasterRecipe(virtualId);
  REQUIRE(restoredRecipe);
  REQUIRE(restoredRecipe->inputs.size() == 2);

  // The surviving input keeps its edge; the missing input's edge is skipped
  // (addStrongDependency would reject an unknown input), and a Warning was
  // surfaced by the restore.
  CHECK(context->dataManager().strongDependenciesOf(virtualId) ==
        QVector<sicnu::data::AssetId>{persistentInput.assetId});
  bool sawMissingDependencyWarning = false;
  for (const sicnu::data::Diagnostic &d : readResult.diagnostics()) {
    if (d.severity == sicnu::data::DiagnosticSeverity::Warning &&
        d.message.contains(QStringLiteral("missing"), Qt::CaseInsensitive))
      sawMissingDependencyWarning = true;
  }
  CHECK(sawMissingDependencyWarning);
}

TEST_CASE("A remote-map asset round-trips with its descriptor and identity",
          "[project][data_roundtrip][remote_map]") {
  // The wave's serialization claim: no new XML element is needed — the existing
  // <source provider canonical subdataset authConfigId> + <option> children
  // already round-trip a remote-map SourceDescriptor. The asset is restored
  // with the same AssetId and the same descriptor (provider/canonical/layer/
  // crs/format/authConfigId). The structure is re-derived on restore, not
  // stored as a snapshot.
  QgsProject *project = QgsProject::instance();
  project->clear();

  QgsMapCanvas canvas;
  const sicnu::display::DisplayViewSpec viewSpec{
      &canvas, project->layerTreeRoot(), project->layerStore()};
  auto createdContext = sicnu::app::ProjectContext::create(viewSpec);
  REQUIRE(createdContext);
  std::unique_ptr<sicnu::app::ProjectContext> context = createdContext.take();

  // A remote-map descriptor with the full option set + an authConfigId.
  sicnu::data::SourceDescriptor remoteSource;
  remoteSource.providerKey = QStringLiteral("wms");
  remoteSource.canonicalSource = QStringLiteral("https://wms.example.com/service");
  remoteSource.authConfigId = QStringLiteral("cfg-abc");
  remoteSource.dataOptions.insert(QStringLiteral("layers"),
                                  QStringLiteral("imagery,labels"));
  remoteSource.dataOptions.insert(QStringLiteral("crs"), QStringLiteral("EPSG:4326"));
  remoteSource.dataOptions.insert(QStringLiteral("format"), QStringLiteral("image/png"));
  const sicnu::data::RegisterResult registered =
      context->dataManager().registerSource({remoteSource});
  REQUIRE_FALSE(registered.assetId.isNull());
  // The default DataManager (NoNetworkProbe) resolves a remote map Offline; the
  // asset still registers — registration of a URL source is NOT blocked. The
  // Offline state is the re-probe outcome (not a stored snapshot), so it must
  // match before save and after restore.
  const auto snapshot = context->dataManager().asset(registered.assetId);
  REQUIRE(snapshot);
  CHECK(snapshot->kind() == sicnu::data::AssetKind::RemoteMap);
  CHECK(snapshot->state() == sicnu::data::AssetState::Offline);

  sicnu::app::DataProjectSerializer serializer;
  bool writeSucceeded = false;
  bool readSucceeded = false;
  QObject signalReceiver;
  QObject::connect(project, &QgsProject::writeProject, &signalReceiver,
                   [&](QDomDocument &document) {
                     writeSucceeded =
                         static_cast<bool>(serializer.write(document, *context));
                   });
  QObject::connect(project, &QgsProject::readProject, &signalReceiver,
                   [&](const QDomDocument &document) {
                     readSucceeded = static_cast<bool>(
                         serializer.read(document, *project, *context));
                   });

  QTemporaryDir temporaryDirectory;
  const QString projectPath =
      temporaryDirectory.filePath(QStringLiteral("remote_map_roundtrip.qgs"));
  REQUIRE(project->write(projectPath));
  REQUIRE(writeSucceeded);

  // No remote-map-specific XML element was introduced: the existing <source>
  // element round-trips the descriptor. The persisted project must NOT carry a
  // new <remoteMaps>/<remoteMap> tag (the descriptor rides in <assets>).
  QFile written(projectPath);
  REQUIRE(written.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString xml = QString::fromUtf8(written.readAll());
  written.close();
  CHECK_FALSE(xml.contains(QStringLiteral("<remoteMaps")));
  CHECK_FALSE(xml.contains(QStringLiteral("<remoteMap")));
  // The descriptor fields ARE persisted through the existing <source> element.
  CHECK(xml.contains(QStringLiteral("https://wms.example.com/service")));
  CHECK(xml.contains(QStringLiteral("cfg-abc")));

  REQUIRE(context->clearProject(*project));
  REQUIRE(project->read(projectPath));
  REQUIRE(readSucceeded);

  // Identity + descriptor preserved. The Offline state matches the pre-save
  // probe outcome — the structure was re-derived by re-probe, not restored from
  // a stored snapshot.
  const auto restored = context->dataManager().asset(registered.assetId);
  REQUIRE(restored);
  CHECK(restored->id() == registered.assetId);
  CHECK(restored->kind() == sicnu::data::AssetKind::RemoteMap);
  CHECK(restored->state() == sicnu::data::AssetState::Offline);
  CHECK(restored->source().providerKey == QStringLiteral("wms"));
  CHECK(restored->source().canonicalSource ==
        QStringLiteral("https://wms.example.com/service"));
  CHECK(restored->source().authConfigId == QStringLiteral("cfg-abc"));
  CHECK(restored->source().dataOptions.value(QStringLiteral("layers")) ==
        QStringLiteral("imagery,labels"));
  CHECK(restored->source().dataOptions.value(QStringLiteral("crs")) ==
        QStringLiteral("EPSG:4326"));
  CHECK(restored->source().dataOptions.value(QStringLiteral("format")) ==
        QStringLiteral("image/png"));
}

