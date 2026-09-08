// patch_generator.cpp — patch spec generation.
#include "patch_generator.h"

#include "dataset_fingerprint.h"
#include "dataset_ids.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>

#include <algorithm>
#include <cmath>

namespace sicnu::dataset
{

QString patchStrategyToString( PatchStrategy strategy )
{
    switch ( strategy )
    {
        case PatchStrategy::FixedGrid: return QStringLiteral( "fixed_grid" );
        case PatchStrategy::SlidingWindow: return QStringLiteral( "sliding_window" );
        case PatchStrategy::Random: return QStringLiteral( "random" );
        case PatchStrategy::RoiCentered: return QStringLiteral( "roi_centered" );
        case PatchStrategy::ObjectCentered: return QStringLiteral( "object_centered" );
        case PatchStrategy::Stratified: return QStringLiteral( "stratified" );
    }
    return QString();
}

std::optional<PatchStrategy> patchStrategyFromString( const QString &text )
{
    if ( text == QLatin1String( "fixed_grid" ) )
        return PatchStrategy::FixedGrid;
    if ( text == QLatin1String( "sliding_window" ) )
        return PatchStrategy::SlidingWindow;
    if ( text == QLatin1String( "random" ) )
        return PatchStrategy::Random;
    if ( text == QLatin1String( "roi_centered" ) )
        return PatchStrategy::RoiCentered;
    if ( text == QLatin1String( "object_centered" ) )
        return PatchStrategy::ObjectCentered;
    if ( text == QLatin1String( "stratified" ) )
        return PatchStrategy::Stratified;
    return std::nullopt;
}

sicnu::data::Result<void> PatchGeneratorConfig::validate() const
{
    using Result = sicnu::data::Result<void>;
    auto fail = []( const QString &message ) {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.patch_config_invalid" ),
                                            message, DiagnosticSeverity::Error } );
    };
    if ( windowWidth <= 0 || windowHeight <= 0 )
        return fail( QStringLiteral( "window size must be positive" ) );
    if ( ( strategy == PatchStrategy::FixedGrid || strategy == PatchStrategy::SlidingWindow ) &&
         ( strideX <= 0 || strideY <= 0 ) )
        return fail( QStringLiteral( "grid strategies require positive strides" ) );
    if ( ( strategy == PatchStrategy::Random || strategy == PatchStrategy::Stratified ) &&
         randomCount <= 0 )
        return fail( QStringLiteral( "random strategies require randomCount > 0" ) );
    if ( noDataMode == NoDataMode::MinValidFraction || noDataMode == NoDataMode::MaxNoDataFraction )
    {
        if ( noDataThreshold <= 0.0 || noDataThreshold >= 1.0 )
            return fail( QStringLiteral( "fraction thresholds must be in (0,1)" ) );
    }
    return Result::success();
}

QJsonObject PatchGeneratorConfig::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "strategy" ), patchStrategyToString( strategy ) );
    json.insert( QStringLiteral( "window_width" ), windowWidth );
    json.insert( QStringLiteral( "window_height" ), windowHeight );
    json.insert( QStringLiteral( "stride_x" ), strideX );
    json.insert( QStringLiteral( "stride_y" ), strideY );
    json.insert( QStringLiteral( "random_count" ), randomCount );
    json.insert( QStringLiteral( "seed" ), qint64( seed ) );
    json.insert( QStringLiteral( "border_policy" ), borderPolicyToString( borderPolicy ) );
    json.insert( QStringLiteral( "nodata_mode" ), noDataModeToString( noDataMode ) );
    json.insert( QStringLiteral( "nodata_threshold" ), noDataThreshold );
    return json;
}

sicnu::data::Result<PatchGeneratorConfig> PatchGeneratorConfig::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<PatchGeneratorConfig>;
    PatchGeneratorConfig config;
    const auto strategy =
        patchStrategyFromString( json.value( QStringLiteral( "strategy" ) ).toString() );
    if ( !strategy )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.patch_config_invalid" ),
                                            QStringLiteral( "strategy unknown" ),
                                            DiagnosticSeverity::Error } );
    config.strategy = *strategy;
    config.windowWidth = json.value( QStringLiteral( "window_width" ) ).toInteger( 256 );
    config.windowHeight = json.value( QStringLiteral( "window_height" ) ).toInteger( 256 );
    config.strideX = json.value( QStringLiteral( "stride_x" ) ).toInteger( 256 );
    config.strideY = json.value( QStringLiteral( "stride_y" ) ).toInteger( 256 );
    config.randomCount = json.value( QStringLiteral( "random_count" ) ).toInt( 100 );
    config.seed = quint64( qMax<qint64>( 0, json.value( QStringLiteral( "seed" ) ).toInteger() ) );
    const auto border =
        borderPolicyFromString( json.value( QStringLiteral( "border_policy" ) ).toString() );
    if ( !border )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.patch_config_invalid" ),
                                            QStringLiteral( "border policy unknown" ),
                                            DiagnosticSeverity::Error } );
    config.borderPolicy = *border;
    const auto noData =
        noDataModeFromString( json.value( QStringLiteral( "nodata_mode" ) ).toString() );
    if ( !noData )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.patch_config_invalid" ),
                                            QStringLiteral( "nodata mode unknown" ),
                                            DiagnosticSeverity::Error } );
    config.noDataMode = *noData;
    config.noDataThreshold = json.value( QStringLiteral( "nodata_threshold" ) ).toDouble();
    const auto validated = config.validate();
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    return Result::success( config );
}

QString PatchGenerator::configHash( const PatchGeneratorConfig &config )
{
    return makeDatasetFingerprint( config.toJson() ).toHex();
}

namespace
{

/// Applies BorderPolicy to one candidate window; returns false when the
/// policy says drop.
bool applyBorder( const PatchGeneratorConfig &config, const PixelWindow &candidate,
                  qint64 rasterWidth, qint64 rasterHeight, PixelWindow &out )
{
    const bool inside =
        candidate.x >= 0 && candidate.y >= 0 &&
        candidate.x + candidate.width <= rasterWidth &&
        candidate.y + candidate.height <= rasterHeight;
    if ( inside )
    {
        out = candidate;
        return true;
    }
    switch ( config.borderPolicy )
    {
        case BorderPolicy::Drop:
            return false;
        case BorderPolicy::Clip:
        {
            const PixelWindow clipped = candidate.intersected(
                PixelWindow{ 0, 0, rasterWidth, rasterHeight } );
            if ( !clipped.isValid() )
                return false;
            out = clipped;
            return true;
        }
        case BorderPolicy::Pad:
        case BorderPolicy::Reflect:
        case BorderPolicy::Constant:
            // The window stays full-size; out-of-bounds pixels are filled at
            // READ time by the consumer (the spec records the policy).
            out = candidate;
            return true;
    }
    return false;
}

/// Applies the NoData policy; returns false when the patch is dropped.
bool applyNoData( const PatchGeneratorConfig &config,
                  const PatchGenerator::ValidFractionReader &reader, GeneratedPatch &patch )
{
    switch ( config.noDataMode )
    {
        case NoDataMode::Drop:
            patch.validityFlag = true;
            return true;
        case NoDataMode::KeepWithFlag:
            if ( reader )
                patch.validFraction = reader( patch.window );
            // Flag = "carries any valid pixel"; unknown fraction stays true.
            patch.validityFlag = patch.validFraction != 0.0;
            return true;
        case NoDataMode::MinValidFraction:
        {
            if ( !reader )
                return true; // no reader: cannot evaluate; spec kept, unflagged
            patch.validFraction = reader( patch.window );
            patch.validityFlag = patch.validFraction >= config.noDataThreshold;
            return patch.validityFlag;
        }
        case NoDataMode::MaxNoDataFraction:
        {
            // Contract: drop when the NODATA fraction exceeds the threshold.
            // validFraction is the VALID share, so the nodata share is
            // 1 - validFraction.
            if ( !reader )
                return true;
            patch.validFraction = reader( patch.window );
            patch.validityFlag = ( 1.0 - patch.validFraction ) <= config.noDataThreshold;
            return patch.validityFlag;
        }
    }
    return true;
}

} // namespace

sicnu::data::Result<QVector<GeneratedPatch>> PatchGenerator::generate(
    const PatchGeneratorConfig &config, qint64 rasterWidth, qint64 rasterHeight,
    const GeoTransform &transform, const ValidFractionReader &validFractionReader,
    const QVector<PatchAnchor> &anchors )
{
    using Result = sicnu::data::Result<QVector<GeneratedPatch>>;
    const auto validated = config.validate();
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    if ( rasterWidth <= 0 || rasterHeight <= 0 )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.patch_config_invalid" ),
                                            QStringLiteral( "raster extent must be positive" ),
                                            DiagnosticSeverity::Error } );
    if ( ( config.strategy == PatchStrategy::RoiCentered ||
           config.strategy == PatchStrategy::ObjectCentered ||
           config.strategy == PatchStrategy::Stratified ) &&
         anchors.isEmpty() )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.patch_config_invalid" ),
                                            QStringLiteral( "anchor strategies require anchors" ),
                                            DiagnosticSeverity::Error } );
    }

    const QString hash = configHash( config );
    QVector<GeneratedPatch> patches;

    auto emitPatch = [&]( const PixelWindow &candidate, const QString &anchorId,
                          const QString &classCode ) {
        PixelWindow window;
        if ( !applyBorder( config, candidate, rasterWidth, rasterHeight, window ) )
        {
            // BorderPolicy::Drop is RECORDED as a dropped spec, never
            // silently vanished (goal 34: no silent failures).
            GeneratedPatch dropped;
            dropped.window = candidate;
            dropped.borderApplied = config.borderPolicy;
            dropped.noDataApplied = config.noDataMode;
            dropped.generatorConfigHash = hash;
            dropped.anchorSampleId = anchorId;
            dropped.classCode = classCode;
            dropped.dropped = true;
            dropped.validityFlag = false;
            patches.append( dropped );
            return;
        }
        GeneratedPatch patch;
        patch.window = window;
        patch.borderApplied = config.borderPolicy;
        patch.noDataApplied = config.noDataMode;
        patch.generatorConfigHash = hash;
        patch.anchorSampleId = anchorId;
        patch.classCode = classCode;
        const auto footprint = groundFootprintForWindow( window, transform );
        patch.groundFootprintWkt = footprint ? footprint.value() : QString();
        if ( !applyNoData( config, validFractionReader, patch ) )
        {
            patch.dropped = true; // recorded, not silently vanished
        }
        patches.append( patch );
    };

    switch ( config.strategy )
    {
        case PatchStrategy::FixedGrid:
        case PatchStrategy::SlidingWindow:
        {
            const auto windows = gridWindows( rasterWidth, rasterHeight, config.windowWidth,
                                              config.windowHeight, config.strideX,
                                              config.strideY );
            for ( const PixelWindow &window : windows )
                emitPatch( window, QString(), QString() );
            break;
        }
        case PatchStrategy::Random:
        {
            DeterministicRandom random(
                DeterministicRandom::seedFor( config.seed, QStringLiteral( "patch.random" ) ) );
            for ( int i = 0; i < config.randomCount; ++i )
            {
                const qint64 maxX = qMax<qint64>( 1, rasterWidth - 1 );
                const qint64 maxY = qMax<qint64>( 1, rasterHeight - 1 );
                const qint64 x = qint64( random.uniform01() * double( maxX ) );
                const qint64 y = qint64( random.uniform01() * double( maxY ) );
                emitPatch( PixelWindow{ x, y, config.windowWidth, config.windowHeight },
                           QString(), QString() );
            }
            break;
        }
        case PatchStrategy::Stratified:
        {
            // Round-robin over class buckets (shuffled inside each) keeps
            // per-class counts balanced without any RNG ordering leaks.
            DeterministicRandom random(
                DeterministicRandom::seedFor( config.seed, QStringLiteral( "patch.stratified" ) ) );
            QMap<QString, QVector<PatchAnchor>> byClass;
            for ( const PatchAnchor &anchor : anchors )
                byClass[anchor.classCode].append( anchor );
            for ( auto it = byClass.begin(); it != byClass.end(); ++it )
            {
                QVector<PatchAnchor> bucket = it.value();
                random.shuffle( bucket );
                const int perClass =
                    std::max<int>( 1, config.randomCount / byClass.size() );
                for ( int i = 0; i < perClass && i < bucket.size(); ++i )
                {
                    const PatchAnchor &anchor = bucket.at( i );
                    // Center the window on the anchor's ground position.
                    const double pixelX = ( anchor.centerX - transform.values[0] ) /
                                          ( transform.values[1] != 0.0 ? transform.values[1] : 1.0 );
                    const double pixelY = ( anchor.centerY - transform.values[3] ) /
                                          ( transform.values[5] != 0.0 ? transform.values[5] : 1.0 );
                    const qint64 x = qint64( std::floor(
                        pixelX - double( config.windowWidth ) / 2.0 ) );
                    const qint64 y = qint64( std::floor(
                        pixelY - double( config.windowHeight ) / 2.0 ) );
                    emitPatch( PixelWindow{ x, y, config.windowWidth, config.windowHeight },
                               anchor.sampleId, anchor.classCode );
                }
            }
            break;
        }
        case PatchStrategy::RoiCentered:
        case PatchStrategy::ObjectCentered:
        {
            for ( const PatchAnchor &anchor : anchors )
            {
                const double pixelX = ( anchor.centerX - transform.values[0] ) /
                                      ( transform.values[1] != 0.0 ? transform.values[1] : 1.0 );
                const double pixelY = ( anchor.centerY - transform.values[3] ) /
                                      ( transform.values[5] != 0.0 ? transform.values[5] : 1.0 );
                const qint64 x = qint64(
                    std::floor( pixelX - double( config.windowWidth ) / 2.0 ) );
                const qint64 y = qint64(
                    std::floor( pixelY - double( config.windowHeight ) / 2.0 ) );
                emitPatch( PixelWindow{ x, y, config.windowWidth, config.windowHeight },
                           anchor.sampleId, anchor.classCode );
            }
            break;
        }
    }
    return Result::success( patches );
}

SampleRecord PatchGenerator::toSampleRecord( const GeneratedPatch &patch,
                                             const QString &datasetVersionId,
                                             const PatchGeneratorConfig &config )
{
    SampleRecord sample;
    sample.setSampleId( SampleId::generate().toString() );
    sample.setDatasetVersionId( datasetVersionId );
    sample.setKind( SampleKind::Patch );
    sample.setGroupId( patch.anchorSampleId ); // anchored patches group by anchor
    PatchSample payload;
    payload.window = patch.window;
    payload.groundFootprintWkt = patch.groundFootprintWkt;
    payload.borderPolicy = patch.borderApplied;
    payload.noDataMode = patch.noDataApplied;
    payload.noDataThreshold = config.noDataThreshold;
    payload.validFraction = patch.validFraction;
    payload.validityFlag = patch.validityFlag;
    payload.generatorConfigHash = patch.generatorConfigHash;
    sample.payload() = payload;
    QJsonObject provenance;
    provenance.insert( QStringLiteral( "strategy" ), patchStrategyToString( config.strategy ) );
    provenance.insert( QStringLiteral( "generator_config_hash" ), patch.generatorConfigHash );
    provenance.insert( QStringLiteral( "dropped_by_policy" ), patch.dropped );
    if ( !patch.anchorSampleId.isEmpty() )
        provenance.insert( QStringLiteral( "anchor_sample" ), patch.anchorSampleId );
    sample.provenance() = provenance;
    return sample;
}

} // namespace sicnu::dataset
