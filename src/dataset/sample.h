// sample.h — typed sample model (ADR 0135).
//
// One shared envelope + typed payloads (std::variant), not N independent
// sample systems. The envelope carries identity/provenance/grouping; each
// payload kind carries exactly the shape its family needs. Payloads never
// inline pixel bytes — raster content stays in the source assets and the
// artifact layer; samples reference windows into them.
//
// Spatial contract (goal §12): pixel windows are HALF-OPEN [x, x+w); ground
// footprints derive from the source geotransform as axis-aligned WKT
// polygons in the source CRS; this module NEVER resamples or reprojects —
// a mismatched grid is a validation error, and harmonization belongs to
// Track A's grid contracts behind an adapter.
#pragma once

#include "dataset_manifest.h" // SourceAssetRef reuse
#include "dataset_types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <array>
#include <variant>

namespace sicnu::dataset
{

/// Serialization schema version stamped into every sample payload.
inline constexpr int kSampleSerializationVersion = 1;

/// Half-open pixel window [x, x+w) × [y, y+h) in source pixel coordinates.
struct PixelWindow
{
    qint64 x = 0;
    qint64 y = 0;
    qint64 width = 0;
    qint64 height = 0;

    bool isValid() const { return width > 0 && height > 0; }
    bool contains( qint64 px, qint64 py ) const
    {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    bool intersects( const PixelWindow &other ) const
    {
        return x < other.x + other.width && other.x < x + width &&
               y < other.y + other.height && other.y < y + height;
    }
    /// Intersection; invalid (empty) window when disjoint.
    PixelWindow intersected( const PixelWindow &other ) const
    {
        const qint64 left = qMax( x, other.x );
        const qint64 top = qMax( y, other.y );
        const qint64 right = qMin( x + width, other.x + other.width );
        const qint64 bottom = qMin( y + height, other.y + other.height );
        if ( right <= left || bottom <= top )
            return PixelWindow{};
        return PixelWindow{ left, top, right - left, bottom - top };
    }
    /// Overlap fraction relative to this window (0 when disjoint).
    double overlapFraction( const PixelWindow &other ) const
    {
        if ( width <= 0 || height <= 0 )
            return 0.0;
        const PixelWindow common = intersected( other );
        if ( !common.isValid() )
            return 0.0;
        return double( common.width ) * double( common.height ) /
               ( double( width ) * double( height ) );
    }

    friend bool operator==( const PixelWindow &, const PixelWindow & ) = default;
};

/// GDAL-style geotransform: [originX, pixelSizeX, rowRotationX, originY,
/// rowRotationY... following GDAL order (gt[0..5]). Only the north-up case
/// (gt[2] == gt[4] == 0) is converted here; rotated rasters are rejected —
/// silently treating them as north-up would corrupt footprints.
struct GeoTransform
{
    std::array<double, 6> values{};
    bool isNorthUp() const { return values[2] == 0.0 && values[4] == 0.0; }
};

/// Deterministic window ↔ ground conversion (north-up only). Returns an
/// axis-aligned WKT POLYGON in the source CRS, clamped to the half-open
/// window bounds. Fails with `dataset.transform_rotated` for rotated
/// transforms.
sicnu::data::Result<QString> groundFootprintForWindow( const PixelWindow &window,
                                                       const GeoTransform &transform );

/// Deterministic grid enumeration of windows over a raster extent
/// (rasterWidth/Height in pixels). The last column/row may overhang; the
/// BorderPolicy of the caller decides what happens to it — the enumeration
/// itself is pure and total.
QVector<PixelWindow> gridWindows( qint64 rasterWidth, qint64 rasterHeight,
                                  qint64 windowWidth, qint64 windowHeight,
                                  qint64 strideX, qint64 strideY );

// --- payload kinds -----------------------------------------------------------

struct PointSample
{
    double x = 0.0;
    double y = 0.0;
    friend bool operator==( const PointSample &, const PointSample & ) = default;
};

struct PixelSample
{
    qint64 column = 0;
    qint64 row = 0;
    friend bool operator==( const PixelSample &, const PixelSample & ) = default;
};

struct WindowSample
{
    PixelWindow window;
    QString groundFootprintWkt; ///< empty = derived, not stored
    friend bool operator==( const WindowSample &, const WindowSample & ) = default;
};

struct PatchSample
{
    PixelWindow window;
    QString groundFootprintWkt;
    /// Policies ACTUALLY applied at generation (recorded on the patch,
    /// goal §17) — never re-derived at read time.
    BorderPolicy borderPolicy = BorderPolicy::Drop;
    NoDataMode noDataMode = NoDataMode::KeepWithFlag;
    double noDataThreshold = 0.0; ///< fraction threshold for the mode, when used
    double validFraction = -1.0;  ///< observed at generation; -1 = not computed
    bool validityFlag = true;     ///< keep-with-flag outcome
    /// SHA-256 hex of the canonical generator configuration that produced
    /// this patch — two patches with the same hash came from the same recipe.
    QString generatorConfigHash;
    friend bool operator==( const PatchSample &, const PatchSample & ) = default;
};

struct PolygonSample
{
    QString wkt;
    friend bool operator==( const PolygonSample &, const PolygonSample & ) = default;
};

/// Reference to a segmentation object (e.g. an OBIA segment).
struct ObjectSample
{
    QString assetId;    ///< segmentation raster/vector asset
    QString objectRef;  ///< object/segment id within that asset
    PixelWindow bounds; ///< optional bounding window in that asset
    friend bool operator==( const ObjectSample &, const ObjectSample & ) = default;
};

/// Pre/post (or bi-temporal) pair referencing two member samples by id.
struct PairSample
{
    QString primaryRef;   ///< e.g. "pre" member SampleId
    QString secondaryRef; ///< e.g. "post" member SampleId
    QString pairRole;     ///< "pre_post", "bi_temporal", …
    friend bool operator==( const PairSample &, const PairSample & ) = default;
};

/// One observation of a temporal sample. `missing=true` records a KNOWN
/// missing acquisition — the hole is information, never silently dropped.
struct TemporalObservation
{
    QDateTime timeUtc;
    QString assetId;
    quint64 revision = 0;
    bool missing = false;
    double quality = -1.0; ///< -1 = unknown
    friend bool operator==( const TemporalObservation &, const TemporalObservation & ) = default;
};

struct TemporalSample
{
    QVector<TemporalObservation> observations; ///< arbitrary (irregular) times
    QDateTime targetTimeUtc;                   ///< prediction/label target time
    QString temporalMask;                      ///< optional named mask recipe
    friend bool operator==( const TemporalSample &, const TemporalSample & ) = default;
};

/// One modality member of a multi-modal sample.
struct MultiModalMember
{
    QString modality;       ///< "optical", "sar", "dem", "label", …
    QString memberSampleId; ///< SampleId of the member sample
    bool required = true;
    /// Missing policy (goal §14): what evaluation/consumers do when this
    /// member is absent. "error" | "skip" | "mask" | custom recipe name.
    QString missingPolicy = QStringLiteral( "error" );
    friend bool operator==( const MultiModalMember &, const MultiModalMember & ) = default;
};

struct MultiModalSample
{
    QVector<MultiModalMember> members;
    friend bool operator==( const MultiModalSample &, const MultiModalSample & ) = default;
};

using SamplePayload = std::variant<std::monostate, PointSample, PixelSample, WindowSample,
                                   PatchSample, PolygonSample, ObjectSample, PairSample,
                                   TemporalSample, MultiModalSample>;

/// The shared envelope: identity, ownership, grouping, quality, provenance.
class SampleRecord
{
  public:
    SampleRecord() = default;

    const QString &sampleId() const { return m_sampleId; }
    void setSampleId( const QString &id ) { m_sampleId = id; }
    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    SampleKind kind() const { return m_kind; }
    void setKind( SampleKind kind ) { m_kind = kind; }

    /// Split/leakage group (scene, object, region, event). Empty = ungrouped.
    const QString &groupId() const { return m_groupId; }
    void setGroupId( const QString &id ) { m_groupId = id; }
    /// Sampling/loss weight (> 0; default 1).
    double weight() const { return m_weight; }
    void setWeight( double weight ) { m_weight = weight; }
    /// Primary observation time (empty for time-invariant samples).
    const QDateTime &timeUtc() const { return m_timeUtc; }
    void setTimeUtc( const QDateTime &time ) { m_timeUtc = time; }
    /// CRS of the payload geometry (authority string or WKT); empty = none.
    const QString &crs() const { return m_crs; }
    void setCrs( const QString &crs ) { m_crs = crs; }
    /// Label quality prior in [0,1]; -1 = unknown.
    double quality() const { return m_quality; }
    void setQuality( double quality ) { m_quality = quality; }

    const QVector<SourceAssetRef> &sourceAssets() const { return m_sourceAssets; }
    QVector<SourceAssetRef> &sourceAssets() { return m_sourceAssets; }
    const QJsonObject &provenance() const { return m_provenance; }
    QJsonObject &provenance() { return m_provenance; }

    const SamplePayload &payload() const { return m_payload; }
    SamplePayload &payload() { return m_payload; }

    // -- serialization -------------------------------------------------------
    QJsonObject toJson() const;
    static sicnu::data::Result<SampleRecord> fromJson( const QJsonObject &json );

    friend bool operator==( const SampleRecord &, const SampleRecord & ) = default;

  private:
    QString m_sampleId;
    QString m_datasetVersionId;
    SampleKind m_kind = SampleKind::Point;
    QString m_groupId;
    double m_weight = 1.0;
    QDateTime m_timeUtc;
    QString m_crs;
    double m_quality = -1.0;
    QVector<SourceAssetRef> m_sourceAssets;
    QJsonObject m_provenance;
    SamplePayload m_payload;
};

/// Validates envelope/payload coherence: kind ↔ payload must match, ids must
/// be present, weight must be positive, pair/temporal refs non-empty,
/// patch windows valid. Diagnostics carry `dataset.sample_invalid`.
sicnu::data::Result<void> validateSample( const SampleRecord &sample );

} // namespace sicnu::dataset
