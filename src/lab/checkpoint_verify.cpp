/***************************************************************************
  lab/checkpoint_verify.cpp — see checkpoint_verify.h
***************************************************************************/

#include "checkpoint_verify.h"

#include <algorithm>
#include <sstream>

namespace sicnu::lab
{
namespace
{

std::string formatDouble( double value )
{
  std::ostringstream out;
  out << value;
  return out.str();
}

bool operatorMatches( const CheckpointCheck &check, const std::string &operatorId )
{
  if ( !check.operatorId.empty() )
    return check.operatorId == operatorId;
  for ( const std::string &candidate : check.operatorIdAny )
  {
    if ( candidate == operatorId )
      return true;
  }
  return false;
}

bool paramsSubsetMatches( const std::map<std::string, std::string> &expected,
                          const std::map<std::string, std::string> &recorded )
{
  for ( const auto &param : expected )
  {
    const auto it = recorded.find( param.first );
    if ( it == recorded.end() || it->second != param.second )
      return false;
  }
  return true;
}

const CheckpointResult *latestResultFor( const LabSession &session, const std::string &checkpointId )
{
  const CheckpointResult *latest = nullptr;
  for ( const CheckpointResult &result : session.checkpointResults )
  {
    if ( result.checkpointId != checkpointId )
      continue;
    if ( !latest || result.seq > latest->seq )
      latest = &result;
  }
  return latest;
}

/// Pure projection: advisory stage status from the latest checkpoint results.
StageStatus deriveStageStatus( const Stage &stage, const LabSession &session )
{
  bool allPassed = true;
  bool blocked = false;
  for ( const Checkpoint &checkpoint : stage.checkpoints )
  {
    const CheckpointResult *latest = latestResultFor( session, checkpoint.id );
    if ( !latest )
    {
      allPassed = false;
      continue;
    }
    if ( checkpoint.advance == AdvancePolicy::Gate && latest->verdict != Verdict::Pass )
      blocked = true;
    if ( latest->verdict != Verdict::Pass )
      allPassed = false;
  }
  if ( blocked )
    return StageStatus::BlockedAdvance;
  if ( allPassed && !stage.checkpoints.empty() )
    return StageStatus::Advanced;
  return StageStatus::Active;
}

} // namespace

LabResult<CheckpointResult> verifyCheckpoint( LabSession &session, const LabRuntimePlan &plan,
                                              const std::string &checkpointId, FileProbe &probe,
                                              long long readBudget )
{
  if ( session.state != SessionState::Active )
    return LabResult<CheckpointResult>::failure(
      { LabDiag{ "lab.session.bad_transition", "cannot verify on a non-active session" } } );

  const Stage *stage = nullptr;
  const Checkpoint *checkpoint = nullptr;
  for ( const Stage &candidateStage : plan.stages )
  {
    for ( const Checkpoint &candidate : candidateStage.checkpoints )
    {
      if ( candidate.id == checkpointId )
      {
        stage = &candidateStage;
        checkpoint = &candidate;
        break;
      }
    }
    if ( checkpoint )
      break;
  }
  if ( !checkpoint || !stage )
    return LabResult<CheckpointResult>::failure(
      { LabDiag{ "lab.session.unknown_checkpoint", "unknown checkpoint '" + checkpointId + "'" } } );

  int attempt = 1;
  for ( const CheckpointResult &result : session.checkpointResults )
  {
    if ( result.checkpointId == checkpointId )
      ++attempt;
  }
  const bool overBudget = checkpoint->attemptsAllowed > 0 && attempt > checkpoint->attemptsAllowed;

  CheckpointResult outcome;
  outcome.checkpointId = checkpointId;
  outcome.attempt = attempt;
  bool sawUnverifiable = false;
  bool sawFail = overBudget; // over-budget cannot pass; evidence appended below

  int index = 0;
  for ( const CheckpointCheck &check : checkpoint->checks )
  {
    CheckEvidence evidence;
    evidence.checkIndex = index++;
    evidence.ok = true;
    evidence.expected = "satisfied";

    switch ( check.kind )
    {
      case CheckKind::ArtifactPresent:
      {
        evidence.expected = check.path;
        if ( !probe.exists( check.path ) )
        {
          evidence.ok = false;
          evidence.observed = "missing";
          sawFail = true;
          break;
        }
        const long long size = probe.fileSize( check.path );
        if ( check.minBytes > 0 && size < check.minBytes )
        {
          evidence.ok = false;
          evidence.observed = std::to_string( size ) + " bytes";
          evidence.expected = "at least " + std::to_string( check.minBytes ) + " bytes";
          sawFail = true;
          break;
        }
        if ( !check.sha256.empty() )
        {
          std::string bytes;
          if ( !probe.readFile( check.path, bytes, readBudget ) )
          {
            evidence.ok = false;
            evidence.observed = "unreadable_or_over_budget";
            evidence.expected = "digest of at most " + std::to_string( readBudget ) + " bytes";
            sawUnverifiable = true;
            break;
          }
          const std::string digest = specFingerprint( bytes );
          if ( digest != check.sha256 )
          {
            evidence.ok = false;
            evidence.observed = digest;
            evidence.expected = check.sha256;
            sawFail = true;
            break;
          }
          evidence.observed = digest;
          evidence.expected = digest;
        }
        else
        {
          evidence.observed = std::to_string( size ) + " bytes";
          evidence.expected = "present";
        }
        break;
      }
      case CheckKind::OperatorInvoked:
      {
        bool invoked = false;
        for ( const ToolChoice &choice : session.toolChoices )
        {
          if ( choice.stageId != stage->id )
            continue;
          if ( !operatorMatches( check, choice.operatorId ) )
            continue;
          if ( !paramsSubsetMatches( check.paramsSubset, choice.paramsSubset ) )
            continue;
          invoked = true;
          break;
        }
        evidence.ok = invoked;
        evidence.observed = invoked ? "invocation recorded" : "no recorded invocation";
        evidence.expected = !check.operatorId.empty() ? check.operatorId
                                                      : "any of " + std::to_string( check.operatorIdAny.size() ) +
                                                          " operators";
        if ( !invoked )
          sawFail = true;
        break;
      }
      case CheckKind::QuestionAnswered:
      {
        const QuestionAnswer *latest = nullptr;
        for ( const QuestionAnswer &answer : session.questionAnswers )
        {
          if ( answer.questionId != check.questionId )
            continue;
          if ( !latest || answer.seq > latest->seq )
            latest = &answer;
        }
        if ( !latest )
        {
          evidence.ok = false;
          evidence.observed = "unanswered";
          evidence.expected = check.questionId;
          sawFail = true;
          break;
        }
        const Question *question = nullptr;
        for ( const Question &candidate : plan.questions )
        {
          if ( candidate.id == check.questionId )
            question = &candidate;
        }
        if ( question && question->hasExpectedNumeric )
        {
          if ( !latest->hasNumeric )
          {
            evidence.ok = false;
            evidence.observed = "non-numeric answer";
            evidence.expected = "numeric in [" + formatDouble( question->expectedMin ) + ", " +
                                formatDouble( question->expectedMax ) + "]";
            sawFail = true;
            break;
          }
          const bool inRange = latest->numeric >= question->expectedMin &&
                               latest->numeric <= question->expectedMax;
          evidence.ok = inRange;
          evidence.observed = formatDouble( latest->numeric );
          evidence.expected = "[" + formatDouble( question->expectedMin ) + ", " +
                              formatDouble( question->expectedMax ) + "]";
          if ( !inRange )
            sawFail = true;
        }
        else
        {
          evidence.observed = "answered";
          evidence.expected = check.questionId;
        }
        break;
      }
    }
    outcome.evidence.push_back( std::move( evidence ) );
  }

  if ( overBudget )
  {
    CheckEvidence budget;
    budget.checkIndex = -1;
    budget.ok = false;
    budget.observed = "attempt_over_budget";
    budget.expected = "attempts_allowed=" + std::to_string( checkpoint->attemptsAllowed );
    outcome.evidence.push_back( std::move( budget ) );
  }

  outcome.verdict = overBudget            ? Verdict::Unverifiable
                    : sawUnverifiable     ? Verdict::Unverifiable
                    : sawFail             ? Verdict::Fail
                                          : Verdict::Pass;
  outcome.seq = nextSeq( session );
  session.lastSeq = outcome.seq;
  session.checkpointResults.push_back( std::move( outcome ) );

  // Recompute the owning stage's advisory status from the appended result.
  for ( SessionStage &sessionStage : session.stages )
  {
    if ( sessionStage.stageId == stage->id )
      sessionStage.status = deriveStageStatus( *stage, session );
  }

  return LabResult<CheckpointResult>::success( session.checkpointResults.back() );
}

} // namespace sicnu::lab
