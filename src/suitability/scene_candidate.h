#pragma once

// scene_candidate.h — self-contained per-scene projection for suitability
// assessment.
//
// The assessor core only sees this DTO; it never imports STAC/GDAL headers.
// A candidate carries honest gaps: an absent meter GSD stays absent (the
// CRS-unit pixel size is evidence, never a stand-in for meters), and an
// unknown acquisition time stays unknown.

#include "../data/asset_types.h"
#include "../data/data_asset.h"
#include "../data/data_result.h"
#include "../data/raster_grid_compat.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace sicnu::suitability
{

inline constexpr int kSceneCandidateSerializationVersion = 1;

struct SceneCandidate
{
    QString id;
    sicnu::data::AssetState state = sicnu::data::AssetState::Registered;
    QString crsWkt;
    sicnu::data::SpatialExtent extent;
    /// Ground sample distance in meters. Set only from explicit evidence
    /// (caller/STAC hint); never derived from CRS-unit pixel sizes.
    std::optional<double> gsdM;
    /// Raw pixel size in CRS units — provenance evidence only.
    std::optional<double> pixelSizeCrsUnits;
    std::optional<QDateTime> acquisitionTimeUtc;
    /// 0-100; empty = unknown. Out-of-range values are carried as-is and
    /// downgraded to "unknown" by the cloud criterion (never clamped).
    std::optional<double> cloudCoverPercent;
    /// band_role.h vocabulary strings; empty = unknown.
    QStringList bandRoles;
    int bandCount = 0;
    std::optional<sicnu::data::RasterGrid> grid;
    /// "optical"/"sar"/...; empty = unknown.
    QString modality;
    /// Passthrough evidence owned by the caller.
    QJsonObject provenance;

    bool usable() const { return state == sicnu::data::AssetState::Ready; }

    QJsonObject toJson() const;
    static sicnu::data::Result<SceneCandidate> fromJson( const QJsonObject &json );
};

/// Projects a catalog RasterStructure onto a SceneCandidate. Extent, CRS,
/// band count and the CRS-unit pixel size come from @p structure; band roles
/// are collected from bands with a known role via bandRoleToString (first
/// occurrence order, deduplicated).
///
/// Optional hints (unknown keys ignored):
///  - "gsd_m" (double): meter GSD; the only source of SceneCandidate::gsdM;
///  - "acquisition_time_utc" (string): ISO 8601 (Qt::ISODate); a zoneless
///    timestamp is read as UTC; an unparsable value leaves the field unset;
///  - "cloud_cover_percent" (double);
///  - "modality" (string).
SceneCandidate sceneCandidateFromRasterStructure(
    const QString &id, sicnu::data::AssetState state,
    const sicnu::data::RasterStructure &structure, const QJsonObject &hints = {} );

} // namespace sicnu::suitability
