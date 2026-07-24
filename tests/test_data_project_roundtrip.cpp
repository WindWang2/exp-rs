#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>

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

namespace {

QString fixturePath(const QString &relative) {
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
