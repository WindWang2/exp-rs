/***************************************************************************
 * mission_timeline_bridge.cpp — embed/extract the timeline in the context
 ***************************************************************************/

#include "app/workbench/mission_timeline_bridge.h"

namespace sicnu::app
{

void embedMissionTimeline( MissionContext &ctx, const MissionTimeline &timeline )
{
    MissionTimeline toEmbed = timeline;
    // The context is the authority for identity: when it knows the mission
    // id, the embedded document follows it. An id-less context keeps the
    // timeline's own id (ensureMissionId mints one at save time).
    if ( !ctx.missionId.isEmpty() )
        toEmbed.setMissionId( ctx.missionId );

    ctx.metadata.insert( QLatin1String( kMissionTimelineMetadataKey ), toEmbed.toJson() );
}

bool missionContextHasTimeline( const MissionContext &ctx )
{
    return ctx.metadata.contains( QLatin1String( kMissionTimelineMetadataKey ) );
}

bool extractMissionTimeline( const MissionContext &ctx, MissionTimeline &out, QString *error )
{
    const auto fail = [&error]( const QString &why ) -> bool {
        if ( error )
            *error = why;
        return false;
    };

    const QJsonObject::const_iterator it =
        ctx.metadata.constFind( QLatin1String( kMissionTimelineMetadataKey ) );
    if ( it == ctx.metadata.constEnd() )
        return fail( QStringLiteral( "missing" ) );

    if ( !it.value().isObject() )
        return fail( QStringLiteral( "embedded_timeline_not_object" ) );

    MissionTimeline decoded;
    QString decodeError;
    if ( !decoded.fromJson( it.value().toObject(), &decodeError ) )
        return fail( decodeError.isEmpty() ? QStringLiteral( "decode_failed" ) : decodeError );

    out = std::move( decoded );
    return true;
}

QString missionTimelineMigrationSource( const MissionContext &ctx )
{
    return ctx.metadata.value( QLatin1String( kMissionTimelineMigrationKey ) ).toString();
}

void setMissionTimelineMigrationSource( MissionContext &ctx, const QString &source )
{
    if ( source.isEmpty() )
        ctx.metadata.remove( QLatin1String( kMissionTimelineMigrationKey ) );
    else
        ctx.metadata.insert( QLatin1String( kMissionTimelineMigrationKey ), source );
}

} // namespace sicnu::app
