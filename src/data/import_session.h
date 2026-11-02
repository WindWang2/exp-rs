// src/data/import_session.h — four-state import session over the EXISTING
// import pieces (R27-T2 milestone D).
//
//   check → continue-import → cancel → resume
//
// ImportPreflight (R25-B) answers "what would happen"; this session is the
// stateful driver AROUND it and DataManager::registerSource: one batch of
// sources moves through Checking → ReadyToImport → Importing →
// Completed/Cancelled/Failed, with a durable per-item journal so a cancelled
// or crashed import RESUMES idempotently (already-registered sources are
// skipped via the DataManager's own dedup lookup — no second identity
// system: the session reuses ImportPreflight's identity token verdicts and
// RegisterResult.reusedExisting verbatim).
//
// Contracts:
//   * thread affinity: all calls run on the DataManager's owning thread
//     (registerSource requirement) — same rule as every DataManager caller.
//   * cancel() is cooperative and synchronous-safe: the import loop observes
//     it between items; an in-flight registerSource completes and its item
//     is journaled (never half-recorded).
//   * the journal publishes atomically (platform::sidecar): a crash at any
//     point leaves the previous complete journal; resume() re-verifies each
//     journaled item against the catalog before skipping it.
//   * machine-readable summary: schema "sicnu.import.session/1" (toJson).
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <vector>

namespace sicnu::data
{

class DataManager;

enum class ImportSessionState
{
    Checking,       ///< constructed; preflight not yet run
    ReadyToImport,  ///< checked: at least one admissible source
    Importing,      ///< registration loop running/ran
    Completed,      ///< every admissible source registered
    Cancelled,      ///< user cancel observed; journal keeps completed items
    Failed          ///< a hard registration error; journal keeps completed items
};

enum class ImportSessionItemState
{
    Pending,
    Registered,
    Refused,        ///< preflight refusal (or dedup skip recorded as info)
    Skipped,        ///< journaled complete + verified in catalog on resume
    Error
};

struct ImportSessionItem
{
    QString path;
    ImportSessionItemState state = ImportSessionItemState::Pending;
    QString verdict;     ///< ImportPreflight verdict string ("" before check)
    QString assetId;     ///< populated on Registered
    QString message;     ///< refusal / error text (no secrets, paths only)
    bool reusedExisting = false; ///< DataManager dedup hit (RegisterResult)
};

/// Machine-readable operation summary (schema sicnu.import.session/1).
struct ImportSessionSummary
{
    inline static constexpr int kSchemaVersion = 1;

    ImportSessionState state = ImportSessionState::Checking;
    std::vector<ImportSessionItem> items;
    int registeredCount() const;
    int refusedCount() const;
    int pendingCount() const;

    QJsonObject toJson() const;
};

class ImportSession
{
  public:
    struct Options
    {
        /// Sources preflighting as NeedsUserChoice are refused with a typed
        /// message (the session never guesses a container/subdataset choice).
        bool refuseNeedsUserChoice = true;
        /// Durable journal path (platform::sidecar atomic publish). Empty
        /// disables persistence — resume then only works in-process.
        QString journalPath;
    };

    /// The session adopts @p sources verbatim (order = import order).
    ImportSession( DataManager *manager, QStringList sources, Options options = {} );

    ImportSessionState state() const { return m_state; }
    ImportSessionSummary summary() const;

    /// Phase 1 — 检查: preflights every source (ImportPreflight::inspect,
    /// local identity + dedup prediction; never registers). Moves to
    /// ReadyToImport when at least one source is admissible, else Completed
    /// with every item Refused.
    void check();

    /// Phase 2 — 继续导入: registers every admissible source in order via
    /// DataManager::registerSource, journaling after each item. Ends
    /// Completed, Cancelled (cancel observed between items) or Failed.
    void proceed();

    /// Requests cancellation of an in-flight proceed()/resume() (or marks a
    /// not-yet-started session Cancelled).
    void cancel();

    /// Phase 4 — 恢复: after cancel/failure/restart, continues exactly the
    /// Pending items; journaled-Registered items are re-verified through
    /// the catalog (findByPath) and skipped, never double-registered.
    void resume();

    /// Publishes the journal (atomic sidecar). Called by the session after
    /// every item; public so a hosting service can flush on shutdown.
    void saveJournal() const;

  private:
    void runPendingItems();
    void loadJournal();

    DataManager *m_manager = nullptr;
    Options m_options;
    std::vector<ImportSessionItem> m_items;
    ImportSessionState m_state = ImportSessionState::Checking;
    std::atomic<bool> m_cancelRequested{ false };
    bool m_journalLoaded = false;
};

} // namespace sicnu::data
