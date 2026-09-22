#include "data_pack_manager.h"
#include "json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace sicnu::teaching_admin {

QJsonObject PackInventoryEntry::toJson() const
{
    QJsonArray issueArr = issuesToJson( issues );
    return QJsonObject{
        { QStringLiteral( "pack_id" ), packId },
        { QStringLiteral( "path" ), path },
        { QStringLiteral( "lab_id" ), labId },
        { QStringLiteral( "pack_version" ), packVersion },
        { QStringLiteral( "declared_bytes" ), static_cast<double>( declaredBytes ) },
        { QStringLiteral( "computed_bytes" ), static_cast<double>( computedBytes ) },
        { QStringLiteral( "digest" ), digest },
        { QStringLiteral( "offline_available" ), offlineAvailable },
        { QStringLiteral( "generator" ), generator },
        { QStringLiteral( "crs_grid_summary" ), crsGridSummary },
        { QStringLiteral( "issues" ), issueArr },
    };
}

QJsonObject PackInventory::toJson() const
{
    QJsonArray arr;
    for ( const auto &p : packs )
        arr.append( p.toJson() );
    return QJsonObject{
        { QStringLiteral( "packs" ), arr },
        { QStringLiteral( "total_declared_bytes" ), static_cast<double>( totalDeclaredBytes ) },
        { QStringLiteral( "byte_budget" ), static_cast<double>( byteBudget ) },
        { QStringLiteral( "within_budget" ), withinBudget },
    };
}

ValidationResult validatePackDocument( const QJsonObject &pack, const QString &repoRoot )
{
    Q_UNUSED( repoRoot );
    ValidationResult r;
    const QString schema = pack.value( QStringLiteral( "schema_version" ) ).toString(
        pack.value( QStringLiteral( "schema" ) ).toString() );
    if ( schema != QLatin1String( "sicnu.lab-pack/1" ) )
        r.addError( QStringLiteral( "pack_schema" ), QStringLiteral( "schema_version" ),
                    QStringLiteral( "must be sicnu.lab-pack/1" ) );

    const QJsonArray inputs = pack.value( QStringLiteral( "inputs" ) ).toArray();
    QSet<QString> seen;
    for ( int i = 0; i < inputs.size(); ++i )
    {
        const QJsonObject in = inputs.at( i ).toObject();
        const QString path = in.value( QStringLiteral( "path" ) ).toString();
        const QString at = QStringLiteral( "inputs[%1].path" ).arg( i );
        if ( isUnsafeRelativePath( path ) )
            r.addError( QStringLiteral( "path_traversal" ), at,
                        QStringLiteral( "pack input path escapes root or is absolute" ) );
        if ( seen.contains( path ) )
            r.addError( QStringLiteral( "duplicate_pack_path" ), at,
                        QStringLiteral( "duplicate input path" ) );
        seen.insert( path );
    }
    return r;
}

PackInventory inventoryPacks( const QString &packsDir, const QString &repoRoot, qint64 byteBudgetBytes )
{
    PackInventory inv;
    inv.byteBudget = byteBudgetBytes;
    QDir dir( packsDir );
    if ( !dir.exists() )
        return inv;

    const auto files = dir.entryList( { QStringLiteral( "*.pack.json" ) }, QDir::Files, QDir::Name );
    for ( const QString &name : files )
    {
        PackInventoryEntry e;
        e.path = dir.filePath( name );
        e.packId = name;
        if ( e.packId.endsWith( QLatin1String( ".pack.json" ) ) )
            e.packId.chop( 10 );

        QFile f( e.path );
        if ( !f.open( QIODevice::ReadOnly ) )
        {
            e.issues.push_back( { QStringLiteral( "pack_unreadable" ), e.path,
                                  QStringLiteral( "cannot read pack" ), QStringLiteral( "error" ) } );
            inv.packs.push_back( e );
            continue;
        }
        const QByteArray raw = f.readAll();
        e.digest = sha256Hex( raw );
        const QJsonObject pack = QJsonDocument::fromJson( raw ).object();
        e.labId = pack.value( QStringLiteral( "lab_id" ) ).toString();
        e.packVersion = pack.value( QStringLiteral( "pack_version" ) ).toString();
        e.generator = pack.value( QStringLiteral( "generator" ) ).toString();
        e.declaredBytes = static_cast<qint64>( pack.value( QStringLiteral( "declared_offline_bytes" ) ).toDouble( -1 ) );

        const ValidationResult vr = validatePackDocument( pack, repoRoot );
        e.issues = vr.issues;

        qint64 sum = 0;
        bool allPresent = true;
        QStringList truths;
        for ( const auto &iv : pack.value( QStringLiteral( "inputs" ) ).toArray() )
        {
            const QJsonObject in = iv.toObject();
            const QString rel = in.value( QStringLiteral( "path" ) ).toString();
            if ( isUnsafeRelativePath( rel ) )
            {
                allPresent = false;
                continue;
            }
            const QString abs = QDir( repoRoot ).filePath( rel );
            QFileInfo fi( abs );
            if ( fi.exists() && fi.isFile() )
                sum += fi.size();
            else
                allPresent = false;
            const QString truth = in.value( QStringLiteral( "sensor_truth" ) ).toString();
            if ( !truth.isEmpty() )
                truths.append( truth.left( 80 ) );
            const qint64 declared = static_cast<qint64>( in.value( QStringLiteral( "bytes" ) ).toDouble( -1 ) );
            if ( declared > 0 && e.declaredBytes < 0 )
                e.declaredBytes = ( e.declaredBytes < 0 ? 0 : e.declaredBytes ) + declared;
        }
        e.computedBytes = sum;
        if ( e.declaredBytes < 0 )
            e.declaredBytes = sum;
        e.offlineAvailable = allPresent && vr.ok;
        e.crsGridSummary = truths.join( QStringLiteral( " | " ) );

        inv.totalDeclaredBytes += std::max<qint64>( 0, e.declaredBytes );
        inv.packs.push_back( e );
    }
    inv.withinBudget = inv.totalDeclaredBytes <= inv.byteBudget;
    return inv;
}

} // namespace sicnu::teaching_admin
