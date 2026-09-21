/***************************************************************************
 * mission_tool_host_install.cpp — the shared mission tool wiring
 ***************************************************************************/

#include "app/workbench/mission_tool_host_install.h"

#include "agent/spatial_tools/mission_tools.h"
#include "app/workbench/mission_run_resolver.h"
#include "app/workbench/mission_tool_authority.h"

#include <qgsmaplayer.h>
#include <qgsproject.h>

namespace sicnu::app
{

void installMissionToolHost()
{
    auto &host = sicnu::agent::spatial_tools::MissionToolHost::instance();

    host.setAuthority( std::make_shared<MissionStoreAuthority>() );

    // The project file is the runtime's key: QgsProject is empty in a fresh
    // headless session, and the tools then refuse (fail closed) instead of
    // inventing a mission.
    host.setProjectPathProvider( []() -> QString {
        QgsProject *project = QgsProject::instance();
        return project ? project->fileName() : QString();
    } );

    // The XML channel is written by QgsProject itself on project save; the
    // sidecar stays the authority between saves (D18 precedence).
    host.setProjectDocumentProvider( []() -> QDomDocument { return QDomDocument(); } );

    host.setRunStatusResolver( []( const sicnu::app::MissionRunRef &ref ) {
        return sicnu::app::resolveMissionRunStatus( ref );
    } );

    // Layer liveness: ids only — no QgsMapLayer* is ever cached across an
    // event-loop turn; the resolver is rebuilt per call.
    host.setRefResolverProvider( []() -> sicnu::app::MissionRefResolver {
        return []( const QString &refId ) -> sicnu::app::MissionRefStatus {
            sicnu::app::MissionRefStatus status;
            QgsProject *project = QgsProject::instance();
            if ( !project )
            {
                status.alive = false;
                status.reason = QStringLiteral( "no_project" );
                return status;
            }
            if ( !project->mapLayer( refId ) )
            {
                status.alive = false;
                status.reason = QStringLiteral( "deleted_layer" );
            }
            return status;
        };
    } );
}

} // namespace sicnu::app
