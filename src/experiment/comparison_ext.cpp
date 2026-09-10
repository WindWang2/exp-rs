// comparison_ext.cpp — see comparison_ext.h for the contract.
#include "comparison_ext.h"
#include <QJsonArray>

#include "../dataset/label_schema.h"

#include <QSet>

namespace sicnu::experiment
{

namespace
{

QStringList sortedDiff( const QStringList &a, const QStringList &b )
{
    QStringList onlyB;
    for ( const QString &value : b )
        if ( !a.contains( value ) )
            onlyB.append( value );
    onlyB.sort();
    return onlyB;
}

} // namespace

QJsonObject ProtocolCompatibility::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "compatible" ), compatible );
    json.insert( QStringLiteral( "differences" ), QJsonArray::fromStringList( differences ) );
    return json;
}

ProtocolCompatibility compareProtocols( const EvaluationProtocol &a, const EvaluationProtocol &b )
{
    ProtocolCompatibility result;
    result.compatible = true;
    auto check = [ & ]( const QString &dimension, bool differs, const QString &detail ) {
        if ( !differs )
            return;
        result.compatible = false;
        result.differences.append( QStringLiteral( "%1: %2" ).arg( dimension, detail ) );
    };
    check( QStringLiteral( "dataset_version" ), a.datasetVersionId() != b.datasetVersionId(),
           QStringLiteral( "%1 vs %2" ).arg( a.datasetVersionId(), b.datasetVersionId() ) );
    check( QStringLiteral( "split_manifest" ), a.splitManifestId() != b.splitManifestId(),
           QStringLiteral( "%1 vs %2" ).arg( a.splitManifestId(), b.splitManifestId() ) );
    check( QStringLiteral( "subset" ), a.subset() != b.subset(),
           QStringLiteral( "%1 vs %2" ).arg( a.subset(), b.subset() ) );
    QStringList sortedA = a.ignoreLabels();
    QStringList sortedB = b.ignoreLabels();
    sortedA.sort();
    sortedB.sort();
    const bool ignoreDiff = sortedA != sortedB;
    check( QStringLiteral( "ignore_labels" ), ignoreDiff,
           QStringLiteral( "%1 vs %2" )
               .arg( a.ignoreLabels().join( QLatin1Char( ',' ) ),
                     b.ignoreLabels().join( QLatin1Char( ',' ) ) ) );
    check( QStringLiteral( "mask" ), a.maskRef() != b.maskRef(),
           QStringLiteral( "%1 vs %2" ).arg( a.maskRef(), b.maskRef() ) );
    check( QStringLiteral( "iou_threshold" ),
           a.iouThreshold() != b.iouThreshold(),
           QStringLiteral( "%1 vs %2" ).arg( a.iouThreshold() ).arg( b.iouThreshold() ) );
    check( QStringLiteral( "confidence_threshold" ),
           a.confidenceThreshold() != b.confidenceThreshold(),
           QStringLiteral( "%1 vs %2" )
               .arg( a.confidenceThreshold() )
               .arg( b.confidenceThreshold() ) );
    check( QStringLiteral( "aggregation" ), a.aggregation() != b.aggregation(),
           QStringLiteral( "%1 vs %2" ).arg( a.aggregation(), b.aggregation() ) );
    return result;
}

QJsonObject SchemaCompatibility::toJson() const
{
    QJsonObject json;
    static const char *verdictNames[] = { "compatible", "compatible_with_differences",
                                          "not_comparable" };
    json.insert( QStringLiteral( "verdict" ),
                 QString::fromUtf8( verdictNames[int( verdict )] ) );
    json.insert( QStringLiteral( "added_codes" ), QJsonArray::fromStringList( addedCodes ) );
    json.insert( QStringLiteral( "removed_codes" ), QJsonArray::fromStringList( removedCodes ) );
    json.insert( QStringLiteral( "reasons" ), QJsonArray::fromStringList( reasons ) );
    return json;
}

SchemaCompatibility compareLabelSchemas( const sicnu::dataset::LabelSchema &a,
                                         const sicnu::dataset::LabelSchema &b )
{
    SchemaCompatibility result;
    if ( a.schemaId() == b.schemaId() && a.version() == b.version() )
    {
        result.verdict = SchemaCompatibility::Verdict::Compatible;
        return result;
    }

    QStringList codesA;
    for ( const auto &klass : a.classes() )
        codesA.append( klass.code() );
    QStringList codesB;
    for ( const auto &klass : b.classes() )
        codesB.append( klass.code() );
    result.addedCodes = sortedDiff( codesA, codesB );
    result.removedCodes = sortedDiff( codesB, codesA );

    if ( !a.schemaId().isEmpty() && !b.schemaId().isEmpty() && a.schemaId() != b.schemaId() )
    {
        result.verdict = SchemaCompatibility::Verdict::NotComparable;
        result.reasons.append( QStringLiteral( "different schema identities: %1 vs %2" )
                                   .arg( a.schemaId(), b.schemaId() ) );
        return result;
    }
    if ( !result.removedCodes.isEmpty() )
    {
        result.verdict = SchemaCompatibility::Verdict::NotComparable;
        result.reasons.append( QStringLiteral( "classes removed: %1" )
                                   .arg( result.removedCodes.join( QLatin1String( ", " ) ) ) );
        return result;
    }
    if ( a.schemaId() == b.schemaId() && a.version() != b.version() )
        result.reasons.append( QStringLiteral( "same schema, version %1 vs %2" )
                                   .arg( a.version() )
                                   .arg( b.version() ) );
    if ( !result.addedCodes.isEmpty() )
    {
        result.verdict = SchemaCompatibility::Verdict::CompatibleWithDifferences;
        result.reasons.append( QStringLiteral( "classes added: %1" )
                                   .arg( result.addedCodes.join( QLatin1String( ", " ) ) ) );
        return result;
    }
    result.verdict = SchemaCompatibility::Verdict::Compatible;
    return result;
}

QJsonObject PairedMetricDelta::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "metric" ), metric );
    json.insert( QStringLiteral( "a" ), valueA );
    json.insert( QStringLiteral( "b" ), valueB );
    json.insert( QStringLiteral( "delta" ), delta );
    if ( supportA >= 0 )
        json.insert( QStringLiteral( "support_a" ), supportA );
    if ( supportB >= 0 )
        json.insert( QStringLiteral( "support_b" ), supportB );
    if ( insufficientSupport )
        json.insert( QStringLiteral( "insufficient_support" ), true );
    return json;
}

QJsonObject PairedRunSummary::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "protocols_compatible" ), protocolsCompatible );
    json.insert( QStringLiteral( "schemas_compatible" ), schemasCompatible );
    QJsonArray deltaArray;
    for ( const auto &delta : deltas )
        deltaArray.append( delta.toJson() );
    json.insert( QStringLiteral( "deltas" ), deltaArray );
    json.insert( QStringLiteral( "notes" ), QJsonArray::fromStringList( notes ) );
    return json;
}

namespace
{

/// Recursively flattens numeric leaves; per-class objects keyed by class code
/// stay nested as "name::code" pairs with an optional support sibling.
void flattenMetrics( const QJsonObject &object, const QString &prefix,
                     QHash<QString, double> &scalars,
                     QHash<QString, QHash<QString, double>> &perClass,
                     QHash<QString, QHash<QString, qint64>> &perClassSupport )
{
    for ( auto it = object.constBegin(); it != object.constEnd(); ++it )
    {
        const QString key = prefix.isEmpty() ? it.key() : prefix + QLatin1String( "::" ) + it.key();
        const QJsonValue value = it.value();
        if ( value.isDouble() )
        {
            scalars.insert( key, value.toDouble() );
        }
        else if ( value.isObject() )
        {
            const QJsonObject child = value.toObject();
            // Per-class document convention: {"water": {"iou":…, "support":…}}
            bool classLike = !child.isEmpty();
            for ( auto cit = child.constBegin(); cit != child.constEnd(); ++cit )
            {
                if ( !cit.value().isObject() )
                {
                    classLike = false;
                    break;
                }
            }
            if ( classLike )
            {
                for ( auto cit = child.constBegin(); cit != child.constEnd(); ++cit )
                {
                    const QJsonObject classDoc = cit.value().toObject();
                    for ( auto mit = classDoc.constBegin(); mit != classDoc.constEnd(); ++mit )
                    {
                        if ( mit.value().isDouble() )
                        {
                            if ( mit.key() == QLatin1String( "support" ) )
                                perClassSupport[key][cit.key()] = qint64( mit.value().toDouble() );
                            else
                                perClass[key][cit.key() + QLatin1String( "::" ) + mit.key()] =
                                    mit.value().toDouble();
                        }
                    }
                }
            }
            else
            {
                flattenMetrics( child, key, scalars, perClass, perClassSupport );
            }
        }
    }
}

} // namespace

PairedRunSummary pairedRunComparison( const MetricRecord &a, const MetricRecord &b,
                                      const SchemaCompatibility *schemaCompat,
                                      qint64 minTestClassSupport )
{
    PairedRunSummary summary;
    const auto protocolCompat = compareProtocols( a.protocol, b.protocol );
    summary.protocolsCompatible = protocolCompat.compatible;
    if ( !protocolCompat.compatible )
        summary.notes.append(
            QStringLiteral( "protocols differ; deltas are reported but are NOT comparable"
                            " evidence" ) );
    summary.schemasCompatible =
        !schemaCompat ||
        schemaCompat->verdict != SchemaCompatibility::Verdict::NotComparable;

    QHash<QString, double> scalarsA;
    QHash<QString, double> scalarsB;
    QHash<QString, QHash<QString, double>> perClassA;
    QHash<QString, QHash<QString, double>> perClassB;
    QHash<QString, QHash<QString, qint64>> supportA;
    QHash<QString, QHash<QString, qint64>> supportB;
    flattenMetrics( a.metrics, QString(), scalarsA, perClassA, supportA );
    flattenMetrics( b.metrics, QString(), scalarsB, perClassB, supportB );

    // Scalar leaves present in both documents.
    QStringList scalarNames;
    for ( auto it = scalarsA.constBegin(); it != scalarsA.constEnd(); ++it )
        if ( scalarsB.contains( it.key() ) )
            scalarNames.append( it.key() );
    scalarNames.sort();
    for ( const QString &name : scalarNames )
    {
        PairedMetricDelta delta;
        delta.metric = name;
        delta.valueA = scalarsA.value( name );
        delta.valueB = scalarsB.value( name );
        delta.delta = delta.valueB - delta.valueA;
        summary.deltas.append( delta );
    }

    // Per-class leaves: matched by (family, class code), with support gates.
    QSet<QString> matchedFamilies;
    for ( auto it = perClassA.constBegin(); it != perClassA.constEnd(); ++it )
    {
        if ( !perClassB.contains( it.key() ) )
            continue;
        matchedFamilies.insert( it.key() );
        const auto &classMetricsA = it.value();
        const auto &classMetricsB = perClassB.value( it.key() );
        QStringList metricNames;
        for ( auto mit = classMetricsA.constBegin(); mit != classMetricsA.constEnd(); ++mit )
            if ( classMetricsB.contains( mit.key() ) )
                metricNames.append( mit.key() );
        metricNames.sort();
        for ( const QString &name : metricNames )
        {
            PairedMetricDelta delta;
            delta.metric = it.key() + QLatin1String( "::" ) + name;
            delta.valueA = classMetricsA.value( name );
            delta.valueB = classMetricsB.value( name );
            delta.delta = delta.valueB - delta.valueA;
            // "family::code" — support lookup drops the "::metric" suffix.
            const QString code = name.section( QLatin1String( "::" ), 0, 0 );
            const qint64 familySupportA = supportA.value( it.key() ).value( code, -1 );
            const qint64 familySupportB = supportB.value( it.key() ).value( code, -1 );
            delta.supportA = familySupportA;
            delta.supportB = familySupportB;
            if ( familySupportA >= 0 && familySupportA < minTestClassSupport )
                delta.insufficientSupport = true;
            if ( familySupportB >= 0 && familySupportB < minTestClassSupport )
                delta.insufficientSupport = true;
            summary.deltas.append( delta );
        }
    }

    if ( !summary.protocolsCompatible || !summary.schemasCompatible )
        summary.notes.append( QStringLiteral(
            "paired comparison requires equal evaluation protocols and comparable"
            " class schemas; consult the flags before citing any delta" ) );
    return summary;
}

} // namespace sicnu::experiment
