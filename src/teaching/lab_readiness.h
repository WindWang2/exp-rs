// LabReadiness — aggregates inspector/passport/prereqs/operator availability.
// Each item carries reason + evidence source. Fail-closed UNKNOWN on gaps.
#pragma once

#include "teaching/lab_status.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::teaching {

struct ReadinessItem {
  std::string id;
  std::string category; // prereq | pack | operator | passport | inspector | scientific | offline
  std::string severity; // block | warn | info | unknown
  std::string reasonZh;
  std::string evidenceSource; // e.g. availability.operator, passport.crs
  bool ok = false;
};

struct LabReadiness {
  std::string schema = "sicnu.teaching.lab_readiness/1";
  std::string labId;
  ReadinessLevel level = ReadinessLevel::Unknown;
  std::vector<ReadinessItem> items;
  std::vector<std::string> issuesZh;
  bool offlineCapable = true;

  /// Build from curriculum availability lab slice + optional passport/inspector
  /// facts and scientific conflict flags. Missing inputs → UNKNOWN items.
  static LabReadiness aggregate( const std::string &labId,
                                 const Json::Value &availabilityLabSlice,
                                 const Json::Value &passportFacts,
                                 const Json::Value &inspectorFacts,
                                 const Json::Value &scientificFlags,
                                 bool offlineMode );

  Json::Value toJson() const;
};

} // namespace sicnu::teaching
