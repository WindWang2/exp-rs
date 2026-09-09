// src/agent/harness/context_ledger.cpp
#include "context_ledger.h"

#include <QDateTime>
#include <QMutexLocker>

namespace sicnu::agent::harness {

namespace {

constexpr int kMaxPlanBindings = 8;
constexpr int kMaxDecisions = 20;
constexpr int kMaxUnderstandingEntries = 32;

QString nowIso()
{
  return QDateTime::currentDateTimeUtc().toString( Qt::ISODate );
}


} // namespace

ContextLedger &ContextLedger::instance()
{
  static ContextLedger ledger;
  return ledger;
}

void ContextLedger::recordPlanBinding( const std::string &runId, const std::string &planId,
                                       const std::string &goal, const std::string &intent,
                                       const std::string &verificationStatus )
{
  QMutexLocker locker( &mMutex );
  // Re-binding an existing run updates in place (verification status changes
  // as the run completes).
  for ( Json::ArrayIndex i = 0; i < mPlanBindings.size(); ++i )
  {
    if ( mPlanBindings[i].get( "run_id", "" ).asString() == runId )
    {
      mPlanBindings[i]["verification_status"] = verificationStatus;
      mPlanBindings[i]["updated_at"] = nowIso().toStdString();
      return;
    }
  }
  Json::Value binding( Json::objectValue );
  binding["run_id"] = runId;
  binding["plan_id"] = planId;
  binding["goal"] = goal;
  binding["intent"] = intent;
  binding["verification_status"] = verificationStatus;
  binding["bound_at"] = nowIso().toStdString();
  mPlanBindings.append( binding );
  while ( mPlanBindings.size() > kMaxPlanBindings )
    mPlanBindings.removeIndex( 0, nullptr );
}

Json::Value ContextLedger::planBindings() const
{
  QMutexLocker locker( &mMutex );
  return mPlanBindings;
}

std::string ContextLedger::recordDecision( const std::string &kind,
                                           const std::string &subject,
                                           const std::string &status,
                                           const std::string &note,
                                           const Json::Value &candidates )
{
  QMutexLocker locker( &mMutex );
  Json::Value decision( Json::objectValue );
  decision["id"] = "decision-" + std::to_string( mNextDecisionId++ );
  decision["kind"] = kind;
  decision["subject"] = subject;
  decision["status"] = status;
  decision["note"] = note;
  if ( candidates.isArray() )
    decision["candidates"] = candidates;
  decision["recorded_at"] = nowIso().toStdString();
  mDecisions.append( decision );
  while ( mDecisions.size() > kMaxDecisions )
    mDecisions.removeIndex( 0, nullptr );
  return decision["id"].asString();
}

bool ContextLedger::resolveDecision( const std::string &decisionId, const std::string &chosen )
{
  QMutexLocker locker( &mMutex );
  for ( Json::ArrayIndex i = 0; i < mDecisions.size(); ++i )
  {
    if ( mDecisions[i].get( "id", "" ).asString() == decisionId )
    {
      mDecisions[i]["status"] = "resolved";
      if ( !chosen.empty() )
        mDecisions[i]["chosen"] = chosen;
      mDecisions[i]["resolved_at"] = nowIso().toStdString();
      return true;
    }
  }
  return false;
}

Json::Value ContextLedger::decisions() const
{
  QMutexLocker locker( &mMutex );
  // Unresolved first (stable), so the agent sees open work before history.
  Json::Value unresolved( Json::arrayValue );
  Json::Value resolved( Json::arrayValue );
  for ( const Json::Value &decision : mDecisions )
  {
    if ( decision.get( "status", "" ).asString() == "resolved" )
      resolved.append( decision );
    else
      unresolved.append( decision );
  }
  for ( const Json::Value &decision : resolved )
    unresolved.append( decision );
  return unresolved;
}

void ContextLedger::cacheUnderstanding( const QString &keyToken, long long,
                                        const Json::Value &understanding )
{
  QMutexLocker locker( &mMutex );
  const std::string key = keyToken.toStdString();
  for ( Json::ArrayIndex i = 0; i < mUnderstandingKeys.size(); ++i )
  {
    if ( mUnderstandingKeys[i].asString() == key )
    {
      mUnderstandingDocs[i] = understanding;
      return;
    }
  }
  mUnderstandingKeys.append( key );
  mUnderstandingDocs.append( understanding );
  while ( mUnderstandingKeys.size() > kMaxUnderstandingEntries )
  {
    mUnderstandingKeys.removeIndex( 0, nullptr );
    mUnderstandingDocs.removeIndex( 0, nullptr );
  }
}

Json::Value ContextLedger::cachedUnderstanding( const QString &keyToken, long long ) const
{
  QMutexLocker locker( &mMutex );
  const std::string key = keyToken.toStdString();
  for ( Json::ArrayIndex i = 0; i < mUnderstandingKeys.size(); ++i )
  {
    if ( mUnderstandingKeys[i].asString() == key )
      return mUnderstandingDocs[i];
  }
  return Json::Value();
}

} // namespace sicnu::agent::harness
