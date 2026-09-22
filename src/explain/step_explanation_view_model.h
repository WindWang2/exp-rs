/***************************************************************************
 * step_explanation_view_model.h — presentation model for step explanations
 *
 * Pure logic (no widgets): projects a StepExplanation into ordered,
 * badge-tagged sections for the teaching panel and a deterministic
 * markdown rendering for CLI/agent surfaces. UI code must render this
 * model, never re-derive explanation semantics.
 ***************************************************************************/
#pragma once

#include "explain/step_explanation.h"

#include <string>
#include <vector>

namespace sicnu::explain
{

struct ExplanationLine
{
  std::string text;
  std::string badge; // 系统事实 | 编写指引 | 推断 | "" (structural)
  std::string evidenceNote;
};

struct ExplanationSection
{
  std::string id; // why | prerequisites | assumptions | state | parameters | skip | execution | sources
  std::string titleZh;
  std::vector<ExplanationLine> lines;
};

struct StepExplanationViewModel
{
  std::string headline;
  std::string operatorLine;
  std::vector<ExplanationSection> sections;
  std::vector<std::string> trustNotes;

  static StepExplanationViewModel fromExplanation( const StepExplanation &explanation );

  // Deterministic rendering: identical models always render identical text.
  std::string toMarkdown() const;
};

} // namespace sicnu::explain
