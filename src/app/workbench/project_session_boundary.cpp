// project_session_boundary.cpp — implementation of the project-open transaction
#include "project_session_boundary.h"

#include "project_context.h"

#include <qgsproject.h>

namespace sicnu::app
{

ProjectSessionOpenResult openProjectSession(
    ProjectContext &projectContext, QgsProject &project, const QString &path,
    const std::function<void()> &onSessionEmptied,
    const std::function<bool( QgsProject &, const QString & )> &readFn )
{
    ProjectSessionOpenResult result;

    // 1. Probe-read into a throwaway project before destroying the live
    // session. A corrupt/unreadable .qgs must not wipe layers, re-bind the
    // governance store, or drop the previous project's story (#1083).
    {
        QgsProject probe;
        if ( !probe.read( path, Qgis::ProjectReadFlag::DontResolveLayers ) )
        {
            result.stage = ProjectSessionOpenResult::Stage::ProbeFailed;
            result.failedPath = path;
            result.diagnostics = QStringList{ probe.error() };
            return result;
        }
    }

    // 2. Clear the previous project's data context (display layers, assets,
    // governance index, QGIS project state).
    const auto cleared = projectContext.clearProject( project );
    if ( !cleared )
    {
        result.stage = ProjectSessionOpenResult::Stage::ClearFailed;
        result.failedPath = path;
        for ( const auto &diagnostic : cleared.diagnostics() )
            result.diagnostics.append(
                QStringLiteral( "[%1] %2" ).arg( diagnostic.code, diagnostic.message ) );
        return result;
    }

    // 3. Story boundary: the session is empty now. The host stops lab
    // recording and resets the mission session here — BEFORE the read — so
    // the previous project's story cannot leak into whatever the read
    // brings (or fails to bring) in.
    if ( onSessionEmptied )
        onSessionEmptied();

    // 4a. Governance 3.0: the store must be open BEFORE the read so the
    // serializer can restore governed state from a v3 document (or run the
    // in-memory v1 migration into it). Failing to open is a warning note,
    // not a stop: governed state then persists through the project DOM only.
    result.governanceStoreOpened = projectContext.openWorkspaceStore( path );

    // 4b. The read itself. QgsProject::read() assigns the file name BEFORE
    // parsing and leaves it on failure — without the rollback below, an
    // empty session would present (and a later saveProject() overwrite!)
    // the file that never opened.
    const bool readOk = readFn ? readFn( project, path ) : project.read( path );
    if ( !readOk )
    {
        result.stage = ProjectSessionOpenResult::Stage::ReadFailed;
        result.failedPath = path;
        result.diagnostics = QStringList{ project.error() };
        // Roll the session identity back to the consistent empty state:
        // the cleared session owns no file and no governance store.
        project.setFileName( QString() );
        projectContext.closeWorkspaceStore();
        return result;
    }

    result.stage = ProjectSessionOpenResult::Stage::Succeeded;
    return result;
}

} // namespace sicnu::app
