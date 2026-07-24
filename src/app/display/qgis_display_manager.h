#pragma once

#include <memory>
#include <optional>

#include <QObject>
#include <QString>
#include <QUuid>
#include <QVector>

#include "data/data_asset.h"
#include "data/data_result.h"

class QgsLayerTree;
class QgsMapCanvas;
class QgsMapLayer;
class QgsMapLayerStore;

namespace sicnu::data {
class DataManager;
}

namespace sicnu::display {

class DisplayViewId {
public:
  DisplayViewId() = default;

  static DisplayViewId generate();

  bool isNull() const;
  QString toString() const;

  friend bool operator==(const DisplayViewId &,
                         const DisplayViewId &) = default;

private:
  explicit DisplayViewId(QUuid value);

  QUuid m_value;
};

class DisplayLayerId {
public:
  DisplayLayerId() = default;

  static DisplayLayerId generate();
  static std::optional<DisplayLayerId> fromString(const QString &text);

  bool isNull() const;
  QString toString() const;

  friend bool operator==(const DisplayLayerId &,
                         const DisplayLayerId &) = default;

private:
  explicit DisplayLayerId(QUuid value);

  QUuid m_value;
};

struct DisplayViewSpec {
  QgsMapCanvas *canvas = nullptr;
  QgsLayerTree *layerTree = nullptr;
  QgsMapLayerStore *layerStore = nullptr;
};

struct AddLayerOptions {
  QString displayName;
  bool insertOnTop = true;
};

struct AdoptLayerOptions {
  std::optional<DisplayLayerId> displayLayerId;
};

class DisplayViewSnapshot {
public:
  const DisplayViewId &id() const;
  const QVector<DisplayLayerId> &layerIds() const;

private:
  friend class QgisDisplayManager;

  DisplayViewSnapshot(DisplayViewId id, QVector<DisplayLayerId> layerIds);

  DisplayViewId m_id;
  QVector<DisplayLayerId> m_layerIds;
};

class DisplayLayerSnapshot {
public:
  const DisplayLayerId &id() const;
  const DisplayViewId &viewId() const;
  const data::AssetId &assetId() const;
  const QString &qgisLayerId() const;

private:
  friend class QgisDisplayManager;

  DisplayLayerSnapshot(DisplayLayerId id, DisplayViewId viewId,
                       data::AssetId assetId, QString qgisLayerId);

  DisplayLayerId m_id;
  DisplayViewId m_viewId;
  data::AssetId m_assetId;
  QString m_qgisLayerId;
};

/**
 * Adapts Data Assets into independent QGIS presentation instances.
 *
 * The canvas, layer tree, and layer store are supplied by the application and
 * remain outside the Data Manager. Each managed Display Layer owns one Data
 * Asset view lease; its QgsMapLayer is owned by the registered view's store.
 */
class QgisDisplayManager : public QObject {
public:
  explicit QgisDisplayManager(data::DataManager *dataManager,
                              QObject *parent = nullptr);
  ~QgisDisplayManager() override;

  data::Result<DisplayViewId> createView(const DisplayViewSpec &spec);
  data::Result<DisplayLayerId> addLayer(DisplayViewId viewId,
                                        data::AssetId assetId,
                                        const AddLayerOptions &options = {});
  data::Result<DisplayLayerId> cloneLayer(DisplayLayerId sourceLayerId,
                                          DisplayViewId targetViewId);
  data::Result<DisplayLayerId>
  adoptLayer(DisplayViewId viewId, data::AssetId assetId, QgsMapLayer *mapLayer,
             const AdoptLayerOptions &options = {});
  data::Result<void> removeLayer(DisplayLayerId layerId);

  std::optional<DisplayViewSnapshot> view(DisplayViewId viewId) const;
  std::optional<DisplayLayerSnapshot> layer(DisplayLayerId layerId) const;

  /// Non-owning access to the authoritative runtime presentation object.
  QgsMapLayer *mapLayer(DisplayLayerId layerId) const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace sicnu::display
