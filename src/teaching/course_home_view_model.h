// CourseHomeViewModel — projects sicnu.curriculum/1 + progress + availability.
// Pure value model; no singleton, no I/O. Fail-closed on conflicted inputs.
#pragma once

#include "teaching/lab_status.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::teaching {

struct CourseLabCard {
  std::string labId;
  std::string moduleId;
  std::string titleZh;
  std::string role; // core | optional | ...
  int estimatedMinutes = 0;
  std::vector<std::string> requiredDataPacks;
  std::vector<std::string> learningGoals; // from module outcomes (shared) + lab notes
  LabUiStatus status = LabUiStatus::Unknown;
  std::vector<std::string> blockersZh;
  std::vector<std::string> warningsZh;
  bool resolvable = false;
  std::string resolution; // labspec|registry|external|unknown
};

struct CourseModuleCard {
  std::string moduleId;
  int index = 0;
  std::string titleZh;
  std::string summaryZh;
  std::vector<std::string> learningOutcomes;
  std::vector<std::string> prerequisiteModules;
  int estimatedMinutes = 0;
  bool optional = false;
  int labsDone = 0;
  int labsTotal = 0;
  std::vector<CourseLabCard> labs;
  LabUiStatus status = LabUiStatus::Unknown;
};

struct CourseHomeViewModel {
  std::string schema = "sicnu.teaching.course_home/1";
  bool ok = false;
  std::string courseId;
  std::string titleZh;
  std::string audienceZh;
  int overallPercent = 0;
  ExperienceMode mode = ExperienceMode::Beginner;
  std::vector<std::string> issuesZh;
  std::vector<CourseModuleCard> modules;
  /// First incomplete, ready-or-in-progress lab for "continue".
  std::string continueLabId;
  std::string continueModuleId;

  /// Project from authoritative docs. Missing/invalid → ok=false, issues filled.
  /// @p progressSummary is sicnu.curriculum.progress.summary/1 (or null).
  /// @p availability is sicnu.curriculum.availability/1 (or null → UNKNOWN readiness).
  static CourseHomeViewModel fromDocuments( const Json::Value &manifest,
                                            const Json::Value &progressSummary,
                                            const Json::Value &availability,
                                            ExperienceMode mode = ExperienceMode::Beginner );

  Json::Value toJson() const;
};

} // namespace sicnu::teaching
