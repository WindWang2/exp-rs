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
    /// Semantic band roles (stable ids such as "nir", "red", "qa") for raster
    /// assets, one entry per band in band order. Empty strings mark bands with
    /// no known role; the list is empty entirely when the asset carries no
    /// product semantics (plain rasters) or is not a raster.
    QStringList bandRoles;
    int layerCount = 0;
    QString crsWkt;
};

struct MapViewSnapshot
{
    QString crsAuthId;
    QString extentStr;
    double scale = 0.0;
    QString activeLayerName;
    /// Active raster's band composition and display stretch, captured for the
    /// active layer when it is a raster. Empty (renderer unset) for vector /
    /// remote-map layers or when no layer is active — so the agent only sees
    /// band context it can actually reason about. Read-only: the snapshot never
    /// writes back to the canvas.
    struct ActiveRasterDisplay
    {
        /// "SingleBandGray" (uses grayBand) or "MultiBandColor" (uses red/green/blueBand).
        QString renderer;
        int grayBand = 0;     ///< 1-based; 0 when unused.
        int redBand = 0;      ///< 1-based; 0 when unused.
        int greenBand = 0;    ///< 1-based; 0 when unused.
        int blueBand = 0;     ///< 1-based; 0 when unused.
        /// Real Data Range (ADR 0008) of the band the stretch is computed on —
        /// the physical pixel bounds, not the 8-bit display range. Unset when
        /// statistics are unavailable.
        std::optional<double> dataMin;
        std::optional<double> dataMax;
        /// Display stretch window applied to that band, e.g. "StretchToMinimumMaximum".
        /// Empty when no contrast enhancement is set.
        QString stretchAlgorithm;
        /// Display min/max currently mapped (the enhanced window). Unset when
        /// the renderer carries no contrast enhancement.
        std::optional<double> displayMin;
        std::optional<double> displayMax;
        bool valid = false;
    };
    ActiveRasterDisplay activeRaster;
};

struct WorkspaceSnapshot
{
    QList<DataAssetInfo> assets;
    MapViewSnapshot mapView;

    static WorkspaceSnapshot capture( data::DataManager *dataManager, ActiveViewHost *viewHost = nullptr );

    QString toSystemPromptHeader() const;
};

} // namespace sicnu::agent
