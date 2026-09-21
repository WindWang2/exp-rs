/***************************************************************************
  lab/hint_policy.h
  Budgeted, escalating hint reveals (ADR 0174).

  Teaching discipline as code: hints are revealed per target in escalation
  order (no level skipping), the per-target budget from hints.policy is a
  hard ceiling, and every reveal is recorded in the session ledger — the
  budget survives persistence, so a restart cannot reset what a student has
  already seen. Over-budget or out-of-order requests are typed refusals
  (the lab_copilot "宁可少帮，不可代做" discipline at the runtime layer).
***************************************************************************/

#ifndef SICNU_LAB_HINT_POLICY_H
#define SICNU_LAB_HINT_POLICY_H

#include "session_state.h"

namespace sicnu::lab
{

struct HintReveal
{
  int level = 0;
  std::string text;
  std::string textZh;
};

/// Reveals the hint `entry` addressed at `target` ("step:<0-based index>" or
/// "checkpoint:<id>") at `level` (1-based into hints.policy.escalation).
///
/// Refusals: non-active session (`lab.session.bad_transition`), no entries
/// for the target (`lab.session.hint_unknown_target`), level beyond the
/// escalation vocabulary (`lab.session.field`), skipping the next unrevealed
/// level (`lab.session.hint_level_gap`), no entry at that level for the
/// target (`lab.session.hint_exhausted`), per-target budget spent
/// (`lab.session.hint_budget`).
LabResult<HintReveal> revealHint( LabSession &session, const LabRuntimePlan &plan,
                                  const std::string &target, int level );

} // namespace sicnu::lab

#endif // SICNU_LAB_HINT_POLICY_H
