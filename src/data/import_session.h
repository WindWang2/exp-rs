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
    /// Content identity observed at check() time ("li1:v1:<hex>"; "" when
    /// the check ran with identity probing off). Journaled so resume()'s
    /// fact-check can detect a replaced file (same path, different bytes).
    QString identityToken;
};

/// Machine-readable operation summary (schema sicnu.import.session/1).
struct ImportSessionSummary
{
    inline static constexpr int kSchemaVersion = 1;

    ImportSessionState state = ImportSessionState::Checking;
    std::vector<ImportSessionItem> items;
    /// Non-empty when the last journal publish FAILED (R1-4): the summary
    /// says so instead of pretending a durable resume point exists.
    QString journalError;
    bool journalDurable = true;
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
        /// A caller supplying its own retry paths can preflight them afresh
        /// while continuing to publish to the same journal.
        bool loadExistingJournal = true;
    };

    /// The session adopts @p sources verbatim (order = import order).
    ImportSession( DataManager *manager, QStringList sources, const Options &options );
    /// Convenience overload with default options (no durable journal).
    ImportSession( DataManager *manager, QStringList sources );

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
    /// not-yet-started session Cancelled). The m_cancelRequested flag is
    /// atomic so the owning thread observes it promptly, but cancel() ITSELF
    /// must be called on the DataManager's owning thread like every other
    /// session call — it also transitions m_state.
    void cancel();

    /// Phase 4 — 恢复: after cancel/failure/restart, continues exactly the
    /// Pending items; journaled-Registered items are re-verified through
    /// the catalog (findByPath) and skipped, never double-registered.
    void resume();

    // ── Stepwise cooperative driving (R28-T02 WP03) ─────────────────────
    //
    // A UI caller owns the DataManager's thread, so a cancel arriving as a
    // QUEUED invocation can never run while monolithic check()/proceed()
    // holds the thread. These bounded variants let the caller drive one
    // chunk per event-loop spin (QTimer(0) chaining, no processEvents
    // re-entry): a queued cancel lands at the next step boundary and the
    // batch stops within one item.

    /// Stepwise check(): preflights at most @p maxItems still-unexamined
    /// Pending items and returns how many were inspected. When every item
    /// has a verdict the state moves exactly like check(). A cancel
    /// request observed at a boundary finalizes Cancelled (returns 0).
    int checkSteps( std::size_t maxItems );

    /// Stepwise proceed(): settles at most @p maxItems Pending items
    /// (register / dedup-skip / refuse-record) and returns how many were
    /// settled. While pending work remains the session stays Importing;
    /// the Completed/Failed transition happens only on the call that
    /// settles the last item (identical semantics to proceed(), bounded).
    /// A cancel request observed at a boundary finalizes Cancelled
    /// (returns 0).
    int proceedSteps( std::size_t maxItems );

    /// True while the session still has Pending items (work the stepwise
    /// driver or the monolithic calls would still settle).
    bool hasPendingItems() const;

    /// Thread-safe cancel request (any thread): arms the cooperative flag
    /// the import loop observes at the next item boundary. Unlike cancel()
    /// it does not touch session state — the owning thread finalizes the
    /// Cancelled transition at the boundary (a foreign thread must never
    /// mutate the session's non-atomic state).
    void requestCancel() { m_cancelRequested.store( true ); }

    /// Read-only fact re-verification over journaled-settled (Skipped)
    /// items: the file's existence and — when the journal carries the
    /// check-time identity token — its current content identity are
    /// re-probed (bounded hash) and recorded in item.message. Never
    /// changes item states or session state: the catalog stays the
    /// registration authority; this makes "registered file was replaced or
    /// moved" VISIBLE instead of silently skipped. resume() runs the same
    /// pass automatically.
    void verifyFacts();

    /// Publishes the journal (atomic sidecar). Called by the session after
    /// every item; public so a hosting service can flush on shutdown. A
    /// failed publish surfaces through summary() as journal_write_error
    /// (resume-after-crash is then not durable — the catalog remains the
    /// idempotent-resume truth).
    void saveJournal();

  private:
    int runPendingItemsBounded( std::size_t maxItems );
    void loadJournal();
    void admitPendingItems();

    DataManager *m_manager = nullptr;
    Options m_options;
    std::vector<ImportSessionItem> m_items;
    ImportSessionState m_state = ImportSessionState::Checking;
    std::atomic<bool> m_cancelRequested{ false };
    bool m_journalLoaded = false;
    QString m_journalError;      ///< non-empty after a failed journal publish
    bool m_journalDurable = true;
};

} // namespace sicnu::data
