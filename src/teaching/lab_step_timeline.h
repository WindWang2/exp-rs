// LabStepTimeline — projects LabSpec or ScientificRecipe stages into a
// student timeline (current/prev/next, I/O, params, why hooks, human-only).
#pragma once

#include "teaching/lab_status.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::teaching {

struct LabStepItem {
  std::string stepId;
  int index = 0;
  std::string titleZh;
  std::string title;
  std::string descriptionZh;
  std::string kind; // operator | human_only | reflection | manual | ui_action
  std::string operatorId; // empty when non-operator
  Json::Value params;     // LabSpec-fixed params (may be masked for students)
  Json::Value paramsDisplay; // after teaching mask
  std::string boundary;   // ui_action|manual|reflection|judgment|""
  std::string teachingNote;
  std::string completionHint;
  std::string commonErrorsZh;
  std::string whyHintZh;  // pointer text; full explain comes from explain VM
  bool humanRequired = false;
  bool aiAllowed = true;  // further gated by autonomy display
  bool paramsStudentDecision = false;
  LabUiStatus executionStatus = LabUiStatus::NotStarted;
  std::string evidenceRef;
};

struct LabStepTimeline {
  std::string schema = "sicnu.teaching.lab_timeline/1";
  bool ok = false;
  std::string labId;
  std::string titleZh;
  std::string objectiveZh;
  std::string sourceKind; // labspec | scientific_recipe | unknown
  int currentIndex = 0;
  std::vector<std::string> issuesZh;
  std::vector<LabStepItem> steps;

  const LabStepItem *current() const;
  const LabStepItem *previous() const;
  const LabStepItem *next() const;

  /// From LabSpec JSON (spec_version 1/2 shape with steps[]).
  static LabStepTimeline fromLabSpec( const Json::Value &labSpec, int currentIndex = 0 );

  /// From ScientificRecipe JSON (sicnu.scientific_recipe/1 stages[]).
  static LabStepTimeline fromScientificRecipe( const Json::Value &recipe, int currentIndex = 0 );

  /// Prefer recipe when schema matches; else LabSpec; else fail-closed.
  static LabStepTimeline fromLabDocument( const Json::Value &doc, int currentIndex = 0 );

  /// Masks keys listed in @p studentDecisionKeys as "***" in paramsDisplay
  /// while keeping raw params for operator prefill under autonomy gates.
  void applyTeachingMask( const std::vector<std::string> &studentDecisionKeys );

  Json::Value toJson() const;
};

} // namespace sicnu::teaching
