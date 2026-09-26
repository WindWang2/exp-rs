// src/app/teaching/lab_operator_launch.h
#pragma once

#include <json/json.h>

#include <QString>

#include <string>
#include <vector>

namespace sicnu::app::teaching {

/// Outcome of preparing a cockpit operator launch against the runtime
/// operator registry. Parameter checking is the platform's ONE validator
/// (sicnu::processing::validateParameters) over the operator's registered
/// descriptor — this seam never re-implements schema rules.
struct LabOperatorLaunchPlan
{
  bool ok = false;                    ///< launch may proceed (params prefillable)
  Json::Value params;                 ///< schema-checked params (object; empty object when none)
  std::vector<std::string> issuesZh;  ///< typed refusal reasons when !ok
};

/// Validates a lab step's params for the operator launch seam.
///
/// - Empty/unspecified params → ok with an empty object: the operator surface
///   opens with its own defaults (today's behavior is preserved, and the
///   shell must not claim a prefill happened).
/// - Unparseable JSON / non-object params → refused with a typed reason.
/// - Unknown operator (no registered parameter schema) with params → refused
///   fail-closed: nothing downstream would check them, so injecting or
///   silently dropping both lie.
/// - Registered operator → params must pass validateParameters against the
///   operator's descriptor (unknown keys, wrong types, out-of-range values
///   and enum misses are rejection reasons, quoted per parameter).
///
/// Never touches the filesystem and never executes anything: prefill only.
LabOperatorLaunchPlan prepareLabOperatorLaunch( const QString &operatorId,
                                                const QString &paramsJson );

/// Rewrites "data/…" string values under @p params to @p dataRoot-anchored
/// absolute paths so the prefilled form works regardless of the process cwd.
/// Everything else (including "outputs/…" targets) is left untouched: output
/// destinations stay student-visible and student-editable, not silently
/// relocated. Returns @p params unchanged when it is not an object.
Json::Value absolutizeLabInputPaths( const Json::Value &params, const QString &dataRoot );

/// Shell-side prefill parse: strict JSON object parse for the launcher seam.
/// Returns a null value (with the error in @p error) on malformed input —
/// the caller must not prefill from it.
Json::Value parseLabPrefillParams( const QString &paramsJson,
                                   std::string *error = nullptr );

} // namespace sicnu::app::teaching
