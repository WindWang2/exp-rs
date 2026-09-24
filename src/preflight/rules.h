// rules.h — the ten builtin rule families of RS14-02 slice B.
//
// Each family maps to one versioned rule with a stable "preflight.*" id and
// a typed SPF_* code vocabulary. Every rule degrades to a typed unknown
// (<CODE>_UNKNOWN, require_ack, basis "unknown") when facts are missing and
// consults NO authority other than the RuleFacts seam.

#pragma once

#include "preflight/rule.h"

#include <vector>

namespace sicnu::preflight {

// Rule ids (stable registry keys).
namespace rule_id {
inline constexpr const char *BandRole = "preflight.band_role";
inline constexpr const char *PairCrs = "preflight.pair_crs";
inline constexpr const char *ResolutionRatio = "preflight.pair_resolution_ratio";
inline constexpr const char *RadiometricState = "preflight.radiometric_state_policy";
inline constexpr const char *Modality = "preflight.modality_policy";
inline constexpr const char *QualityMask = "preflight.quality_mask";
inline constexpr const char *Temporal = "preflight.temporal_policy";
inline constexpr const char *Leakage = "preflight.train_eval_leakage";
inline constexpr const char *ModelCompat = "preflight.model_compatibility";
inline constexpr const char *OperatorKnown = "preflight.operator_known";
} // namespace rule_id

// Fact factories — one per builtin rule, in id order.
PreflightRulePtr makeBandRoleRule();
PreflightRulePtr makePairCrsRule();
PreflightRulePtr makeResolutionRatioRule();
PreflightRulePtr makeRadiometricStateRule();
PreflightRulePtr makeModalityRule();
PreflightRulePtr makeQualityMaskRule();
PreflightRulePtr makeTemporalRule();
PreflightRulePtr makeLeakageRule();
PreflightRulePtr makeModelCompatRule();
PreflightRulePtr makeOperatorKnownRule();

/// All ten builtin rules, sorted by id — registration stays deterministic at
/// every call site.
std::vector<PreflightRulePtr> builtinRules();

} // namespace sicnu::preflight
