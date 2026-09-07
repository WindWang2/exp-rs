// dataset_quality.cpp — composition statistics + label QA.
#include "dataset_quality.h"

#include "wkt.h"

#include <QJsonObject>

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::dataset
{

namespace
{

QString resolutionKey( double resolution )
{
    if ( resolution <= 0.0 )
        return QStringLiteral( "unknown" );
    return QString::number( std::round( resolution * 100.0 ) / 100.0, 'f', 2 );
}

QJsonObject hashToJason( const QHash<QString, qint64> &table )
{
    QJsonObject json;
    for ( auto it = table.constBegin(); it != table.constEnd(); ++it )
        json.insert( it.key(), it.value() );
    return json;
}

} // namespace

QJsonObject DatasetComposition::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "sample_count" ), sampleCount );
    json.insert( QStringLiteral( "by_class" ), hashToJason( byClass ) );
    json.insert( QStringLiteral( "by_sensor" ), hashToJason( bySensor ) );
    json.insert( QStringLiteral( "by_region" ), hashToJason( byRegion ) );
    json.insert( QStringLiteral( "by_season" ), hashToJason( bySeason ) );
    json.insert( QStringLiteral( "by_modality" ), hashToJason( byModality ) );
    json.insert( QStringLiteral( "by_resolution" ), hashToJason( byResolution ) );
    QJsonObject years;
    for ( auto it = byYear.constBegin(); it != byYear.constEnd(); ++it )
        years.insert( QString::number( it.key() ), it.value() );
    json.insert( QStringLiteral( "by_year" ), years );
    json.insert( QStringLiteral( "missing_time_count" ), missingTimeCount );
    json.insert( QStringLiteral( "unknown_class_count" ), unknownClassCount );
    json.insert( QStringLiteral( "zero_weight_count" ), zeroWeightCount );
    return json;
}

DatasetComposition computeComposition( const QVector<CompositionRow> &rows,
                                       const LabelSchema *knownClasses )
{
    DatasetComposition composition;
    for ( const CompositionRow &row : rows )
    {
        ++composition.sampleCount;
        if ( !knownClasses || knownClasses->classByCode( row.classCode ) ||
             row.classCode.isEmpty() )
            ++composition.byClass[row.classCode.isEmpty() ? QStringLiteral( "(unlabeled)" )
                                                          : row.classCode];
        else
        {
            ++composition.unknownClassCount;
            ++composition.byClass[QStringLiteral( "(unknown)" )];
        }
        ++composition.bySensor[row.sensor.isEmpty() ? QStringLiteral( "(unknown)" )
                                                    : row.sensor];
        ++composition.byRegion[row.region.isEmpty() ? QStringLiteral( "(unknown)" )
                                                    : row.region];
        ++composition.bySeason[row.season.isEmpty() ? QStringLiteral( "(unknown)" )
                                                    : row.season];
        ++composition.byModality[row.modality.isEmpty() ? QStringLiteral( "(unknown)" )
                                                        : row.modality];
        ++composition.byResolution[resolutionKey( row.resolution )];
        if ( row.year > 0 )
            ++composition.byYear[row.year];
        else
            ++composition.missingTimeCount;
        if ( !( row.weight > 0.0 ) )
            ++composition.zeroWeightCount;
    }
    return composition;
}

namespace
{

QVector<ImbalanceFinding> dimensionImbalance( const QString &dimension,
                                              const QHash<QString, qint64> &table,
                                              double imbalanceRatio )
{
    QVector<ImbalanceFinding> findings;
    qint64 maxValue = 0;
    qint64 minNonZero = std::numeric_limits<qint64>::max();
    int nonZeroBuckets = 0;
    for ( auto it = table.constBegin(); it != table.constEnd(); ++it )
    {
        if ( it.value() <= 0 )
            continue;
        ++nonZeroBuckets;
        maxValue = qMax( maxValue, it.value() );
        minNonZero = qMin( minNonZero, it.value() );
    }
    if ( nonZeroBuckets < 2 || minNonZero <= 0 )
        return findings;
    if ( double( maxValue ) / double( minNonZero ) >= imbalanceRatio )
    {
        ImbalanceFinding finding;
        finding.dimension = dimension;
        finding.detail = QStringLiteral( "max bucket %1 vs min non-zero bucket %2" )
                             .arg( maxValue )
                             .arg( minNonZero );
        finding.distribution = hashToJason( table );
        findings.append( finding );
    }
    return findings;
}

} // namespace

QVector<ImbalanceFinding> imbalanceFindings( const DatasetComposition &composition,
                                             double imbalanceRatio )
{
    QVector<ImbalanceFinding> findings;
    findings.append( dimensionImbalance( QStringLiteral( "class" ), composition.byClass,
                                         imbalanceRatio ) );
    findings.append( dimensionImbalance( QStringLiteral( "region" ), composition.byRegion,
                                         imbalanceRatio ) );
    findings.append( dimensionImbalance( QStringLiteral( "sensor" ), composition.bySensor,
                                         imbalanceRatio ) );
    findings.append( dimensionImbalance( QStringLiteral( "season" ), composition.bySeason,
                                         imbalanceRatio ) );
    findings.append( dimensionImbalance( QStringLiteral( "resolution" ),
                                         composition.byResolution, imbalanceRatio ) );
    return findings;
}

QVector<LabelQualityFinding> labelQualityAudit( const QVector<LabelQaItem> &items,
                                                const LabelSchema &schema,
                                                const LabelQualityConfig &config )
{
    QVector<LabelQualityFinding> findings;
    auto add = [&]( DiagnosticSeverity severity, const QString &code, const QString &sampleId,
                    const QString &annotationId, const QString &message,
                    const QJsonObject &evidence = {} ) {
        LabelQualityFinding finding;
        finding.severity = severity;
        finding.code = code;
        finding.sampleId = sampleId;
        finding.annotationId = annotationId;
        finding.message = message;
        finding.evidence = evidence;
        findings.append( finding );
    };

    for ( const LabelQaItem &item : items )
    {
        // Empty label: no tip annotations at all.
        if ( item.annotationIds.isEmpty() )
        {
            add( DiagnosticSeverity::Warning, QStringLiteral( "label.empty" ), item.sampleId,
                 QString(), QStringLiteral( "sample carries no annotation" ) );
            continue;
        }

        QSet<QString> seenSignatures;
        bool sawClass = false;
        QString firstClass;
        bool conflicting = false;
        for ( int i = 0; i < item.annotationIds.size(); ++i )
        {
            const QString &annotationId = item.annotationIds.at( i );
            const QString &classCode = i < item.classCodes.size() ? item.classCodes.at( i )
                                                                  : QString();
            const QString &wkt = i < item.geometryWkts.size() ? item.geometryWkts.at( i )
                                                              : QString();

            // Unknown class (when a class code is present).
            if ( !classCode.isEmpty() )
            {
                sawClass = true;
                if ( firstClass.isEmpty() )
                    firstClass = classCode;
                else if ( classCode != firstClass )
                    conflicting = true;
                if ( !schema.classByCode( classCode ) )
                {
                    add( DiagnosticSeverity::Error, QStringLiteral( "label.unknown_class" ),
                         item.sampleId, annotationId,
                         QStringLiteral( "class '%1' is not in schema '%2'" )
                             .arg( classCode, schema.name() ) );
                }
            }

            // Geometry checks.
            if ( !wkt.isEmpty() )
            {
                int ringCount = 0;
                const auto polygon = parseWktPolygon( wkt, &ringCount );
                if ( !polygon )
                {
                    add( DiagnosticSeverity::Error, QStringLiteral( "label.invalid_geometry" ),
                         item.sampleId, annotationId, polygon.diagnostics().first().message );
                }
                else
                {
                    if ( ringCount > 1 )
                    {
                        add( DiagnosticSeverity::Info, QStringLiteral( "label.multipart" ),
                             item.sampleId, annotationId,
                             QStringLiteral( "geometry has %1 rings; audits cover the first" )
                                 .arg( ringCount ) );
                    }
                    const SimplePolygon &shape = polygon.value();
                    const double area = std::fabs( shape.maxX() - shape.minX() ) *
                                        std::fabs( shape.maxY() - shape.minY() );
                    if ( config.tinyPolygonArea > 0.0 && area < config.tinyPolygonArea )
                    {
                        add( DiagnosticSeverity::Warning, QStringLiteral( "label.tiny_polygon" ),
                             item.sampleId, annotationId,
                             QStringLiteral( "bbox area %1 below threshold %2" )
                                 .arg( area )
                                 .arg( config.tinyPolygonArea ) );
                    }
                    if ( config.hasRasterExtent &&
                         ( shape.maxX() < config.rasterMinX || shape.minX() > config.rasterMaxX ||
                           shape.maxY() < config.rasterMinY || shape.minY() > config.rasterMaxY ) )
                    {
                        add( DiagnosticSeverity::Error, QStringLiteral( "label.outside_raster" ),
                             item.sampleId, annotationId,
                             QStringLiteral( "geometry outside the raster extent" ) );
                    }
                    // Duplicate detection: same class + geometry signature.
                    const QString signature = classCode + QLatin1Char( '|' ) + wkt;
                    if ( seenSignatures.contains( signature ) )
                    {
                        add( DiagnosticSeverity::Warning,
                             QStringLiteral( "label.duplicate_annotation" ), item.sampleId,
                             annotationId, QStringLiteral( "identical annotation already present" ) );
                    }
                    seenSignatures.insert( signature );
                }
            }
        }

        // Conflicting tip labels on one sample.
        if ( conflicting )
        {
            add( DiagnosticSeverity::Error, QStringLiteral( "label.conflict" ), item.sampleId,
                 QString(), QStringLiteral( "sample carries multiple disagreeing tip labels" ) );
        }
        // Annotations exist but none carries a class.
        if ( !sawClass && !item.classCodes.isEmpty() )
        {
            add( DiagnosticSeverity::Warning, QStringLiteral( "label.no_class" ), item.sampleId,
                 QString(), QStringLiteral( "annotations present without any class code" ) );
        }
    }
    return findings;
}

DatasetQualityLevel recommendQualityLevel( const QVector<LabelQualityFinding> &findings )
{
    bool hasError = false;
    bool hasWarning = false;
    for ( const LabelQualityFinding &finding : findings )
    {
        if ( finding.severity == DiagnosticSeverity::Error )
            hasError = true;
        else if ( finding.severity == DiagnosticSeverity::Warning )
            hasWarning = true;
    }
    if ( hasError )
        return DatasetQualityLevel::Draft;
    if ( hasWarning )
        return DatasetQualityLevel::Valid;
    return DatasetQualityLevel::Certified;
}

} // namespace sicnu::dataset
