// project_session_boundary.h — one project-open transaction
//
// The core of the shell's openProject, extracted so the session boundary is
// testable without the full main window: this module — not the window — owns
// the clear → hook → bind → read sequence and the failure rollback.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class QgsProject;

namespace sicnu::app
{

class ProjectContext;

/// Typed outcome of one project-open transaction.
struct ProjectSessionOpenResult
{
    enum class Stage
    {
        /// Project read; session now presents the opened file.
        Succeeded,
        /// The probe-read refused the file: the live session is untouched.
        ProbeFailed,
        /// clearProject refused: the live session is (best-effort) untouched.
        ClearFailed,
        /// The full read failed after the session was emptied: the
        /// transaction rolled the identity back to the consistent empty
        /// state (no phantom fileName, governance store closed).
        ReadFailed,
    };

    Stage stage = Stage::ProbeFailed;
    /// The path the transaction was unable to open (empty on success).
    QString failedPath;
    /// clearProject diagnostics (ClearFailed) or the project's own read
    /// error (ReadFailed), for host display.
    QStringList diagnostics;
    /// Whether the governance store opened for the target (Succeeded only;
    /// false means memory-only mode, which is a warning note, not a stop).
    bool governanceStoreOpened = false;
};

/// Open @p path into the live session:
///
///   1. probe-read the file into a throwaway project (fail-closed: a corrupt
///      target must not wipe the live session — #1083),
///   2. clear the previous project's data/display/governance state,
///   3. hand the emptied session to @p onSessionEmptied (the story-boundary
///      hook: lab recording stop, mission session reset) — whether the read
///      below succeeds or fails, the previous project's story must not leak,
///   4. bind the governance store to the target and read the project.
///
/// On a failed read the transaction rolls the session identity back to the
/// consistent EMPTY state before returning. QgsProject::read() sets fileName
/// BEFORE parsing and leaves it pointing at the target on failure (the
/// read-side twin of #1097), so the rollback resets the file name and closes
/// the governance store that was just bound to the target. Nothing about the
/// failed target survives as session state: the window title, the save
/// target and the store cannot silently adopt a file that never opened.
///
/// @p readFn is the injectable read edge (mirrors
/// ProjectContext::createForTesting's probe seam): the production path is
/// QgsProject::read, tests inject a failing read to exercise the mid-
/// transaction failure branch that a well-formed file cannot otherwise
/// reach (the probe and the full read share one parser, so a file that
/// passes the probe essentially never fails the real read).
ProjectSessionOpenResult openProjectSession(
    ProjectContext &projectContext, QgsProject &project, const QString &path,
    const std::function<void()> &onSessionEmptied,
    const std::function<bool( QgsProject &, const QString & )> &readFn = {} );

} // namespace sicnu::app
