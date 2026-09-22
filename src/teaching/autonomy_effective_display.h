// AutonomyEffectiveDisplay — display/request over existing autonomy gates.
// Never invents policy; projects sicnu.autonomy-status/1 + decideAutonomy.
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::teaching {

struct AutonomyCapabilityRow {
  std::string capability;
  std::string decision; // allow | deny | limited
  std::string reasonCode;
  std::string reasonZh;
  std::string downgradeTo;
};

struct AutonomyEffectiveDisplay {
  std::string schema = "sicnu.teaching.autonomy_display/1";
  bool ok = false;
  std::string effectiveLevel; // L0..L5
  int effectiveOrdinal = 0;
  std::string mode;
  std::string role;
  std::string domain;
  std::vector<AutonomyCapabilityRow> rows;
  std::vector<std::string> issuesZh;
  /// Ladder labels for viz (always 6 entries when ok).
  std::vector<std::string> ladderLabelsZh;

  /// Project from an existing autonomy-status document (prefer).
  static AutonomyEffectiveDisplay fromStatusDoc( const Json::Value &statusDoc );

  /// Build via Sicnu::autonomy APIs when linked; also accepts raw policy JSON
  /// and produces a display using status projection helper in .cpp.
  static AutonomyEffectiveDisplay fromPolicyDoc( const Json::Value &policyDoc,
                                                 const std::string &role,
                                                 const std::string &domain = "lab" );

  /// Request a capability: returns allow/deny/downgrade projection without
  /// mutating policy. Uses decideAutonomy under the hood.
  Json::Value requestCapability( const Json::Value &policyDoc,
                                 const std::string &capability,
                                 const std::string &role,
                                 const std::string &domain = "lab" ) const;

  Json::Value toJson() const;
};

} // namespace sicnu::teaching
