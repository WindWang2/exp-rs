/***************************************************************************
  lab/hint_policy.cpp — see hint_policy.h
***************************************************************************/

#include "hint_policy.h"

#include <algorithm>
#include <cstdlib>

namespace sicnu::lab
{
namespace
{

bool isHintTarget( const HintEntry &entry, const std::string &target )
{
  if ( entry.hasStep )
  {
    return target == "step:" + std::to_string( entry.targetStep );
  }
  if ( entry.hasCheckpoint )
    return target == "checkpoint:" + entry.targetCheckpoint;
  return false;
}

} // namespace

LabResult<HintReveal> revealHint( LabSession &session, const LabRuntimePlan &plan,
                                  const std::string &target, int level )
{
  if ( session.state != SessionState::Active )
    return LabResult<HintReveal>::failure(
      { LabDiag{ "lab.session.bad_transition", "cannot reveal hints on a non-active session" } } );

  bool targetExists = false;
  for ( const HintEntry &entry : plan.hints.entries )
  {
    if ( isHintTarget( entry, target ) )
    {
      targetExists = true;
      break;
    }
  }
  if ( !targetExists )
    return LabResult<HintReveal>::failure(
      { LabDiag{ "lab.session.hint_unknown_target", "no hint entries for '" + target + "'" } } );

  if ( level < 1 || level > static_cast<int>( plan.hints.policy.escalation.size() ) )
    return LabResult<HintReveal>::failure(
      { LabDiag{ "lab.session.field",
                 "hint level must be within 1.." + std::to_string( plan.hints.policy.escalation.size() ) } } );

  // Budget first: a spent budget refuses even a correct next level.
  int revealedForTarget = 0;
  int maxRevealedLevel = 0;
  for ( const HintEvent &event : session.hintEvents )
  {
    if ( event.target != target )
      continue;
    ++revealedForTarget;
    maxRevealedLevel = std::max( maxRevealedLevel, event.level );
  }
  if ( plan.hints.policy.maxRevealsPerTarget > 0 &&
       revealedForTarget >= plan.hints.policy.maxRevealsPerTarget )
  {
    return LabResult<HintReveal>::failure( { LabDiag{
      "lab.session.hint_budget",
      "hint budget for '" + target + "' is spent (" +
        std::to_string( revealedForTarget ) + "/" +
        std::to_string( plan.hints.policy.maxRevealsPerTarget ) + ") — ask your teacher" } } );
  }

  // Escalation discipline: reveal levels in order, no skipping.
  if ( level > maxRevealedLevel + 1 )
    return LabResult<HintReveal>::failure( { LabDiag{
      "lab.session.hint_level_gap",
      "hints for '" + target + "' must be revealed in escalation order (next is level " +
        std::to_string( maxRevealedLevel + 1 ) + ")" } } );

  const HintEntry *entry = nullptr;
  for ( const HintEntry &candidate : plan.hints.entries )
  {
    if ( isHintTarget( candidate, target ) && candidate.level == level )
    {
      entry = &candidate;
      break;
    }
  }
  if ( !entry )
    return LabResult<HintReveal>::failure( { LabDiag{
      "lab.session.hint_exhausted",
      "no level-" + std::to_string( level ) + " hint entry for '" + target + "'" } } );

  HintEvent event;
  event.target = target;
  event.level = level;
  event.seq = nextSeq( session );
  session.lastSeq = event.seq;
  session.hintEvents.push_back( std::move( event ) );

  HintReveal reveal;
  reveal.level = level;
  reveal.text = entry->text;
  reveal.textZh = entry->textZh;
  return LabResult<HintReveal>::success( std::move( reveal ) );
}

} // namespace sicnu::lab
