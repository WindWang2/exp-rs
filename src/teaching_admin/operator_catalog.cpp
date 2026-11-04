#include "operator_catalog.h"
#include "json_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::teaching_admin {

QJsonObject OperatorCatalog::toJson() const
{
    QStringList ids;
    for ( const QString &id : operatorIds )
        ids.append( id );
    ids.sort();
    return sortKeys( QJsonObject{
        { QStringLiteral( "operator_count" ), ids.size() },
        { QStringLiteral( "operators" ), QJsonArray::fromStringList( ids ) },
        { QStringLiteral( "issues" ), issuesToJson( issues ) },
    } );
}

OperatorCatalog loadOperatorCatalog( const QString &capabilityDir )
{
    OperatorCatalog cat;
    QDir dir( capabilityDir );
    if ( !dir.exists() )
    {
        cat.issues.push_back( { QStringLiteral( "operator_registry_missing" ), capabilityDir,
                                QStringLiteral( "capability directory not found; authoring stays fail-closed" ),
                                QStringLiteral( "error" ) } );
        return cat;
    }

    const auto files = dir.entryList( { QStringLiteral( "*.json" ) }, QDir::Files, QDir::Name );
    for ( const QString &name : files )
    {
        // Relations metadata is not an operator sidecar.
        if ( name == QLatin1String( "capability_relations.json" ) )
            continue;
        // Sidecars are generated per operator as rs-<id>.json.
        if ( !name.startsWith( QLatin1String( "rs-" ) ) )
            continue;

        // swarm-review LT1-4: bounded read (R14-E audit F5 discipline, same
        // budget as the curriculum/pack/checkpoint sidecar scans): an
        // oversize or unreadable sidecar is a typed issue, never an
        // unbounded readAll() slurp into memory.
        constexpr qint64 kMaxSidecarBytes = 16 * 1024 * 1024;
        const QString sidecarPath = dir.filePath( name );
        const BoundedReadResult bounded = readBounded( sidecarPath, kMaxSidecarBytes );
        if ( bounded.unreadable )
        {
            cat.issues.push_back( { QStringLiteral( "operator_sidecar_unreadable" ), name,
                                    QStringLiteral( "cannot read capability sidecar" ),
                                    QStringLiteral( "error" ) } );
            continue;
        }
        if ( bounded.oversize )
        {
            cat.issues.push_back( { QStringLiteral( "operator_sidecar_oversize" ), name,
                                    QStringLiteral( "capability sidecar exceeds the 16 MiB read budget" ),
                                    QStringLiteral( "error" ) } );
            continue;
        }
        const QJsonObject doc = QJsonDocument::fromJson( bounded.bytes ).object();
        const QString id = doc.value( QStringLiteral( "id" ) ).toString();
        if ( id.isEmpty() )
        {
            cat.issues.push_back( { QStringLiteral( "operator_sidecar_invalid" ), name,
                                    QStringLiteral( "capability sidecar lacks an operator id" ),
                                    QStringLiteral( "error" ) } );
            continue;
        }

        QJsonObject properties;
        const QJsonArray params =
          doc.value( QStringLiteral( "capability" ) ).toObject()
            .value( QStringLiteral( "io" ) ).toObject()
            .value( QStringLiteral( "parameters" ) ).toArray();
        for ( const auto &pv : params )
        {
            const QJsonObject p = pv.toObject();
            const QString paramName = p.value( QStringLiteral( "name" ) ).toString();
            if ( paramName.isEmpty() )
                continue;
            properties.insert( paramName, p );
        }
        cat.paramSchemas.insert( id, QJsonObject{ { QStringLiteral( "properties" ), properties } } );
        cat.operatorIds.insert( id );
    }
    return cat;
}

} // namespace sicnu::teaching_admin
