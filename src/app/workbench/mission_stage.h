/***************************************************************************
 * mission_stage.h — Mission task space: stage machine, timeline, reconcile
 *
 * Track: glm53-mission-workbench-12 (Unified Mission Workbench 12.0)
 *
 * Scope (see .planning/glm53-mission-workbench-12/DEDUP.md):
 *   D18 (#991) already owns the MissionContext *value* model, its QJson
 *   serialization (kind "mission_context", schema_version "1.0"), the
 *   sidecar+XML dual persistence and the content fingerprint. This module
 *   deliberately does NOT re-create any of that. It adds the missing
 *   "task space" layer on top:
 *
 *     - an ordered mission stage axis (import -> preprocess -> analyze ->
 *       verify -> publish) with an explicit, fail-closed transition table;
 *     - a resumable task timeline (append-only event log + revision);
 *     - reference reconciliation (deleted / renamed layers & artifacts);
 *     - deterministic JSON serialization so GUI, MCP and Pi render one
 *       single projection.
 *
 * Hard rules inherited from mission_context.h:
 *   - value semantics only: no QObject, no QgsMapLayer*, no QPointer, no
 *     raw pointers to live project state;
 *   - every mutation is fail-closed: an illegal transition never mutates
 *     and never bumps the revision;
 *   - no second scheduler: task execution is referenced by MissionRunRef
 *     (TaskCenter / workflow run / pipeline run) and owned elsewhere.
 *
 * Dependency policy: Qt Core only (plus jsoncpp via mission_context.h).
 * Nothing here may include qgs*.h or Qt Widgets — that is what keeps the
 * Track's regression and benchmark targets cheap to build.
 ***************************************************************************/
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

namespace sicnu::app
{

inline constexpr const char *kMissionTimelineKind = "mission_timeline";
inline constexpr const char *kMissionTimelineSchemaVersion = "1.0";

// ---------------------------------------------------------------------------
// Stage axis
// ---------------------------------------------------------------------------

/// Ordered mission stages. The enum value is the canonical order; stage
/// advancement is derived from task states, never stored redundantly.
enum class MissionStage
{
    Import = 0,
    Preprocess = 1,
    Analyze = 2,
    Verify = 3,
    Publish = 4
};

/// Stable wire key ("import" …). Never null; unknown input falls back to
/// std::nullopt so callers fail closed.
const char *missionStageKey( MissionStage stage );
std::optional<MissionStage> missionStageFromKey( const QString &key );
QString missionStageLabel( MissionStage stage );
/// All stages in canonical order — the iteration order for UI and projection.
QVector<MissionStage> missionStages();

// ---------------------------------------------------------------------------
// Task status
// ---------------------------------------------------------------------------

enum class MissionTaskStatus
{
    Pending,   ///< accepted, not started
    Running,   ///< handed to TaskCenter / workflow / pipeline run
    Succeeded, ///< completed and verified
    Failed,    ///< completed with an error; retryable
    Canceled,  ///< stopped by the user; resumable
    Stale      ///< its inputs/outputs no longer resolve (deleted/renamed refs)
};

const char *missionTaskStatusKey( MissionTaskStatus status );
std::optional<MissionTaskStatus> missionTaskStatusFromKey( const QString &key );
QString missionTaskStatusLabel( MissionTaskStatus status );

/// True when the task is not in flight (UI may offer retry/resume).
bool missionTaskStatusIsSettled( MissionTaskStatus status );
/// True when the task can be handed to a runner again.
bool missionTaskStatusIsRetryable( MissionTaskStatus status );

// ---------------------------------------------------------------------------
// Task / event records
// ---------------------------------------------------------------------------

/// Reference to the *existing* execution authority. This module never runs
/// anything itself: it records who owns the run so GUI / MCP / Pi all report
/// the same identifier.
struct MissionRunRef
{
    QString kind; ///< "task_center" | "workflow_run" | "pipeline_run" | ""
    QString id;

    bool isNull() const { return id.isEmpty(); }
    bool operator==( const MissionRunRef & ) const = default;
};

struct MissionTask
{
    QString id;            ///< stable, unique inside the timeline
    MissionStage stage = MissionStage::Import;
    QString title;
    QString capabilityId;  ///< agent tool id or command id, e.g. "rs:spectral_index"
    QStringList inputRefIds;
    QStringList outputRefIds;
    MissionTaskStatus status = MissionTaskStatus::Pending;
    int attempts = 0;      ///< incremented on every (re)start
    QString startedIso;
    QString endedIso;
    QString errorCode;
    QString errorMessage;
    MissionRunRef run;
    QString retryOf;       ///< lineage: id of the task this one retries

    bool operator==( const MissionTask & ) const = default;
};

/// Append-only audit record. `seq` is strictly increasing from 1 and is the
/// cursor used for incremental UI updates (`eventsSince`).
struct MissionEvent
{
    quint64 seq = 0;
    QString taskId;
    MissionTaskStatus from = MissionTaskStatus::Pending;
    MissionTaskStatus to = MissionTaskStatus::Pending;
    QString iso;
    QString note;

    bool operator==( const MissionEvent & ) const = default;
};

/// Result of a mutating call. `applied == false` means *nothing* changed and
/// `reason` is a stable, machine-comparable code (never a sentence).
struct MissionOutcome
{
    bool applied = false;
    QString reason;

    static MissionOutcome ok( QString reason = {} )
    {
        MissionOutcome o;
        o.applied = true;
        o.reason = std::move( reason );
        return o;
    }
    static MissionOutcome rejected( QString reason )
    {
        MissionOutcome o;
        o.applied = false;
        o.reason = std::move( reason );
        return o;
    }
};

inline constexpr const char *kMissionOk = "ok";
inline constexpr const char *kMissionErrUnknownTask = "unknown_task";
inline constexpr const char *kMissionErrDuplicateTask = "duplicate_task_id";
inline constexpr const char *kMissionErrEmptyTaskId = "empty_task_id";
inline constexpr const char *kMissionErrIllegalTransition = "illegal_transition";
inline constexpr const char *kMissionErrNotRetryable = "status_not_retryable";
inline constexpr const char *kMissionErrStaleRun = "stale_run_reference";

// ---------------------------------------------------------------------------
// Transition table (pure, fail-closed)
// ---------------------------------------------------------------------------

/// Whether @p to may follow @p from. Same-status is a legal no-op so that
/// idempotent replays (progress events, double cancel) never wedge the UI.
bool missionTransitionAllowed( MissionTaskStatus from, MissionTaskStatus to );

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

class MissionTimeline
{
public:
    MissionTimeline() = default;

    const QString &missionId() const { return mMissionId; }
    void setMissionId( const QString &id ) { mMissionId = id; }
    const QString &projectRef() const { return mProjectRef; }
    void setProjectRef( const QString &ref ) { mProjectRef = ref; }

    // --- tasks ---
    MissionOutcome addTask( const MissionTask &task );
    bool hasTask( const QString &taskId ) const;
    const MissionTask *task( const QString &taskId ) const;
    QVector<MissionTask> tasks() const { return mTasks; }
    QVector<MissionTask> tasksForStage( MissionStage stage ) const;

    /// Fail-closed status transition. Rejects unknown tasks, illegal pairs and
    /// transitions that would resurrect a stale run reference. @p errorCode /
    /// @p errorMessage are recorded on the task when @p to is Failed (the
    /// latest failure wins; empty values clear a previous one).
    MissionOutcome transition( const QString &taskId,
                               MissionTaskStatus to,
                               const QString &iso,
                               const QString &note = {},
                               const QString &errorCode = {},
                               const QString &errorMessage = {} );

    /// Failed/Canceled/Stale -> Pending, keeping the retry lineage. Does NOT
    /// bump `attempts` by itself: an attempt is counted when the task is handed
    /// to a runner (the Pending -> Running transition), so a requeue that never
    /// starts cannot inflate the attempt count. Optionally rebinds the run
    /// reference (new TaskCenter id).
    MissionOutcome retry( const QString &taskId,
                          const QString &iso,
                          const MissionRunRef &run = {},
                          const QString &note = {} );

    /// Bind or rebind the run authority (TaskCenter / workflow / pipeline id)
    /// without changing status. Required before Pending -> Running, which is
    /// what makes "which execution is in flight" answerable identically on the
    /// GUI, MCP and Pi surfaces. Audit-only: the run id lives in the task
    /// record and is covered by the transition event that follows.
    MissionOutcome bindRunReference( const QString &taskId,
                                     const MissionRunRef &run,
                                     const QString &iso = {},
                                     const QString &note = {} );

    /// Earliest stage that still has unsettled work; otherwise the last stage
    /// that produced a result. Empty timelines report Import.
    MissionStage currentStage() const;

    /// Rewrite a renamed reference in place (layers, artifacts, runs). A rename
    /// is *not* a dangling reference: task status, attempts and run binding are
    /// preserved and an audit event is appended. Returns the number of
    /// occurrences rewritten.
    int rewriteReference( const QString &fromId,
                          const QString &toId,
                          const QString &iso,
                          const QString &note = {} );

    // --- event log ---
    QVector<MissionEvent> events() const { return mEvents; }
    QVector<MissionEvent> eventsSince( quint64 seq ) const;
    quint64 lastEventSeq() const { return mSeq; }

    /// Bumped exactly once per applied mutation; the incremental UI cursor.
    quint64 revision() const { return mRevision; }

    // --- serialization ---
    QJsonObject toJson() const;
    /// Fail-closed on wrong kind / version / malformed payload.
    bool fromJson( const QJsonObject &obj, QString *error = nullptr );

    bool operator==( const MissionTimeline & ) const = default;

private:
    MissionTask *mutableTask( const QString &taskId );

    QString mMissionId;
    QString mProjectRef;
    QVector<MissionTask> mTasks;
    QVector<MissionEvent> mEvents;
    quint64 mSeq = 0;
    quint64 mRevision = 0;
};

// ---------------------------------------------------------------------------
// Reconciliation (deleted / renamed references)
// ---------------------------------------------------------------------------

struct MissionRefStatus
{
    bool alive = true;
    QString reason; ///< machine code, e.g. "deleted_layer"
};

/// Resolves whether a referenced id still exists in the owning authority.
/// Supplied by the caller (GUI project, headless store) — this module never
/// caches live pointers.
using MissionRefResolver = std::function<MissionRefStatus( const QString &refId )>;

struct MissionRefHealth
{
    QString refId;
    bool alive = true;
    QString reason;
};

struct MissionReconciliation
{
    QVector<MissionRefHealth> refs;
    QStringList danglingRefIds;
    /// Tasks whose inputs or outputs lost a reference — candidates for Stale.
    QStringList staleTaskIds;

    bool hasIssues() const { return !danglingRefIds.isEmpty() || !staleTaskIds.isEmpty(); }
    QJsonObject toJson() const;
};

/// Pure scan: every input/output ref of every task is resolved once (ids are
/// de-duplicated so a 300-layer mission costs 300 resolver calls, not 3000).
MissionReconciliation reconcileMission( const MissionTimeline &timeline,
                                        const MissionRefResolver &resolver );

/// Applies the scan: tasks listed in @p rec become Stale (a legal transition).
/// Returns the number of tasks actually moved.
int applyReconciliation( MissionTimeline &timeline,
                         const MissionReconciliation &rec,
                         const QString &iso );

struct MissionRename
{
    QString fromId;
    QString toId;
};

/// Rewrites renamed refs in place. A rename is NOT a dangling ref: tasks keep
/// their status and gain an audit note. Returns the number of rewritten refs.
int applyRenames( MissionTimeline &timeline,
                  const QVector<MissionRename> &renames,
                  const QString &iso );

} // namespace sicnu::app
