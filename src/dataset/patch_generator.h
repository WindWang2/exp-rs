// patch_generator.h — unified patch generation (goal §17, ADR 0135/0136).
//
// The generator produces PATCH SPECS (windows + policy + provenance), not
// pixel buffers: source rasters are read by the caller through bounded
// window IO, and the module stays GDAL-free. Every generated patch records
// the generator-config hash and the policies actually applied, so a patch
// proves its own origin.
//
// Pixel access for NoData/valid-fraction policies flows through a
// caller-provided reader callback — injectable, bounded, cancellable.
#pragma once

#include "deterministic_random.h"
#include "sample.h"

#include "../data/data_result.h"

#include <functional>
#include <QString>
#include <QVector>

namespace sicnu::dataset
{

/// Patch window strategy (goal §17). Pair/temporal/multimodal patches are
/// sample-model compositions (PairSample/TemporalSample/MultiModalSample);
/// the generator covers the window-based families.
enum class PatchStrategy
{
    FixedGrid,     ///< regular grid over the raster extent
    SlidingWindow, ///< grid with explicit stride < window (high overlap)
    Random,        ///< seeded uniform sampling of window origins
    RoiCentered,   ///< one window centered on each ROI (polygon bounds)
    ObjectCentered,///< one window centered on each object's bounds
    Stratified,    ///< random origins balanced per class code
};

QString patchStrategyToString( PatchStrategy strategy );
std::optional<PatchStrategy> patchStrategyFromString( const QString &text );

struct PatchGeneratorConfig
{
    PatchStrategy strategy = PatchStrategy::FixedGrid;
    qint64 windowWidth = 256;
    qint64 windowHeight = 256;
    qint64 strideX = 256;  ///< FixedGrid/SlidingWindow
    qint64 strideY = 256;
    int randomCount = 100; ///< Random/Stratified
    quint64 seed = 0;      ///< required for Random/Stratified
    BorderPolicy borderPolicy = BorderPolicy::Drop;
    NoDataMode noDataMode = NoDataMode::KeepWithFlag;
    double noDataThreshold = 0.0;
    /// Band/time selection ride in provenance, not the config identity —
    /// the config hash covers geometry/policy only.

    sicnu::data::Result<void> validate() const;
    QJsonObject toJson() const;
    static sicnu::data::Result<PatchGeneratorConfig> fromJson( const QJsonObject &json );
};

/// A ROI/object anchor: bounds + optional class code (for Stratified).
struct PatchAnchor
{
    QString sampleId;      ///< the ROI/object sample this anchor came from
    QString classCode;
    double centerX = 0.0;  ///< ground coordinates
    double centerY = 0.0;
};

/// One generated patch spec: everything a SampleRecord payload needs plus
/// the generator provenance.
struct GeneratedPatch
{
    PixelWindow window;        ///< in the SOURCE raster pixel grid
    QString groundFootprintWkt;
    BorderPolicy borderApplied = BorderPolicy::Drop;
    NoDataMode noDataApplied = NoDataMode::KeepWithFlag;
    double validFraction = -1.0;
    bool validityFlag = true;
    bool dropped = false;      ///< policy decided to drop (kept for reporting)
    QString generatorConfigHash;
    QString anchorSampleId;    ///< RoiCentered/ObjectCentered/Stratified
    QString classCode;
};

class PatchGenerator
{
  public:
    /// SHA-256 hex of the canonical config JSON (patch provenance).
    static QString configHash( const PatchGeneratorConfig &config );

    /// Generates patches over a raster extent (pixel size). @p transform
    /// converts windows to ground footprints (north-up only). NoDataPolicy
    /// evaluation uses @p validFractionReader when the mode needs fractions;
    /// a null reader with a fraction policy is a validation error.
    using ValidFractionReader = std::function<double( const PixelWindow & )>;
    static sicnu::data::Result<QVector<GeneratedPatch>> generate(
        const PatchGeneratorConfig &config, qint64 rasterWidth, qint64 rasterHeight,
        const GeoTransform &transform, const ValidFractionReader &validFractionReader,
        const QVector<PatchAnchor> &anchors = {} );

    /// Converts a generated patch into a storable SampleRecord (kind Patch)
    /// with the envelope filled from the generator context.
    static SampleRecord toSampleRecord( const GeneratedPatch &patch,
                                        const QString &datasetVersionId,
                                        const PatchGeneratorConfig &config );
};

} // namespace sicnu::dataset
