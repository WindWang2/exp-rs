#pragma once

// Shared bounded-wait primitives (R6 test-determinism track, WP-B/WP-D).
//
// Two shapes cover the waits this suite needs:
//
//   waitUntil(pred, budgetMs, describe, pump)  — main-thread predicate wait.
//     Polls `pred` (optionally pumping the Qt event loop via `pump`) until it
//     holds or `budgetMs` elapses, then prints expected/actual/elapsed so a
//     failed `REQUIRE(waitUntil(...))` names the state it waited for instead
//     of degrading into a bare bool. Replaces `while(!flag) sleep` on the
//     waiting side and fixed `msleep` synchronization sleeps.
//
//   TestGate — worker-side release gate. Replaces the
//     `while (!release.load()) sleep(1)` executor-lambda shape, whose failure
//     mode was: an assertion between submit and release aborts the case with
//     the gate closed, the worker spins until the ctest timeout, and the hang
//     masks the real failure. A TestGate opens itself after its budget (and
//     flags the timeout), so a failing test degrades to a fast, diagnosable
//     CHECK instead of a hang. The owning thread asserts `!timedOut()` after
//     the run.
//
// Deliberately Qt-free so io/sdk/study test targets (no Qt link) can use it;
// Qt call sites pass their own event-loop pump as `pump`.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace sicnu_test
{

/// Poll interval: short enough that a signal-driven transition is observed
/// within a few ms of landing, long enough not to burn a core while waiting.
inline constexpr int kWaitPollMs = 5;

inline bool waitUntil( const std::function<bool()> &pred,
                       int budgetMs,
                       const std::function<std::string()> &describe = {},
                       const std::function<void()> &pump = {} )
{
  const auto start = std::chrono::steady_clock::now();
  const auto deadline = start + std::chrono::milliseconds( budgetMs );
  while ( !pred() )
  {
    if ( std::chrono::steady_clock::now() >= deadline )
    {
      std::fprintf( stderr,
                    "[sicnu_test::waitUntil] timed out after %d ms (budget %d ms); "
                    "expected: predicate true; actual: %s\n",
                    static_cast<int>( std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - start )
                                        .count() ),
                    budgetMs,
                    describe ? describe().c_str() : "(no state description provided)" );
      std::fflush( stderr );
      return false;
    }
    if ( pump )
      pump();
    std::this_thread::sleep_for( std::chrono::milliseconds( kWaitPollMs ) );
  }
  return true;
}

/// A release gate for executor/pool worker threads, with a self-opening
/// deadline budget so no failure mode on the test thread can strand a worker
/// spinning until the harness timeout. Worker side calls wait(); the owning
/// test thread calls open() when the scenario may proceed and asserts
/// !timedOut() once the run settles (a timed-out gate means the test never
/// reached its open() — the deadline converted the hang into a fast failure
/// with the release point named).
class TestGate
{
public:
  explicit TestGate( int budgetMs = 30000 ) : budgetMs_( budgetMs ) {}

  TestGate( const TestGate & ) = delete;
  TestGate &operator=( const TestGate & ) = delete;

  /// Worker side: blocks until open() or the budget expires, whichever first.
  void wait() const
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( budgetMs_ );
    std::unique_lock<std::mutex> lock( mx_ );
    if ( !cv_.wait_until( lock, deadline, [this] { return opened_.load(); } ) )
    {
      timedOut_.store( true );
      std::fprintf( stderr,
                    "[sicnu_test::TestGate] gate timed out unopened after %d ms "
                    "(worker released by deadline; the test thread never reached open())\n",
                    budgetMs_ );
      std::fflush( stderr );
    }
  }

  /// Owning thread: release the worker. The store happens under mx_ so the
  /// notify cannot slip through the window between the waiter's final
  /// predicate check (evaluated under mx_) and its block — without the lock
  /// a notify landing there is lost and the worker sleeps its whole budget
  /// in a PASSING run (review P1, R6).
  void open() const
  {
    {
      std::lock_guard<std::mutex> lock( mx_ );
      opened_.store( true );
    }
    cv_.notify_all();
  }

  bool opened() const { return opened_.load(); }
  /// True when wait() returned because the budget expired, not because the
  /// test opened the gate. Assert !timedOut() on the owning thread.
  bool timedOut() const { return timedOut_.load(); }

private:
  mutable std::mutex mx_;
  mutable std::condition_variable cv_;
  mutable std::atomic_bool opened_{ false };
  mutable std::atomic_bool timedOut_{ false };
  int budgetMs_;
};

} // namespace sicnu_test
