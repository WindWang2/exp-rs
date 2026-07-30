// src/agent/workspace_snapshot.h
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

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
    QString kind = QStringLiteral( "Unknown" );
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

    QJsonObject toJson() const;
    QString toSystemPromptHeader() const;
};

} // namespace sicnu::agent
