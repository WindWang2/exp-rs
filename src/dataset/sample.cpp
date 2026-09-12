// sample.cpp — sample serialization + spatial contract implementation.
#include "sample.h"

#include <QJsonArray>

#include <cmath>

namespace sicnu::dataset
{

sicnu::data::Result<QString> groundFootprintForWindow( const PixelWindow &window,
                                                       const GeoTransform &transform )
{
    using Result = sicnu::data::Result<QString>;
    if ( !window.isValid() )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                            QStringLiteral( "window is not valid" ),
                                            DiagnosticSeverity::Error } );
    if ( !transform.isNorthUp() )
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.transform_rotated" ),
            QStringLiteral( "rotated geotransforms are not converted silently; "
                            "harmonize via the grid-contract adapter first" ),
            DiagnosticSeverity::Error } );

    // Half-open window → closed ground rectangle: left/top edges are exact,
    // right/bottom map the exclusive edge (x+w) rather than the last pixel.
    const double &originX = transform.values[0];
    const double &pixelX = transform.values[1];
    const double &originY = transform.values[3];
    const double &pixelY = transform.values[5];
    const double left = originX + pixelX * double( window.x );
    const double right = originX + pixelX * double( window.x + window.width );
    const double top = originY + pixelY * double( window.y );
    const double bottom = originY + pixelY * double( window.y + window.height );

    // WKT ring is CCW in the written order; exactness of the half-open
    // bounds matters more than ring orientation for audit predicates.
    const QString polygon = QStringLiteral(
                                "POLYGON((%1 %2, %3 %2, %3 %4, %1 %4, %1 %2))" )
                                .arg( left, 0, 'g', 17 )
                                .arg( top, 0, 'g', 17 )
                                .arg( right, 0, 'g', 17 )
                                .arg( bottom, 0, 'g', 17 );
    return Result::success( polygon );
}

QVector<PixelWindow> gridWindows( qint64 rasterWidth, qint64 rasterHeight,
                                  qint64 windowWidth, qint64 windowHeight,
                                  qint64 strideX, qint64 strideY )
{
    QVector<PixelWindow> windows;
    if ( rasterWidth <= 0 || rasterHeight <= 0 || windowWidth <= 0 || windowHeight <= 0 ||
         strideX <= 0 || strideY <= 0 )
        return windows;
    // Guard against absurd enumerations (a bad stride must not hang the
    // process); callers needing more tiles than this are misconfigured.
    constexpr qint64 kMaxWindows = 10000000;
    for ( qint64 y = 0; y < rasterHeight; y += strideY )
    {
        for ( qint64 x = 0; x < rasterWidth; x += strideX )
        {
            windows.append( PixelWindow{ x, y, windowWidth, windowHeight } );
            if ( windows.size() >= kMaxWindows )
                return windows;
        }
    }
    return windows;
}

namespace
{

QJsonObject encodeWindow( const PixelWindow &window )
{
    QJsonObject json;
    json.insert( QStringLiteral( "x" ), window.x );
    json.insert( QStringLiteral( "y" ), window.y );
    json.insert( QStringLiteral( "width" ), window.width );
    json.insert( QStringLiteral( "height" ), window.height );
    return json;
}

std::optional<PixelWindow> decodeWindow( const QJsonObject &json )
{
    PixelWindow window;
    window.x = json.value( QStringLiteral( "x" ) ).toInteger();
    window.y = json.value( QStringLiteral( "y" ) ).toInteger();
    window.width = json.value( QStringLiteral( "width" ) ).toInteger();
    window.height = json.value( QStringLiteral( "height" ) ).toInteger();
    if ( !window.isValid() )
        return std::nullopt;
    return window;
}

std::optional<SourceAssetRef> decodeSourceAsset( const QJsonObject &json )
{
    SourceAssetRef ref;
    ref.assetId = json.value( QStringLiteral( "asset_id" ) ).toString();
    if ( ref.assetId.isEmpty() )
        return std::nullopt;
    ref.revision = quint64( qMax<qint64>( 0, json.value( QStringLiteral( "revision" ) ).toInteger() ) );
    ref.role = json.value( QStringLiteral( "role" ) ).toString();
    const QString bands = json.value( QStringLiteral( "bands" ) ).toString();
    if ( !bands.isEmpty() )
        ref.bandReferences = bands.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
    return ref;
}

// --- payload encode/decode (kind ↔ payload coherence enforced by caller) ----

QJsonObject encodePayload( SampleKind kind, const SamplePayload &payload )
{
    QJsonObject json;
    switch ( kind )
    {
        case SampleKind::Point:
        {
            const auto &point = std::get<PointSample>( payload );
            json.insert( QStringLiteral( "x" ), point.x );
            json.insert( QStringLiteral( "y" ), point.y );
            break;
        }
        case SampleKind::Pixel:
        {
            const auto &pixel = std::get<PixelSample>( payload );
            json.insert( QStringLiteral( "column" ), pixel.column );
            json.insert( QStringLiteral( "row" ), pixel.row );
            break;
        }
        case SampleKind::Window:
        {
            const auto &window = std::get<WindowSample>( payload );
            json.insert( QStringLiteral( "window" ), encodeWindow( window.window ) );
            if ( !window.groundFootprintWkt.isEmpty() )
                json.insert( QStringLiteral( "ground_footprint" ), window.groundFootprintWkt );
            break;
        }
        case SampleKind::Patch:
        {
            const auto &patch = std::get<PatchSample>( payload );
            json.insert( QStringLiteral( "window" ), encodeWindow( patch.window ) );
            if ( !patch.groundFootprintWkt.isEmpty() )
                json.insert( QStringLiteral( "ground_footprint" ), patch.groundFootprintWkt );
            json.insert( QStringLiteral( "border_policy" ),
                         borderPolicyToString( patch.borderPolicy ) );
            json.insert( QStringLiteral( "nodata_mode" ), noDataModeToString( patch.noDataMode ) );
            json.insert( QStringLiteral( "nodata_threshold" ), patch.noDataThreshold );
            json.insert( QStringLiteral( "valid_fraction" ), patch.validFraction );
            json.insert( QStringLiteral( "validity_flag" ), patch.validityFlag );
            if ( !patch.generatorConfigHash.isEmpty() )
                json.insert( QStringLiteral( "generator_config_hash" ), patch.generatorConfigHash );
            break;
        }
        case SampleKind::Polygon:
        {
            json.insert( QStringLiteral( "wkt" ), std::get<PolygonSample>( payload ).wkt );
            break;
        }
        case SampleKind::Object:
        {
            const auto &object = std::get<ObjectSample>( payload );
            json.insert( QStringLiteral( "asset_id" ), object.assetId );
            json.insert( QStringLiteral( "object_ref" ), object.objectRef );
            if ( object.bounds.isValid() )
                json.insert( QStringLiteral( "bounds" ), encodeWindow( object.bounds ) );
            break;
        }
        case SampleKind::Pair:
        {
            const auto &pair = std::get<PairSample>( payload );
            json.insert( QStringLiteral( "primary_ref" ), pair.primaryRef );
            json.insert( QStringLiteral( "secondary_ref" ), pair.secondaryRef );
            json.insert( QStringLiteral( "pair_role" ), pair.pairRole );
            break;
        }
        case SampleKind::Temporal:
        {
            const auto &temporal = std::get<TemporalSample>( payload );
            QJsonArray observations;
            for ( const TemporalObservation &observation : temporal.observations )
            {
                QJsonObject item;
                item.insert( QStringLiteral( "time_utc" ),
                             observation.timeUtc.toString( Qt::ISODateWithMs ) );
                if ( !observation.assetId.isEmpty() )
                {
                    item.insert( QStringLiteral( "asset_id" ), observation.assetId );
                    item.insert( QStringLiteral( "revision" ), qint64( observation.revision ) );
                }
                item.insert( QStringLiteral( "missing" ), observation.missing );
                item.insert( QStringLiteral( "quality" ), observation.quality );
                observations.append( item );
            }
            json.insert( QStringLiteral( "observations" ), observations );
            if ( temporal.targetTimeUtc.isValid() )
                json.insert( QStringLiteral( "target_time_utc" ),
                             temporal.targetTimeUtc.toString( Qt::ISODateWithMs ) );
            if ( !temporal.temporalMask.isEmpty() )
                json.insert( QStringLiteral( "temporal_mask" ), temporal.temporalMask );
            break;
        }
        case SampleKind::MultiModal:
        {
            const auto &multi = std::get<MultiModalSample>( payload );
            QJsonArray members;
            for ( const MultiModalMember &member : multi.members )
            {
                QJsonObject item;
                item.insert( QStringLiteral( "modality" ), member.modality );
                item.insert( QStringLiteral( "member_sample_id" ), member.memberSampleId );
                item.insert( QStringLiteral( "required" ), member.required );
                item.insert( QStringLiteral( "missing_policy" ), member.missingPolicy );
                members.append( item );
            }
            json.insert( QStringLiteral( "members" ), members );
            break;
        }
    }
    return json;
}

sicnu::data::Result<SamplePayload> decodePayload( SampleKind kind, const QJsonObject &json )
{
    using Result = sicnu::data::Result<SamplePayload>;
    switch ( kind )
    {
        case SampleKind::Point:
        {
            PointSample point;
            point.x = json.value( QStringLiteral( "x" ) ).toDouble();
            point.y = json.value( QStringLiteral( "y" ) ).toDouble();
            if ( !std::isfinite( point.x ) || !std::isfinite( point.y ) )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "point coordinates must be finite" ),
                                                    DiagnosticSeverity::Error } );
            return Result::success( SamplePayload( point ) );
        }
        case SampleKind::Pixel:
        {
            PixelSample pixel;
            pixel.column = json.value( QStringLiteral( "column" ) ).toInteger();
            pixel.row = json.value( QStringLiteral( "row" ) ).toInteger();
            if ( pixel.column < 0 || pixel.row < 0 )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "pixel indices must be >= 0" ),
                                                    DiagnosticSeverity::Error } );
            return Result::success( SamplePayload( pixel ) );
        }
        case SampleKind::Window:
        {
            WindowSample window;
            const auto decoded = decodeWindow( json.value( QStringLiteral( "window" ) ).toObject() );
            if ( !decoded )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "window sample requires a valid window" ),
                                                    DiagnosticSeverity::Error } );
            window.window = *decoded;
            window.groundFootprintWkt =
                json.value( QStringLiteral( "ground_footprint" ) ).toString();
            return Result::success( SamplePayload( window ) );
        }
        case SampleKind::Patch:
        {
            PatchSample patch;
            const auto decoded = decodeWindow( json.value( QStringLiteral( "window" ) ).toObject() );
            if ( !decoded )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "patch sample requires a valid window" ),
                                                    DiagnosticSeverity::Error } );
            patch.window = *decoded;
            patch.groundFootprintWkt = json.value( QStringLiteral( "ground_footprint" ) ).toString();
            const auto border =
                borderPolicyFromString( json.value( QStringLiteral( "border_policy" ) ).toString() );
            if ( !border )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "patch border policy unknown" ),
                                                    DiagnosticSeverity::Error } );
            patch.borderPolicy = *border;
            const auto noData =
                noDataModeFromString( json.value( QStringLiteral( "nodata_mode" ) ).toString() );
            if ( !noData )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "patch nodata mode unknown" ),
                                                    DiagnosticSeverity::Error } );
            patch.noDataMode = *noData;
            patch.noDataThreshold = json.value( QStringLiteral( "nodata_threshold" ) ).toDouble();
            patch.validFraction = json.value( QStringLiteral( "valid_fraction" ) ).toDouble();
            patch.validityFlag = json.value( QStringLiteral( "validity_flag" ) ).toBool( true );
            patch.generatorConfigHash =
                json.value( QStringLiteral( "generator_config_hash" ) ).toString();
            return Result::success( SamplePayload( patch ) );
        }
        case SampleKind::Polygon:
        {
            PolygonSample polygon;
            polygon.wkt = json.value( QStringLiteral( "wkt" ) ).toString();
            if ( !polygon.wkt.startsWith( QLatin1String( "POLYGON" ), Qt::CaseInsensitive ) &&
                 !polygon.wkt.startsWith( QLatin1String( "MULTIPOLYGON" ), Qt::CaseInsensitive ) )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "polygon sample requires (MULTI)POLYGON wkt" ),
                                                    DiagnosticSeverity::Error } );
            return Result::success( SamplePayload( polygon ) );
        }
        case SampleKind::Object:
        {
            ObjectSample object;
            object.assetId = json.value( QStringLiteral( "asset_id" ) ).toString();
            object.objectRef = json.value( QStringLiteral( "object_ref" ) ).toString();
            if ( object.assetId.isEmpty() || object.objectRef.isEmpty() )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "object sample requires asset + object refs" ),
                                                    DiagnosticSeverity::Error } );
            object.bounds = decodeWindow( json.value( QStringLiteral( "bounds" ) ).toObject() )
                                .value_or( PixelWindow{} );
            return Result::success( SamplePayload( object ) );
        }
        case SampleKind::Pair:
        {
            PairSample pair;
            pair.primaryRef = json.value( QStringLiteral( "primary_ref" ) ).toString();
            pair.secondaryRef = json.value( QStringLiteral( "secondary_ref" ) ).toString();
            pair.pairRole = json.value( QStringLiteral( "pair_role" ) ).toString();
            if ( pair.primaryRef.isEmpty() || pair.secondaryRef.isEmpty() )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "pair sample requires both member refs" ),
                                                    DiagnosticSeverity::Error } );
            return Result::success( SamplePayload( pair ) );
        }
        case SampleKind::Temporal:
        {
            TemporalSample temporal;
            for ( const QJsonValue &value : json.value( QStringLiteral( "observations" ) ).toArray() )
            {
                const QJsonObject item = value.toObject();
                TemporalObservation observation;
                observation.timeUtc = QDateTime::fromString(
                    item.value( QStringLiteral( "time_utc" ) ).toString(), Qt::ISODateWithMs );
                if ( !observation.timeUtc.isValid() )
                    return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                        QStringLiteral( "temporal observation time invalid" ),
                                                        DiagnosticSeverity::Error } );
                observation.assetId = item.value( QStringLiteral( "asset_id" ) ).toString();
                observation.revision =
                    quint64( qMax<qint64>( 0, item.value( QStringLiteral( "revision" ) ).toInteger() ) );
                observation.missing = item.value( QStringLiteral( "missing" ) ).toBool( false );
                observation.quality = item.value( QStringLiteral( "quality" ) ).toDouble();
                temporal.observations.append( observation );
            }
            if ( temporal.observations.isEmpty() )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "temporal sample requires observations" ),
                                                    DiagnosticSeverity::Error } );
            temporal.targetTimeUtc = QDateTime::fromString(
                json.value( QStringLiteral( "target_time_utc" ) ).toString(), Qt::ISODateWithMs );
            temporal.temporalMask = json.value( QStringLiteral( "temporal_mask" ) ).toString();
            return Result::success( SamplePayload( temporal ) );
        }
        case SampleKind::MultiModal:
        {
            MultiModalSample multi;
            for ( const QJsonValue &value : json.value( QStringLiteral( "members" ) ).toArray() )
            {
                const QJsonObject item = value.toObject();
                MultiModalMember member;
                member.modality = item.value( QStringLiteral( "modality" ) ).toString();
                member.memberSampleId = item.value( QStringLiteral( "member_sample_id" ) ).toString();
                member.required = item.value( QStringLiteral( "required" ) ).toBool( true );
                member.missingPolicy =
                    item.value( QStringLiteral( "missing_policy" ) ).toString( QStringLiteral( "error" ) );
                if ( member.modality.isEmpty() || member.memberSampleId.isEmpty() )
                    return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                        QStringLiteral( "multimodal member requires modality + sample ref" ),
                                                        DiagnosticSeverity::Error } );
                multi.members.append( member );
            }
            if ( multi.members.isEmpty() )
                return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                    QStringLiteral( "multimodal sample requires members" ),
                                                    DiagnosticSeverity::Error } );
            return Result::success( SamplePayload( multi ) );
        }
    }
    return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                        QStringLiteral( "unknown sample kind" ),
                                        DiagnosticSeverity::Error } );
}

} // namespace

QJsonObject SampleRecord::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSampleSerializationVersion );
    json.insert( QStringLiteral( "sample_id" ), m_sampleId );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "kind" ), sampleKindToString( m_kind ) );
    if ( !m_groupId.isEmpty() )
        json.insert( QStringLiteral( "group_id" ), m_groupId );
    json.insert( QStringLiteral( "weight" ), m_weight );
    if ( m_timeUtc.isValid() )
        json.insert( QStringLiteral( "time_utc" ), m_timeUtc.toString( Qt::ISODateWithMs ) );
    if ( m_validFromUtc.isValid() )
        json.insert( QStringLiteral( "valid_from_utc" ),
                     m_validFromUtc.toString( Qt::ISODateWithMs ) );
    if ( m_validUntilUtc.isValid() )
        json.insert( QStringLiteral( "valid_until_utc" ),
                     m_validUntilUtc.toString( Qt::ISODateWithMs ) );
    if ( !m_crs.isEmpty() )
        json.insert( QStringLiteral( "crs" ), m_crs );
    json.insert( QStringLiteral( "quality" ), m_quality );

    QJsonArray sourceAssets;
    for ( const SourceAssetRef &ref : m_sourceAssets )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "asset_id" ), ref.assetId );
        item.insert( QStringLiteral( "revision" ), qint64( ref.revision ) );
        if ( !ref.role.isEmpty() )
            item.insert( QStringLiteral( "role" ), ref.role );
        sourceAssets.append( item );
    }
    json.insert( QStringLiteral( "source_assets" ), sourceAssets );
    json.insert( QStringLiteral( "provenance" ), m_provenance );
    json.insert( QStringLiteral( "payload" ), encodePayload( m_kind, m_payload ) );
    return json;
}

sicnu::data::Result<SampleRecord> SampleRecord::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<SampleRecord>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kSampleSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.sample_version" ),
            QStringLiteral( "sample schema version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kSampleSerializationVersion ),
            DiagnosticSeverity::Error,
        } );
    }

    SampleRecord sample;
    sample.m_sampleId = json.value( QStringLiteral( "sample_id" ) ).toString();
    sample.m_datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    if ( sample.m_sampleId.isEmpty() || sample.m_datasetVersionId.isEmpty() )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                            QStringLiteral( "sample requires sample_id + dataset_version_id" ),
                                            DiagnosticSeverity::Error } );
    }
    const auto kind = sampleKindFromString( json.value( QStringLiteral( "kind" ) ).toString() );
    if ( !kind )
    {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                            QStringLiteral( "sample kind unknown" ),
                                            DiagnosticSeverity::Error } );
    }
    sample.m_kind = *kind;
    sample.m_groupId = json.value( QStringLiteral( "group_id" ) ).toString();
    sample.m_weight = json.value( QStringLiteral( "weight" ) ).toDouble( 1.0 );
    sample.m_timeUtc = QDateTime::fromString(
        json.value( QStringLiteral( "time_utc" ) ).toString(), Qt::ISODateWithMs );
    sample.m_validFromUtc = QDateTime::fromString(
        json.value( QStringLiteral( "valid_from_utc" ) ).toString(), Qt::ISODateWithMs );
    sample.m_validUntilUtc = QDateTime::fromString(
        json.value( QStringLiteral( "valid_until_utc" ) ).toString(), Qt::ISODateWithMs );
    sample.m_crs = json.value( QStringLiteral( "crs" ) ).toString();
    sample.m_quality = json.value( QStringLiteral( "quality" ) ).toDouble( -1.0 );
    for ( const QJsonValue &value : json.value( QStringLiteral( "source_assets" ) ).toArray() )
    {
        const auto ref = decodeSourceAsset( value.toObject() );
        if ( !ref )
        {
            return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ),
                                                QStringLiteral( "source asset entry malformed" ),
                                                DiagnosticSeverity::Error } );
        }
        sample.m_sourceAssets.append( *ref );
    }
    sample.m_provenance = json.value( QStringLiteral( "provenance" ) ).toObject();

    auto payload = decodePayload( sample.m_kind,
                                  json.value( QStringLiteral( "payload" ) ).toObject() );
    if ( !payload )
        return Result::failure( payload.diagnostics() );
    sample.m_payload = payload.value();

    const auto validated = validateSample( sample );
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    return Result::success( sample );
}

sicnu::data::Result<void> validateSample( const SampleRecord &sample )
{
    using Result = sicnu::data::Result<void>;
    auto fail = []( const QString &message ) {
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.sample_invalid" ), message,
                                            DiagnosticSeverity::Error } );
    };
    if ( sample.sampleId().isEmpty() || sample.datasetVersionId().isEmpty() )
        return fail( QStringLiteral( "sample requires ids" ) );
    if ( !( sample.weight() > 0.0 ) || !std::isfinite( sample.weight() ) )
        return fail( QStringLiteral( "sample weight must be a positive finite number" ) );
    if ( sample.validFromUtc().isValid() && sample.validUntilUtc().isValid() &&
         sample.validFromUtc() > sample.validUntilUtc() )
        return fail( QStringLiteral( "sample validity window is empty (from > until)" ) );
    if ( sample.timeUtc().isValid() && sample.validFromUtc().isValid() &&
         sample.timeUtc() < sample.validFromUtc() )
        return fail( QStringLiteral( "sample observation time precedes its validity window" ) );
    if ( sample.timeUtc().isValid() && sample.validUntilUtc().isValid() &&
         sample.timeUtc() > sample.validUntilUtc() )
        return fail( QStringLiteral( "sample observation time exceeds its validity window" ) );

    const bool payloadMatches =
        ( sample.kind() == SampleKind::Point &&
          std::holds_alternative<PointSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::Pixel &&
          std::holds_alternative<PixelSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::Window &&
          std::holds_alternative<WindowSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::Patch &&
          std::holds_alternative<PatchSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::Polygon &&
          std::holds_alternative<PolygonSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::Object &&
          std::holds_alternative<ObjectSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::Pair &&
          std::holds_alternative<PairSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::Temporal &&
          std::holds_alternative<TemporalSample>( sample.payload() ) ) ||
        ( sample.kind() == SampleKind::MultiModal &&
          std::holds_alternative<MultiModalSample>( sample.payload() ) );
    if ( !payloadMatches )
        return fail( QStringLiteral( "sample kind does not match payload type" ) );

    if ( sample.kind() == SampleKind::Patch )
    {
        const auto &patch = std::get<PatchSample>( sample.payload() );
        if ( !patch.window.isValid() )
            return fail( QStringLiteral( "patch window is not valid" ) );
    }
    if ( sample.kind() == SampleKind::Window )
    {
        const auto &window = std::get<WindowSample>( sample.payload() );
        if ( !window.window.isValid() )
            return fail( QStringLiteral( "window is not valid" ) );
    }
    if ( sample.kind() == SampleKind::Temporal &&
         std::get<TemporalSample>( sample.payload() ).observations.isEmpty() )
        return fail( QStringLiteral( "temporal sample requires observations" ) );
    if ( sample.kind() == SampleKind::MultiModal &&
         std::get<MultiModalSample>( sample.payload() ).members.isEmpty() )
        return fail( QStringLiteral( "multimodal sample requires members" ) );
    return Result::success();
}

} // namespace sicnu::dataset
