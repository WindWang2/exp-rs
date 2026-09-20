/***************************************************************************
 * mission_tool_authority.cpp — store-backed MissionAuthority
 ***************************************************************************/

#include "app/workbench/mission_tool_authority.h"

#include "app/workbench/mission_runtime_store.h"

namespace sicnu::app
{

bool MissionStoreAuthority::load( const QString &projectPath, const QDomDocument &document,
                                  sicnu::app::MissionRuntimeState &state, QString &errorCode,
                                  QString &errorMessage )
{
    QString err;
    if ( !loadMissionRuntime( projectPath, document, state, &err ) )
    {
        errorCode = state.authorityCorrupt ? QStringLiteral( "authority_corrupt" )
                                          : QStringLiteral( "authority_unreadable" );
        errorMessage = err;
        return false;
    }
    return true;
}

bool MissionStoreAuthority::commit( const QString &projectPath, QDomDocument &document,
                                    sicnu::app::MissionRuntimeState &state, QString &errorCode,
                                    QString &errorMessage )
{
    QString err;
    if ( !saveMissionRuntime( projectPath, document, state, &err ) )
    {
        errorCode = QStringLiteral( "commit_failed" );
        errorMessage = err;
        return false;
    }
    return true;
}

} // namespace sicnu::app
