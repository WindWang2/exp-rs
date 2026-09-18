// scratch_registry.h — Budgeted scratch leases for external-memory tile
// streams (LSEE 10.0, ADR 0148 §4).
//
// A run's spill intermediates live under one directory
// `<root>/<runId>/` with deterministic names. Every byte the run puts on
// disk is accounted against the registry's budget BEFORE the write happens:
// `acquire()` refuses (typed) when the run's outstanding scratch would
// exceed the budget — scratch pressure becomes an admission/planning fact,
// never an ENOSPC surprise mid-stream.
//
// Leases are RAII and reference-counted: copying a ScratchLease retains the
// bytes, destroying the last copy releases them (and unlinks the file if it
// was still provisional). `finalize()` fsyncs and atomically renames a
// lease's `.part` file to its final name (same temp+rename family as the
// workflow checkpoint), so a crash leaves either the previous content or a
// complete `.part` that the stale sweep removes — never a half-written
// final file that looks readable.
//
// Qt-free and header-light: implemented over std::filesystem so sicnu_runtime
// keeps its no-Qt charter.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::runtime::chunk
{

class ScratchRegistry; // F-A-2: forward decl for the atomic detachment pointer

/// Raised by acquire() when the budget cannot cover the request. Carries the
/// structured need/have numbers for the caller's actionable refusal.
struct ScratchBudgetExceeded : std::runtime_error
{
    ScratchBudgetExceeded( std::uint64_t needBytes, std::uint64_t outstandingBytes,
                           std::uint64_t budgetBytes )
        : std::runtime_error( "scratch budget exceeded: need " + std::to_string( needBytes )
                              + " B, outstanding " + std::to_string( outstandingBytes )
                              + " B, budget " + std::to_string( budgetBytes ) + " B" )
        , need( needBytes )
        , outstanding( outstandingBytes )
        , budget( budgetBytes )
    {
    }
    std::uint64_t need;
    std::uint64_t outstanding;
    std::uint64_t budget;
};

/// One scratch file. Copying the lease retains the bytes; the last copy
/// releases them (provisional files are unlinked on release).
class ScratchLease
{
  public:
    ScratchLease() = default;

    // Bodies live in scratch_registry.cpp: the inline members would need the
    // complete Entry type, which is only defined there.
    const std::string &path() const;
    const std::string &finalPath() const;
    std::uint64_t bytes() const;
    bool isValid() const { return m_impl != nullptr; }
    /// True once finalize() renamed the file to its final (durable) name.
    bool isFinalized() const;
    /// Writes the content digest sidecar for the CURRENT file content
    /// (writers call this after filling the file, before finalize()), so a
    /// crash restart can re-prove the bytes via verifyDigest().
    void sealDigest() const;
    /// Atomically publishes the file: fsync + rename `<name>.part` → `<name>`.
    /// Idempotent; returns the final path.
    const std::string &finalize() const;
    /// Re-proves the file's content digest after a crash (see ScratchRegistry).
    bool verifyDigest() const;

    struct Entry; // defined in scratch_registry.cpp; shared_ptr refcounts it

  private:
    friend class ScratchRegistry;
    struct Deleter
    {
        void operator()( Entry *entry ) const;
    };
    std::shared_ptr<Entry> m_impl; // shared_ptr IS the refcount
    static const std::string k_empty;

    ScratchLease( std::shared_ptr<Entry> impl );
};

class ScratchRegistry
{
  public:
    struct Config
    {
        std::string root; ///< scratch root directory; empty → platform temp
        /// Byte budget for OUTSTANDING scratch across all runs of this
        /// registry. 0 = unbounded (tests / trusted single-run hosts).
        std::uint64_t budgetBytes = 0;
    };

    explicit ScratchRegistry( Config config );
    ~ScratchRegistry();

    ScratchRegistry( const ScratchRegistry & ) = delete;
    ScratchRegistry &operator=( const ScratchRegistry & ) = delete;

    /// Acquires @p bytes of scratch for @p runId under a deterministic name
    /// (`<counter>-<stem>.part`). Refuses (ScratchBudgetExceeded) when the
    /// budget can't cover outstanding + bytes. The file is created empty;
    /// writers open `lease.path()` themselves.
    /// Path-component contract (#1056): @p runId and @p stem become path
    /// elements, so they are validated at this boundary — non-empty, ≤ 200
    /// bytes, restricted to [A-Za-z0-9._-], never a dot element — and any
    /// violation throws std::invalid_argument instead of escaping the
    /// per-run directory.
    ScratchLease acquire( const std::string &runId, const std::string &stem,
                          std::uint64_t bytes );

    /// Outstanding (acquired, not yet released) bytes across all runs.
    std::uint64_t outstandingBytes() const;
    /// Per-run view for admission/telemetry.
    std::uint64_t outstandingBytes( const std::string &runId ) const;

    /// Removes every run directory under @p root whose last modification is
    /// older than @p age (crashed-run sweep). Returns the count of run
    /// directories removed. Static: runnable from startup recovery without a
    /// registry instance. STARTUP-ONLY contract (F-A-17): a live run's
    /// directory mtime stops advancing while it only appends to existing
    /// files, so the sweep must never run concurrently with active runs on
    /// the same root.
    static std::size_t sweepStale( const std::string &root,
                                   std::chrono::milliseconds age );

    /// Resolves the effective root (config root or platform temp default).
    std::string root() const;

  private:
    friend class ScratchLease;
    void releaseEntry( ScratchLease::Entry *entry );
    void unaccountLocked( const std::string &runId, std::uint64_t bytes );

    Config m_config;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::uint64_t> m_outstandingByRun;
    std::uint64_t m_outstandingTotal = 0;
    std::uint64_t m_nextFileCounter = 1;
    std::filesystem::path m_rootPath;
    /// Live leases (weak): the destructor detaches them so a lease released
    /// after the registry's death can never call back into freed memory.
    /// Expired entries are pruned on acquire.
    std::vector<std::weak_ptr<ScratchLease::Entry>> m_liveEntries;
};

} // namespace sicnu::runtime::chunk
