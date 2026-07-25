#include "qgis_display_manager.h"

#include <map>
#include <utility>

#include <QPointer>
#include <QThread>

#include <qgslayertree.h>
#include <qgslayertreegroup.h>
#include <qgslayertreelayer.h>
#include <qgslayertreemapcanvasbridge.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmaplayerstore.h>
#include <qgsmaplayerstyle.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include "data/data_manager.h"

namespace sicnu::display {

namespace {

using data::Diagnostic;
using data::DiagnosticSeverity;

Diagnostic displayDiagnostic(const QString &code, const QString &message) {
  return Diagnostic{code, message, DiagnosticSeverity::Error};
}

QString materializationSource(const data::AssetSnapshot &asset) {
  const data::SourceDescriptor &source = asset.source();
  if (source.subdataset.isEmpty())
    return source.canonicalSource;

  if (asset.kind() == data::AssetKind::Raster)
    return source.subdataset;

  return source.canonicalSource + QStringLiteral("|layername=") +
         source.subdataset;
}

std::unique_ptr<QgsMapLayer> materializeLayer(const data::AssetSnapshot &asset,
                                              const AddLayerOptions &options) {
  const QString name =
      options.displayName.isEmpty() ? asset.displayName() : options.displayName;
  const QString source = materializationSource(asset);

  switch (asset.kind()) {
  case data::AssetKind::Raster:
  // A VirtualRaster resolves to a GDAL-readable managed .vrt scratch file, so
  // it materializes through the same raster path as an ordinary file raster.
  case data::AssetKind::VirtualRaster:
    return std::make_unique<QgsRasterLayer>(source, name,
                                            QStringLiteral("gdal"));
  case data::AssetKind::Vector:
    return std::make_unique<QgsVectorLayer>(source, name,
                                            QStringLiteral("ogr"));
  case data::AssetKind::RemoteMap:
    return {};
  }
  return {};
}

} // namespace

DisplayViewId::DisplayViewId(QUuid value) : m_value(std::move(value)) {}

DisplayViewId DisplayViewId::generate() {
  return DisplayViewId(QUuid::createUuid());
}

bool DisplayViewId::isNull() const { return m_value.isNull(); }

QString DisplayViewId::toString() const {
  return m_value.toString(QUuid::WithoutBraces);
}

DisplayLayerId::DisplayLayerId(QUuid value) : m_value(std::move(value)) {}

DisplayLayerId DisplayLayerId::generate() {
  return DisplayLayerId(QUuid::createUuid());
}

std::optional<DisplayLayerId> DisplayLayerId::fromString(const QString &text) {
  const QUuid value(text);
  if (value.isNull())
    return std::nullopt;
  return DisplayLayerId(value);
}

bool DisplayLayerId::isNull() const { return m_value.isNull(); }

QString DisplayLayerId::toString() const {
  return m_value.toString(QUuid::WithoutBraces);
}

DisplayViewSnapshot::DisplayViewSnapshot(DisplayViewId id,
                                         QVector<DisplayLayerId> layerIds)
    : m_id(std::move(id)), m_layerIds(std::move(layerIds)) {}

const DisplayViewId &DisplayViewSnapshot::id() const { return m_id; }

const QVector<DisplayLayerId> &DisplayViewSnapshot::layerIds() const {
  return m_layerIds;
}

DisplayLayerSnapshot::DisplayLayerSnapshot(DisplayLayerId id,
                                           DisplayViewId viewId,
                                           data::AssetId assetId,
                                           QString qgisLayerId)
    : m_id(std::move(id)), m_viewId(std::move(viewId)),
      m_assetId(std::move(assetId)), m_qgisLayerId(std::move(qgisLayerId)) {}

const DisplayLayerId &DisplayLayerSnapshot::id() const { return m_id; }

const DisplayViewId &DisplayLayerSnapshot::viewId() const { return m_viewId; }

const data::AssetId &DisplayLayerSnapshot::assetId() const { return m_assetId; }

const QString &DisplayLayerSnapshot::qgisLayerId() const {
  return m_qgisLayerId;
}

struct QgisDisplayManager::Impl {
  struct ViewRecord {
    DisplayViewId id;
    QPointer<QgsMapCanvas> canvas;
    QPointer<QgsLayerTree> layerTree;
    QPointer<QgsMapLayerStore> layerStore;
    QPointer<QgsLayerTreeMapCanvasBridge> bridge;
    QVector<DisplayLayerId> layerIds;
  };

  struct LayerRecord {
    DisplayLayerSnapshot snapshot;
    QPointer<QgsMapLayer> mapLayer;
    data::AssetLease lease;
  };

  QPointer<data::DataManager> dataManager;
  std::map<QString, std::unique_ptr<ViewRecord>> views;
  std::map<QString, std::unique_ptr<LayerRecord>> layers;

  ViewRecord *findView(DisplayViewId id) {
    const auto it = views.find(id.toString());
    return it == views.end() ? nullptr : it->second.get();
  }

  const ViewRecord *findView(DisplayViewId id) const {
    const auto it = views.find(id.toString());
    return it == views.end() ? nullptr : it->second.get();
  }

  LayerRecord *findLayer(DisplayLayerId id) {
    const auto it = layers.find(id.toString());
    return it == layers.end() ? nullptr : it->second.get();
  }

  const LayerRecord *findLayer(DisplayLayerId id) const {
    const auto it = layers.find(id.toString());
    return it == layers.end() ? nullptr : it->second.get();
  }
};

QgisDisplayManager::QgisDisplayManager(data::DataManager *dataManager,
                                       QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {
  m_impl->dataManager = dataManager;
  if (dataManager) {
    connect(dataManager, &data::DataManager::assetAboutToUnload, this,
            [this](data::AssetId assetId) {
              QVector<DisplayLayerId> affected;
              for (const auto &[key, record] : m_impl->layers) {
                (void)key;
                if (record->snapshot.assetId() == assetId)
                  affected.append(record->snapshot.id());
              }
              for (const DisplayLayerId layerId : affected)
                (void)removeLayer(layerId);
            });
    connect(dataManager, &data::DataManager::assetChanged, this,
            [this](data::AssetId assetId) {
              // A relocation or reload changed the asset's source. Recreate the
              // QGIS layer for every Display Layer that presents it, keeping each
              // Display Layer's identity and presentation state.
              QVector<DisplayLayerId> affected;
              for (const auto &[key, record] : m_impl->layers) {
                (void)key;
                if (record->snapshot.assetId() == assetId)
                  affected.append(record->snapshot.id());
              }
              for (const DisplayLayerId layerId : affected)
                (void)relocateLayer(layerId);
            });
  }
}

QgisDisplayManager::~QgisDisplayManager() {
  QVector<DisplayLayerId> layerIds;
  layerIds.reserve(static_cast<qsizetype>(m_impl->layers.size()));
  for (const auto &[key, record] : m_impl->layers) {
    (void)key;
    layerIds.append(record->snapshot.id());
  }
  for (const DisplayLayerId layerId : layerIds)
    (void)removeLayer(layerId);
}

data::Result<DisplayViewId>
QgisDisplayManager::createView(const DisplayViewSpec &spec) {
  if (QThread::currentThread() != thread()) {
    return data::Result<DisplayViewId>::failure(displayDiagnostic(
        QStringLiteral("display.wrong_thread"),
        QStringLiteral(
            "Display mutations must run on the manager's owning thread")));
  }
  if (!spec.canvas || !spec.layerTree || !spec.layerStore) {
    return data::Result<DisplayViewId>::failure(displayDiagnostic(
        QStringLiteral("display.invalid_view"),
        QStringLiteral(
            "A display view requires a canvas, layer tree, and layer store")));
  }

  const DisplayViewId id = DisplayViewId::generate();
  auto record = std::make_unique<Impl::ViewRecord>();
  record->id = id;
  record->canvas = spec.canvas;
  record->layerTree = spec.layerTree;
  record->layerStore = spec.layerStore;
  record->bridge =
      new QgsLayerTreeMapCanvasBridge(spec.layerTree, spec.canvas, this);
  record->bridge->setAutoSetupOnFirstLayer(false);
  m_impl->views.emplace(id.toString(), std::move(record));
  return data::Result<DisplayViewId>::success(id);
}

data::Result<DisplayLayerId>
QgisDisplayManager::addLayer(DisplayViewId viewId, data::AssetId assetId,
                             const AddLayerOptions &options) {
  if (QThread::currentThread() != thread()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.wrong_thread"),
        QStringLiteral(
            "Display mutations must run on the manager's owning thread")));
  }

  Impl::ViewRecord *viewRecord = m_impl->findView(viewId);
  if (!viewRecord) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.invalid_view"),
        QStringLiteral("No registered display view matches the requested id")));
  }
  if (viewRecord->canvas.isNull() || viewRecord->layerTree.isNull() ||
      viewRecord->layerStore.isNull()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.view_unavailable"),
        QStringLiteral(
            "The display view's QGIS objects are no longer available")));
  }
  if (m_impl->dataManager.isNull()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.data_manager_unavailable"),
        QStringLiteral("The Data Manager is no longer available")));
  }

  const std::optional<data::AssetSnapshot> asset =
      m_impl->dataManager->asset(assetId);
  if (!asset) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.asset_not_found"),
        QStringLiteral("No registered Data Asset matches the requested id")));
  }
  if (asset->state() != data::AssetState::Ready) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.asset_not_ready"),
        QStringLiteral("The Data Asset is not ready for display")));
  }
  if (!asset->capabilities().testFlag(data::AssetCapability::Renderable)) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.asset_not_renderable"),
        QStringLiteral("The Data Asset does not declare display capability")));
  }

  std::unique_ptr<QgsMapLayer> qgisLayer = materializeLayer(*asset, options);
  if (!qgisLayer || !qgisLayer->isValid()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.materialization_failed"),
        QStringLiteral(
            "QGIS could not create a valid layer for the Data Asset")));
  }

  data::Result<data::AssetLease> acquired = m_impl->dataManager->acquire(
      data::AssetRef{asset->id(), asset->revision()},
      data::AssetUse{data::LeaseKind::View,
                     QStringLiteral("QGIS display layer in view %1")
                         .arg(viewId.toString())});
  if (!acquired)
    return data::Result<DisplayLayerId>::failure(acquired.diagnostics());

  const DisplayLayerId layerId = DisplayLayerId::generate();
  qgisLayer->setCustomProperty(QStringLiteral("sicnu/assetId"),
                               assetId.toString());
  qgisLayer->setCustomProperty(QStringLiteral("sicnu/displayLayerId"),
                               layerId.toString());
  qgisLayer->setCustomProperty(QStringLiteral("sicnu/displayViewId"),
                               viewId.toString());

  QgsMapLayer *storedLayer =
      viewRecord->layerStore->addMapLayer(qgisLayer.get());
  if (!storedLayer) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.store_rejected_layer"),
        QStringLiteral(
            "The display view's layer store rejected the QGIS layer")));
  }
  qgisLayer.release();

  // A Vector Asset being edited in another view is read-only here: only the
  // Edit Lease owner may modify features. The owning layer is the one that
  // acquired the Edit Lease; every other Display Layer of the same asset stays
  // read-only until the edit session commits or rolls back.
  if (auto *vectorLayer = qobject_cast<QgsVectorLayer *>(storedLayer)) {
    if (m_impl->dataManager->hasActiveEditLease(assetId))
      vectorLayer->setReadOnly(true);
  }

  if (options.insertOnTop)
    viewRecord->layerTree->insertLayer(0, storedLayer);
  else
    viewRecord->layerTree->addLayer(storedLayer);
  if (viewRecord->bridge)
    viewRecord->bridge->setCanvasLayers();

  auto layerRecord = std::make_unique<Impl::LayerRecord>(Impl::LayerRecord{
      DisplayLayerSnapshot{layerId, viewId, assetId, storedLayer->id()},
      storedLayer, acquired.take()});
  m_impl->layers.emplace(layerId.toString(), std::move(layerRecord));
  if (options.insertOnTop)
    viewRecord->layerIds.prepend(layerId);
  else
    viewRecord->layerIds.append(layerId);

  return data::Result<DisplayLayerId>::success(layerId);
}

data::Result<DisplayLayerId>
QgisDisplayManager::cloneLayer(DisplayLayerId sourceLayerId,
                               DisplayViewId targetViewId) {
  if (QThread::currentThread() != thread()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.wrong_thread"),
        QStringLiteral(
            "Display mutations must run on the manager's owning thread")));
  }

  const Impl::LayerRecord *sourceRecord = m_impl->findLayer(sourceLayerId);
  if (!sourceRecord || sourceRecord->mapLayer.isNull()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.layer_not_found"),
        QStringLiteral("No live Display Layer matches the requested id")));
  }

  Impl::ViewRecord *targetView = m_impl->findView(targetViewId);
  if (!targetView) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.invalid_view"),
        QStringLiteral(
            "No registered target Display View matches the requested id")));
  }
  if (targetView->canvas.isNull() || targetView->layerTree.isNull() ||
      targetView->layerStore.isNull()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.view_unavailable"),
        QStringLiteral("The target Display View is no longer available")));
  }
  if (m_impl->dataManager.isNull()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.data_manager_unavailable"),
        QStringLiteral("The Data Manager is no longer available")));
  }

  const std::optional<data::AssetSnapshot> asset =
      m_impl->dataManager->asset(sourceRecord->snapshot.assetId());
  if (!asset || asset->state() != data::AssetState::Ready) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.asset_not_ready"),
        QStringLiteral(
            "The source Display Layer's Data Asset is unavailable")));
  }

  std::unique_ptr<QgsMapLayer> clonedLayer(sourceRecord->mapLayer->clone());
  if (!clonedLayer || !clonedLayer->isValid()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.clone_failed"),
        QStringLiteral("QGIS could not clone the source presentation")));
  }

  data::Result<data::AssetLease> acquired = m_impl->dataManager->acquire(
      data::AssetRef{asset->id(), asset->revision()},
      data::AssetUse{data::LeaseKind::View,
                     QStringLiteral("Cloned QGIS display layer in view %1")
                         .arg(targetViewId.toString())});
  if (!acquired)
    return data::Result<DisplayLayerId>::failure(acquired.diagnostics());

  const DisplayLayerId clonedId = DisplayLayerId::generate();
  clonedLayer->setCustomProperty(QStringLiteral("sicnu/assetId"),
                                 asset->id().toString());
  clonedLayer->setCustomProperty(QStringLiteral("sicnu/displayLayerId"),
                                 clonedId.toString());
  clonedLayer->setCustomProperty(QStringLiteral("sicnu/displayViewId"),
                                 targetViewId.toString());

  QgsMapLayer *storedLayer =
      targetView->layerStore->addMapLayer(clonedLayer.get());
  if (!storedLayer) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.store_rejected_layer"),
        QStringLiteral(
            "The target layer store rejected the cloned QGIS layer")));
  }
  clonedLayer.release();

  targetView->layerTree->insertLayer(0, storedLayer);
  if (targetView->bridge)
    targetView->bridge->setCanvasLayers();

  auto record = std::make_unique<Impl::LayerRecord>(
      Impl::LayerRecord{DisplayLayerSnapshot{clonedId, targetViewId,
                                             asset->id(), storedLayer->id()},
                        storedLayer, acquired.take()});
  m_impl->layers.emplace(clonedId.toString(), std::move(record));
  targetView->layerIds.prepend(clonedId);
  return data::Result<DisplayLayerId>::success(clonedId);
}

data::Result<DisplayLayerId>
QgisDisplayManager::adoptLayer(DisplayViewId viewId, data::AssetId assetId,
                               QgsMapLayer *mapLayer,
                               const AdoptLayerOptions &options) {
  if (QThread::currentThread() != thread()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.wrong_thread"),
        QStringLiteral(
            "Display mutations must run on the manager's owning thread")));
  }

  Impl::ViewRecord *viewRecord = m_impl->findView(viewId);
  if (!viewRecord || viewRecord->canvas.isNull() ||
      viewRecord->layerTree.isNull() || viewRecord->layerStore.isNull()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.view_unavailable"),
        QStringLiteral("The target Display View is unavailable")));
  }
  if (m_impl->dataManager.isNull()) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.data_manager_unavailable"),
        QStringLiteral("The Data Manager is no longer available")));
  }

  const std::optional<data::AssetSnapshot> asset =
      m_impl->dataManager->asset(assetId);
  if (!asset) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.asset_not_found"),
        QStringLiteral("No registered Data Asset matches the requested id")));
  }

  // A Ready asset requires a live QGIS layer. A Missing asset is still adopted
  // so its Display Layer record survives project load; the layer QGIS produced
  // for the absent source may be invalid, and relocateLayer() rebuilds it once
  // the source is recovered.
  const bool assetAvailable = asset->state() == data::AssetState::Ready &&
      asset->capabilities().testFlag(data::AssetCapability::Renderable);
  const bool layerBelongsToView =
      mapLayer && viewRecord->layerStore->mapLayer(mapLayer->id()) == mapLayer;
  if (assetAvailable && (!mapLayer || !mapLayer->isValid() || !layerBelongsToView)) {
    return data::Result<DisplayLayerId>::failure(
        displayDiagnostic(QStringLiteral("display.layer_not_adoptable"),
                          QStringLiteral("The QGIS layer is invalid or does "
                                         "not belong to the target view")));
  }
  if (!assetAvailable && !layerBelongsToView) {
    return data::Result<DisplayLayerId>::failure(
        displayDiagnostic(QStringLiteral("display.layer_not_adoptable"),
                          QStringLiteral("The QGIS layer does not belong to "
                                         "the target view")));
  }

  for (const auto &[key, record] : m_impl->layers) {
    (void)key;
    if (record->mapLayer == mapLayer)
      return data::Result<DisplayLayerId>::success(record->snapshot.id());
  }

  const DisplayLayerId layerId =
      options.displayLayerId.value_or(DisplayLayerId::generate());
  if (m_impl->findLayer(layerId)) {
    return data::Result<DisplayLayerId>::failure(displayDiagnostic(
        QStringLiteral("display.layer_id_conflict"),
        QStringLiteral("The persisted Display Layer ID is already in use")));
  }

  data::Result<data::AssetLease> acquired = m_impl->dataManager->acquire(
      data::AssetRef{asset->id(), asset->revision()},
      data::AssetUse{data::LeaseKind::View,
                     QStringLiteral("Adopted QGIS display layer in view %1")
                         .arg(viewId.toString())});
  if (!acquired)
    return data::Result<DisplayLayerId>::failure(acquired.diagnostics());

  mapLayer->setCustomProperty(QStringLiteral("sicnu/assetId"),
                              assetId.toString());
  mapLayer->setCustomProperty(QStringLiteral("sicnu/displayLayerId"),
                              layerId.toString());
  mapLayer->setCustomProperty(QStringLiteral("sicnu/displayViewId"),
                              viewId.toString());

  auto record = std::make_unique<Impl::LayerRecord>(Impl::LayerRecord{
      DisplayLayerSnapshot{layerId, viewId, assetId, mapLayer->id()}, mapLayer,
      acquired.take()});
  m_impl->layers.emplace(layerId.toString(), std::move(record));
  viewRecord->layerIds.append(layerId);
  if (viewRecord->bridge)
    viewRecord->bridge->setCanvasLayers();
  return data::Result<DisplayLayerId>::success(layerId);
}

data::Result<void> QgisDisplayManager::relocateLayer(DisplayLayerId layerId) {
  if (QThread::currentThread() != thread()) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.wrong_thread"),
        QStringLiteral(
            "Display mutations must run on the manager's owning thread")));
  }

  const auto layerIt = m_impl->layers.find(layerId.toString());
  if (layerIt == m_impl->layers.end()) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.layer_not_found"),
        QStringLiteral("No Display Layer matches the requested id")));
  }

  Impl::LayerRecord *layerRecord = layerIt->second.get();
  Impl::ViewRecord *viewRecord = m_impl->findView(layerRecord->snapshot.viewId());
  if (!viewRecord || viewRecord->layerTree.isNull() ||
      viewRecord->layerStore.isNull()) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.view_unavailable"),
        QStringLiteral("The display view's QGIS objects are no longer available")));
  }
  if (m_impl->dataManager.isNull()) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.data_manager_unavailable"),
        QStringLiteral("The Data Manager is no longer available")));
  }

  const std::optional<data::AssetSnapshot> asset =
      m_impl->dataManager->asset(layerRecord->snapshot.assetId());
  if (!asset) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.asset_not_found"),
        QStringLiteral("No registered Data Asset matches the layer's asset id")));
  }

  // A still-missing asset keeps its Display Layer identity without materializing
  // a replacement; there is no source to open yet.
  if (asset->state() != data::AssetState::Ready)
    return data::Result<void>::success();

  // Capture the authoritative runtime presentation state before replacing the
  // layer, so the relocated layer keeps its renderer.
  QgsMapLayerStyle presentation;
  if (!layerRecord->mapLayer.isNull())
    presentation.readFromLayer(layerRecord->mapLayer);

  // Remember the tree position so the replacement lands in the same slot.
  const QString oldQgisLayerId = layerRecord->snapshot.qgisLayerId();
  int treeIndex = -1;
  QgsLayerTreeGroup *parentGroup = nullptr;
  if (QgsLayerTreeLayer *oldNode = viewRecord->layerTree->findLayer(oldQgisLayerId)) {
    parentGroup = qobject_cast<QgsLayerTreeGroup *>(oldNode->parent());
    if (parentGroup)
      treeIndex = parentGroup->children().indexOf(oldNode);
  }

  std::unique_ptr<QgsMapLayer> replacementLayer =
      materializeLayer(*asset, AddLayerOptions{});
  if (!replacementLayer || !replacementLayer->isValid()) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.materialization_failed"),
        QStringLiteral(
            "QGIS could not create a replacement layer for the relocated asset")));
  }

  // Restore the captured presentation state onto the new layer.
  presentation.writeToLayer(replacementLayer.get());

  // Acquire a lease pinned to the asset's new revision.
  data::Result<data::AssetLease> acquired = m_impl->dataManager->acquire(
      data::AssetRef{asset->id(), asset->revision()},
      data::AssetUse{data::LeaseKind::View,
                     QStringLiteral("Relocated QGIS display layer in view %1")
                         .arg(layerRecord->snapshot.viewId().toString())});
  if (!acquired)
    return data::Result<void>::failure(acquired.diagnostics());

  const DisplayLayerId keptId = layerRecord->snapshot.id();
  replacementLayer->setCustomProperty(QStringLiteral("sicnu/assetId"),
                                      asset->id().toString());
  replacementLayer->setCustomProperty(QStringLiteral("sicnu/displayLayerId"),
                                      keptId.toString());
  replacementLayer->setCustomProperty(QStringLiteral("sicnu/displayViewId"),
                                      layerRecord->snapshot.viewId().toString());

  QgsMapLayer *storedLayer =
      viewRecord->layerStore->addMapLayer(replacementLayer.get());
  if (!storedLayer) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.store_rejected_layer"),
        QStringLiteral("The layer store rejected the replacement QGIS layer")));
  }
  replacementLayer.release();

  // Replace the tree node in place, preserving the original position.
  if (parentGroup && treeIndex >= 0) {
    parentGroup->insertLayer(treeIndex, storedLayer);
  } else {
    viewRecord->layerTree->addLayer(storedLayer);
  }
  if (QgsLayerTreeLayer *oldNode = viewRecord->layerTree->findLayer(oldQgisLayerId)) {
    if (QgsLayerTreeGroup *parent = qobject_cast<QgsLayerTreeGroup *>(oldNode->parent()))
      parent->removeChildNode(oldNode);
  }

  // Remove the stale layer from the store after the replacement is registered.
  if (viewRecord->layerStore->mapLayer(oldQgisLayerId))
    viewRecord->layerStore->removeMapLayer(oldQgisLayerId);
  if (viewRecord->bridge)
    viewRecord->bridge->setCanvasLayers();

  // Update the record: same DisplayLayerId and asset, new QGIS layer and lease.
  // The old lease is released by reassigning the move-only AssetLease.
  layerRecord->snapshot = DisplayLayerSnapshot{
      keptId, layerRecord->snapshot.viewId(), asset->id(), storedLayer->id()};
  layerRecord->mapLayer = storedLayer;
  layerRecord->lease = acquired.take();

  return data::Result<void>::success();
}

data::Result<void> QgisDisplayManager::removeLayer(DisplayLayerId layerId) {
  if (QThread::currentThread() != thread()) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.wrong_thread"),
        QStringLiteral(
            "Display mutations must run on the manager's owning thread")));
  }

  const auto layerIt = m_impl->layers.find(layerId.toString());
  if (layerIt == m_impl->layers.end()) {
    return data::Result<void>::failure(displayDiagnostic(
        QStringLiteral("display.layer_not_found"),
        QStringLiteral("No Display Layer matches the requested id")));
  }

  Impl::LayerRecord *layerRecord = layerIt->second.get();
  Impl::ViewRecord *viewRecord =
      m_impl->findView(layerRecord->snapshot.viewId());
  const QString qgisLayerId = layerRecord->snapshot.qgisLayerId();

  if (viewRecord) {
    viewRecord->layerIds.removeAll(layerId);
    if (viewRecord->layerTree) {
      if (QgsLayerTreeLayer *node =
              viewRecord->layerTree->findLayer(qgisLayerId)) {
        if (QgsLayerTreeGroup *parent =
                qobject_cast<QgsLayerTreeGroup *>(node->parent())) {
          parent->removeChildNode(node);
        }
      }
    }

    if (viewRecord->layerStore &&
        viewRecord->layerStore->mapLayer(qgisLayerId)) {
      viewRecord->layerStore->removeMapLayer(qgisLayerId);
    }
    if (viewRecord->bridge)
      viewRecord->bridge->setCanvasLayers();
  }

  m_impl->layers.erase(layerIt);
  return data::Result<void>::success();
}

std::optional<DisplayViewSnapshot>
QgisDisplayManager::view(DisplayViewId viewId) const {
  const Impl::ViewRecord *record = m_impl->findView(viewId);
  if (!record)
    return std::nullopt;
  return DisplayViewSnapshot{record->id, record->layerIds};
}

std::optional<DisplayLayerSnapshot>
QgisDisplayManager::layer(DisplayLayerId layerId) const {
  const Impl::LayerRecord *record = m_impl->findLayer(layerId);
  if (!record)
    return std::nullopt;
  return record->snapshot;
}

QgsMapLayer *QgisDisplayManager::mapLayer(DisplayLayerId layerId) const {
  const Impl::LayerRecord *record = m_impl->findLayer(layerId);
  return record ? record->mapLayer.data() : nullptr;
}

} // namespace sicnu::display
