// src/agent/workspace_snapshot.h
#pragma once

#include <QList>
#include <QString>

#include <optional>

#include "data/asset_types.h"

namespace sicnu::data
{
class DataManager;
}

class ActiveViewHost;

namespace sicnu::agent
{

struct DataAssetInfo
{
    QString id;
    QString displayName;
    QString path;
    /// Unset until capture() fills it from the asset; renders as "Unknown" in the prompt.
    std::optional<data::AssetKind> kind;
    int width = 0;
    int height = 0;
    int bandCount = 0;
    int layerCount = 0;
    QString crsWkt;
};

struct MapViewSnapshot
{
    QString crsAuthId;
    QString extentStr;
    double scale = 0.0;
    QString activeLayerName;
};

struct WorkspaceSnapshot
{
    QList<DataAssetInfo> assets;
    MapViewSnapshot mapView;

    static WorkspaceSnapshot capture( data::DataManager *dataManager, ActiveViewHost *viewHost = nullptr );

    QString toSystemPromptHeader() const;
};

} // namespace sicnu::agent
