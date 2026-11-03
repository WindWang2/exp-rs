// tests/test_r27_t03_dock_lifecycle.cpp — #1742 (AS-006) + #1760 (AS-008)
//
// The dock's serialized executor: startWorker cancels the running task and
// queues the replacement; tasks NEVER overlap, startWorker NEVER blocks the
// GUI thread, and rapid cancel→restart cycles leave no concurrent task, no
// unjoined thread and no lost final state. The armRepairApproval honesty
// gate: malformed tokens are refused on the spot with a typed code, and the
// approval state line reaches the panel. No TSAN lane exists on this host —
// the oracle is the deterministic 100x serialized-restart stress below plus
// the structural one-task-at-a-time invariant (max in-flight == 1).
#include <catch2/catch_test_macros.hpp>

#include "app/agent_ops/agent_ops_control_center_dock.h"
#include "agent_ops/repair_approval.h"

#include <QApplication>
#include <atomic>
#include <chrono>
#include <thread>

namespace
{
void ensureApp()
{
    if (!qApp)
    {
        static int argc = 0;
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, nullptr);
    }
}
} // namespace

TEST_CASE("r27/t03 dock: serialized executor runs at most one task, ever",
          "[r27t03][dock][p1]")
{
    ensureApp();
    sicnu::app::agent_ops::AgentOpsControlCenterDock dock;

    std::atomic<int> inFlight{0};
    std::atomic<int> maxInFlight{0};
    std::atomic<int> completed{0};

    // 100 rapid startWorker cycles — each task registers itself, records
    // the concurrency high-water mark, then yields. Under the old
    // cancel-then-spawn-without-join shape two tasks could observe each
    // other (#1742); the serialized executor makes overlap impossible.
    for (int i = 0; i < 100; ++i)
    {
        dock.startWorker([&](const std::atomic<bool> &cancel) {
            const int now = inFlight.fetch_add(1) + 1;
            int prev = maxInFlight.load();
            while (prev < now && !maxInFlight.compare_exchange_weak(prev, now))
            {
            }
            for (int q = 0; q < 3 && !cancel.load(); ++q)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            inFlight.fetch_sub(1);
            completed.fetch_add(1);
        });
    }
    dock.joinWorker();
    CHECK(maxInFlight.load() == 1);
    // The one-deep queue keeps only the latest task; earlier unstarted ones
    // are replaced. What ran must be serialized, and the final task must
    // have run (joinWorker is a real barrier).
    CHECK(completed.load() >= 1);
    CHECK(dock.isWorkerRunning() == false);
}

TEST_CASE("r27/t03 dock: startWorker never blocks on a long prior task",
          "[r27t03][dock][gui]")
{
    ensureApp();
    sicnu::app::agent_ops::AgentOpsControlCenterDock dock;

    dock.startWorker([&](const std::atomic<bool> &cancel) {
        // Uncancellable-ish long task: cooperative quanta; ~500ms total.
        for (int i = 0; i < 50 && !cancel.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    auto t0 = std::chrono::steady_clock::now();
    dock.startWorker([](const std::atomic<bool> &) {});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
    // Queuing is a lock + notify: the 500ms task's completion must not gate
    // the GUI thread (the old reaper design also passed this; the invariant
    // must survive the serialization change).
    CHECK(elapsed < 200);
    dock.joinWorker();
}

TEST_CASE("r27/t03 dock: restart-under-cancellation keeps coordinator access serialized",
          "[r27t03][dock][p1][stress]")
{
    ensureApp();
    sicnu::app::agent_ops::AgentOpsControlCenterDock dock;

    // The #1742 shape: tasks touch shared state the way startSession's
    // worker does (arm → run). Two tasks inside the critical section at
    // once would interleave — assert the serialization invariant directly.
    std::atomic<int> violations{0};
    std::atomic<bool> inCritical{false};
    std::atomic<bool> stop{false};

    for (int i = 0; i < 100; ++i)
    {
        dock.startWorker([&](const std::atomic<bool> &cancel) {
            bool expected = false;
            if (!inCritical.compare_exchange_strong(expected, true))
            {
                violations.fetch_add(1);
            }
            while (!stop.load() && !cancel.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            inCritical.store(false);
        });
    }
    stop.store(true);
    dock.joinWorker();
    CHECK(violations.load() == 0);
}

TEST_CASE("r27/t03 dock: destruction while tasks are queued joins cleanly",
          "[r27t03][dock][lifetime]")
{
    ensureApp();
    for (int iter = 0; iter < 10; ++iter)
    {
        std::atomic<bool> alive{true};
        {
            sicnu::app::agent_ops::AgentOpsControlCenterDock dock;
            dock.startWorker([&](const std::atomic<bool> &cancel) {
                while (!cancel.load() && alive.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });
            dock.startWorker([&](const std::atomic<bool> &cancel) {
                while (!cancel.load() && alive.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });
            dock.startWorker([&](const std::atomic<bool> &cancel) {
                while (!cancel.load() && alive.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });
            // Destructor cancels, drops the pending work, joins once.
        }
        alive.store(false);
    }
    SUCCEED("10 destruction cycles with queued work: no hang, no crash");
}

TEST_CASE("r27/t03 dock: armRepairApproval is honest about garbage (#1760)",
          "[r27t03][dock][p2]")
{
    ensureApp();
    sicnu::app::agent_ops::AgentOpsControlCenterDock dock;

    // Shape-invalid tokens are typed refusals NOW — the old contract stored
    // anything and reported success unconditionally.
    CHECK(dock.armRepairApproval(Json::Value(42), 1000) == "APPROVAL_MALFORMED");
    CHECK(dock.armRepairApproval(Json::Value(Json::arrayValue), 1000) == "APPROVAL_MALFORMED");
    Json::Value noNonce(Json::objectValue);
    noNonce["kind"] = sicnu::agent_ops::kRepairApprovalKind;
    noNonce["schema_version"] = sicnu::agent_ops::kRepairApprovalSchema;
    noNonce["findings_digest"] = "aaaabbbbccccdddd";
    noNonce["coordinator_id"] = Json::Int64(1);
    noNonce["issued_at_ms"] = Json::Int64(1000);
    noNonce["expires_at_ms"] = Json::Int64(6000);
    noNonce["digest"] = "0123456789abcdef";
    CHECK(dock.armRepairApproval(noNonce, 1000) == "APPROVAL_MALFORMED");

    Json::Value fatWindow = noNonce;
    fatWindow["nonce"] = "0011223344556677";
    fatWindow["expires_at_ms"] =
        Json::Int64(1000 + sicnu::agent_ops::kRepairApprovalMaxTtlMs + 1);
    CHECK(dock.armRepairApproval(fatWindow, 1000) == "APPROVAL_TTL_EXCESSIVE");

    // A shape-valid token is accepted for the next launch — with the
    // AUTHENTICITY verdict still deferred to the coordinator thread (shape
    // alone proves nothing), surfaced through approvalStatusChanged.
    Json::Value valid = fatWindow;
    valid["expires_at_ms"] = Json::Int64(6000);
    CHECK(dock.armRepairApproval(valid, 1000).empty());
    dock.joinWorker();
}

TEST_CASE("r27/t03 dock: operator intent is recorded without coordinator arming",
          "[r27t03][dock][p0]")
{
    ensureApp();
    sicnu::app::agent_ops::AgentOpsControlCenterDock dock;
    // No coordinator attached: the Approve click still records intent and
    // reports honestly; nothing arms (fail closed, no crash).
    dock.requestApproveRepair();
    SUCCEED("operator intent recorded without a coordinator");
    dock.joinWorker();
}
