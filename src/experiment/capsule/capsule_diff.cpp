// capsule_diff.cpp — see capsule_diff.h.
#include "capsule_diff.h"

#include "data/execution_fingerprint.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>

namespace sicnu::experiment::capsule
{

namespace
{

/// Identity sections (ADR 0137 pins + what was produced). Any difference is
/// a semantic break.
const QStringList kIdentitySections{
    QStringLiteral( "goal" ),         QStringLiteral( "capabilities" ),
    QStringLiteral( "inputs" ),       QStringLiteral( "parameters" ),
    QStringLiteral( "plan" ),         QStringLiteral( "outputs" ),
    QStringLiteral( "provenance" ),
};

/// Reported sections: same experiment, different machine or moment.
const QStringList kReportedSections{
    QStringLiteral( "environment" ), QStringLiteral( "evidence" ),
    QStringLiteral( "created_utc" ), QStringLiteral( "capsule_id" ),
};

/// Compact scalar rendering for diff entries (strings verbatim, everything
/// else via its JSON value).
QString renderValue( const QJsonValue &value )
{
    if ( value.isString() )
        return value.toString();
    if ( value.isNull() || value.isUndefined() )
        return QStringLiteral( "<absent>" );
    return value.toVariant().toString();
}

QString kindOf( const QString &section )
{
    return kIdentitySections.contains( section ) ? QLatin1String( "identity" )
                                                 : QLatin1String( "reported" );
}

/// Fast path: identical digests ⇒ Identical without section walking.
bool digestsEqual( const CapsuleDocument &a, const CapsuleDocument &b )
{
    return a.digestValue() == b.digestValue() && a.digestValid() && b.digestValid();
}

/// Depth-first comparison of one member path; emits an entry per differing
/// leaf path.
void diffValue( const QString &path, const QJsonValue &left, const QJsonValue &right,
                const QString &kind, QVector<CapsuleSectionDiff> &out )
{
    if ( left == right )
        return;
    const bool bothObjects = left.isObject() && right.isObject();
    const bool bothArrays = left.isArray() && right.isArray();
    if ( bothObjects )
    {
        const QJsonObject leftObject = left.toObject();
        const QJsonObject rightObject = right.toObject();
        QSet<QString> keys;
        for ( auto it = leftObject.begin(); it != leftObject.end(); ++it )
            keys.insert( it.key() );
        for ( auto it = rightObject.begin(); it != rightObject.end(); ++it )
            keys.insert( it.key() );
        QStringList sortedKeys = keys.values();
        std::sort( sortedKeys.begin(), sortedKeys.end() );
        for ( const QString &key : sortedKeys )
            diffValue( path + QLatin1Char( '.' ) + key, leftObject.value( key ),
                       rightObject.value( key ), kind, out );
        return;
    }
    if ( bothArrays )
    {
        const QJsonArray leftArray = left.toArray();
        const QJsonArray rightArray = right.toArray();
        const int count = qMax( leftArray.size(), rightArray.size() );
        for ( int i = 0; i < count; ++i )
            diffValue( QStringLiteral( "%1[%2]" ).arg( path ).arg( i ),
                       leftArray.at( i ), rightArray.at( i ), kind, out );
        return;
    }
    CapsuleSectionDiff entry;
    entry.section = path;
    entry.kind = kind;
    entry.left = renderValue( left );
    entry.right = renderValue( right );
    out.append( entry );
}

} // namespace

QJsonObject CapsuleSectionDiff::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "section" ), section );
    json.insert( QStringLiteral( "kind" ), kind );
    json.insert( QStringLiteral( "left" ), left );
    json.insert( QStringLiteral( "right" ), right );
    return json;
}

QString CapsuleDiffReport::levelToString() const
{
    switch ( level )
    {
        case Level::Identical:
            return QStringLiteral( "identical" );
        case Level::EquivalentRerun:
            return QStringLiteral( "equivalent_rerun" );
        case Level::IdentityBreak:
            return QStringLiteral( "identity_break" );
    }
    return QStringLiteral( "identity_break" );
}

QJsonObject CapsuleDiffReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "level" ), levelToString() );
    QJsonArray sectionArray;
    for ( const auto &section : sections )
        sectionArray.append( section.toJson() );
    json.insert( QStringLiteral( "sections" ), sectionArray );
    json.insert( QStringLiteral( "reasons" ),
                 QJsonArray::fromStringList( reasons ) );
    return json;
}

CapsuleDiffReport CapsuleDiffReport::diff( const CapsuleDocument &a, const CapsuleDocument &b )
{
    CapsuleDiffReport report;

    if ( digestsEqual( a, b ) )
        return report; // Identical, no reasons needed

    // Compare each top-level section (schema and digest are contract, not
    // content — the shape gates own them).
    QStringList sections = kIdentitySections + kReportedSections;
    sections.sort();
    for ( const QString &section : sections )
    {
        const QJsonValue left = a.root().value( section );
        const QJsonValue right = b.root().value( section );
        if ( left == right )
            continue;
        const QString kind = kindOf( section );
        report.reasons << QStringLiteral( "%1 differs (%2)" ).arg( section, kind );
        diffValue( section, left, right, kind, report.sections );
    }

    for ( const auto &entry : report.sections )
    {
        if ( entry.kind == QLatin1String( "identity" ) )
        {
            report.level = Level::IdentityBreak;
            return report;
        }
    }
    report.level = report.sections.isEmpty() ? Level::Identical : Level::EquivalentRerun;
    return report;
}

} // namespace sicnu::experiment::capsule
