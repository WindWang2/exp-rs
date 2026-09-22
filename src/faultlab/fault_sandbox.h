// fault_sandbox.h/.cpp — the sandbox contract of the fault lab.
//
// Rules (enforced here and re-verified by the runner):
//   * faults only ever mutate sandbox copies — copyOf/copyWithinBudget are
//     the only sanctioned way to obtain a mutable fixture;
//   * every sandbox has a byte budget; a fixture that does not fit is
//     refused with a typed `faultlab.budget_exceeded` BEFORE any copy;
//   * cleanup is verified: a sandbox reports no residue only when its
//     directory is really gone, and the destructor cleans up abandoned
//     sandboxes so a failing assertion cannot leak temp directories.
#pragma once

#include "fault_types.h"

#include <cstdint>
#include <string>

namespace sicnu::faultlab
{

class FaultSandbox
{
  public:
  public:
    /// Default-constructed sandbox is inactive with an empty path —
    /// cleanup() is a no-op and verifyNoResidue() reports no residue. It
    /// exists because FaultResult<T> requires a default-constructible value
    /// slot; usable sandboxes only come from create().
    FaultSandbox() = default;

    static FaultResult<FaultSandbox> create( const std::string &root = {} );

    ~FaultSandbox();

    FaultSandbox( const FaultSandbox & ) = delete;
    FaultSandbox &operator=( const FaultSandbox & ) = delete;
    FaultSandbox( FaultSandbox &&other ) noexcept;
    FaultSandbox &operator=( FaultSandbox &&other ) noexcept;

    const std::string &path() const
    {
        return mPath;
    }

    bool active() const
    {
        return mActive;
    }

    /// Removes the sandbox tree (best effort; sets active=false).
    void cleanup();

    /// True when the sandbox directory no longer exists.
    bool verifyNoResidue() const;

    /// Deep copy of a fixture grid. NaN no-data samples, band metadata and
    /// extras are duplicated; the source is never touched.
    static FaultGrid copyOf( const FaultGrid &source );

    /// copyOf, but refused with `faultlab.budget_exceeded` when the fixture's
    /// sample bytes exceed `maxBytes`. The budget is checked before any
    /// allocation so an oversized fixture cannot exhaust the sandbox.
    static FaultResult<FaultGrid> copyWithinBudget( const FaultGrid &source,
                                                    std::uint64_t maxBytes );

  private:
    std::string mPath;
    bool mActive = false;
};

} // namespace sicnu::faultlab
