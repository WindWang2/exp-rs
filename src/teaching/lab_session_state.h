// LabSessionState — teaching navigation session (course/lab/run refs only).
// Does NOT duplicate ExperimentStore or sicnu.lab-session/1 stage machines.
// Corrupt documents fail closed (ok=false); never partially adopted.
#pragma once

#include "teaching/lab_status.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::teaching {

inline constexpr const char *kTeachingSessionSchema = "sicnu.teaching.session/1";

struct LabSessionState {
  std::string schema = kTeachingSessionSchema;
  bool ok = false;
  std::string sessionId;
  std::string courseId;
  std::string moduleId;
  std::string labId;
  std::string experimentId;   // navigation ref only
  std::string runId;          // navigation ref only
  int stepIndex = 0;
  std::vector<std::string> evidenceRefs;
  std::string autonomyPolicyRef; // path or id pointer
  Json::Value lastValidationSummary; // projected feedback summary (no goldens)
  std::string capsuleExportRef;
  ExperienceMode mode = ExperienceMode::Beginner;
  LabUiStatus labStatus = LabUiStatus::NotStarted;
  std::vector<std::string> issuesZh;

  static LabSessionState makeNew( const std::string &sessionId,
                                  const std::string &courseId,
                                  const std::string &labId,
                                  ExperienceMode mode = ExperienceMode::Beginner );

  /// Strict parse — unknown schema/keys/types → ok=false + issues.
  static LabSessionState fromJson( const Json::Value &doc );

  Json::Value toJson() const;

  /// Serialize/deserialize helpers (byte-stable styled JSON).
  std::string serialize() const;
  static LabSessionState deserialize( const std::string &bytes );

  /// Filesystem save/load — corrupt/missing → fail-closed empty state.
  bool saveToFile( const std::string &path ) const;
  static LabSessionState loadFromFile( const std::string &path );
};

} // namespace sicnu::teaching
