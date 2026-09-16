// sar_pair_network.h — multi-temporal interferometric pair network
// (Advanced InSAR 11.0, package E; DECISIONS D-005).
//
// Builds the pair graph of a scene stack under temporal/perpendicular
// baseline constraints, checks connectivity, and exposes the honest QA
// (components, reference) the inversion (package F) requires.
//
// SCENE TRUTH (package A): every scene is an InSarSceneTruth — validated
// acquisition UTC, wavelength, orbit segment. The network refuses (typed,
// domain-coded) instead of guessing when any scene fails validation, when
// wavelengths disagree across the stack (WAVELENGTH_INCOMPATIBLE — relative
// tolerance 1e-9, the same rule as buildPairTruth), or when two orbits
// with absolute anchors cannot share an imaged window
// (ORBIT_EPOCH_MISMATCH per pair).
//
// BASELINE SCREENING METRIC: network filtering uses the B⊥ evaluated at
// the MASTER scene's orbit mid-time, at the nadir ground point of that
// mid-time position (height 0, radial LOS). This is a SCREENING metric —
// one number per pair for graph construction — not a per-pixel product;
// per-pixel baselines come from pairBaselineAtGround. Both orbits must
// bracket the master mid-time (else BASELINE_NO_ZERO_DOPPLER_* / refusal).
//
// STRATEGIES: AllPairs (every eligible pair) and Consecutive (each scene
// to its temporal successor only — the minimal connected chain when every
// consecutive pair passes the constraints).
//
// FAIL-CLOSED SEMANTICS (Oracle 2): metadata problems are always typed
// refusals. A graph that ends up DISCONNECTED under the constraints is a
// refusal too (PAIR_GRAPH_DISCONNECTED) unless allowDisconnected is set —
// in which case the result carries the component map and connected=false,
// explicitly, for QA use. Nothing is silently filtered out: eligibility
// decisions are all visible in the returned pair set.
//
// Bounded scale: scenes ≤ 512, eligible pairs ≤ 65536 (typed refusals
// beyond — PERFORMANCE.md; the O(scenes²) pair table must not silently
// materialize for runaway stacks).
#pragma once

#include "sar_baseline.h"

#include <QString>
#include <vector>

namespace sicnu::sar
{

enum class PairStrategy
{
    AllPairs,
    Consecutive,
};

struct PairNetworkParams
{
    PairStrategy strategy = PairStrategy::AllPairs;
    /// Constraint bounds; NaN (kUnset) = unconstrained. Perpendicular
    /// bounds apply to |B⊥| (the screening metric is sign-free).
    double maxTemporalDays = kUnset;
    double minPerpendicularM = kUnset;
    double maxPerpendicularM = kUnset;
    int referenceIdx = 0;     ///< index into the (caller-ordered) scene list
    bool allowDisconnected = false;
};

struct InSarNetworkPair
{
    int masterIdx = -1;
    int slaveIdx = -1;
    double temporalDays = 0.0;   ///< signed, slave − master
    double perpendicularM = 0.0; ///< screening B⊥ (magnitude)
    double parallelM = 0.0;
    double magnitudeM = 0.0;
};

struct PairNetworkResult
{
    std::vector<InSarNetworkPair> pairs;
    int referenceIdx = -1;
    int componentCount = 0;                  ///< union-find components over scenes
    std::vector<int> componentOfScene;       ///< per-scene component label
    bool connected = false;
    double maxPerpendicularSeenM = 0.0;      ///< QA summary over returned pairs
    double maxTemporalSeenDays = 0.0;
};

/// Builds and validates the pair network. Returns false with a
/// domain-coded @a error on any fail-closed condition.
bool buildPairNetwork( const std::vector<InSarSceneTruth> &scenes,
                       const PairNetworkParams &params,
                       PairNetworkResult *out, QString *error = nullptr );

} // namespace sicnu::sar
