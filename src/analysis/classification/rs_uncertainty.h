// rs_uncertainty.h — Classification & Object Intelligence 11.0 (F12).
//
// Machine-verifiable uncertainty measures over one class-probability row
// (column k ↔ class order k, RsClassOrder), and the reject-option policy.
//
// Definitions (locked by test, see docs/processing/classification-intelligence.md):
//   entropy      H  = −Σ_k p_k·log2(p_k), with 0·log2(0) := 0. Range [0, log2(K)].
//   margin       M  = p(1) − p(2) after sorting the row descending. Range [0, 1].
//   confidence   C  = max_k p_k. Range [0, 1].
//   ensemble disagreement (population variance, averaged per class):
//                   D = (1/K)·Σ_k (1/m)·Σ_{j=1..m} (p_{j,k} − μ_k)²,
//                   μ_k = (1/m)·Σ_j p_{j,k}. Range [0, 0.25] for probabilities.
//
// Reject policy (uniform "low quality" direction):
//   Entropy    rejected ⇔ H ≥ threshold
//   Margin     rejected ⇔ M ≤ threshold
//   Confidence rejected ⇔ C ≤ threshold
#pragma once

#include "qgis_analysis_export.h"

#include <span>

class QGIS_ANALYSIS_EXPORT RsUncertainty
{
  public:
    enum class Measure
    {
      Entropy = 0,
      Margin,
      Confidence,
    };

    /// Shannon entropy, log2 base. Fails when the row holds a negative or
    /// non-finite value or sums outside 1 ± 1e-3 (callers must normalise).
    static bool entropy( std::span<const float> probs, double &outH );

    /// Top-1 minus top-2 probability. Same validation as entropy().
    static bool margin( std::span<const float> probs, double &outM );

    /// Maximum probability. Same validation as entropy().
    static bool confidence( std::span<const float> probs, double &outC );

    /// Normalised entropy: H / log2(K) in [0,1] (K > 1 required).
    static double normalizeEntropy( double h, int classCount );

    /// Per-row measure value for a probability row.
    static bool measure( Measure m, std::span<const float> probs, double &out );

    /// Reject policy with the per-measure direction documented above.
    static bool isRejected( Measure m, double value, double threshold );

    /// Ensemble disagreement over m members × K classes (row-major,
    /// member-major: member j occupies slots [j*K, (j+1)*K)). Fails on
    /// invalid input (m < 2, K < 1, non-finite or negative values).
    static bool ensembleDisagreement( std::span<const float> memberProbs,
                                      int memberCount,
                                      int classCount,
                                      double &outD );
};
