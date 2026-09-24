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

        QFile f( dir.filePath( name ) );
        if ( !f.open( QIODevice::ReadOnly ) )
        {
            cat.issues.push_back( { QStringLiteral( "operator_sidecar_unreadable" ), name,
                                    QStringLiteral( "cannot read capability sidecar" ),
                                    QStringLiteral( "error" ) } );
            continue;
        }
        const QJsonObject doc = QJsonDocument::fromJson( f.readAll() ).object();
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
