// src/agent/harness/context_ledger.cpp
#include "context_ledger.h"

#include <QDateTime>
#include <QFileInfo>
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
                                       const std::string &verificationStatus,
                                       const std::string &planFingerprint )
{
  QMutexLocker locker( &mMutex );
  // Re-binding an existing run updates in place (verification status changes
  // as the run completes).
  for ( Json::ArrayIndex i = 0; i < mPlanBindings.size(); ++i )
  {
    if ( mPlanBindings[i].get( "run_id", "" ).asString() == runId )
    {
      mPlanBindings[i]["verification_status"] = verificationStatus;
      if ( !planFingerprint.empty() )
        mPlanBindings[i]["plan_fingerprint"] = planFingerprint;
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
  if ( !planFingerprint.empty() )
    binding["plan_fingerprint"] = planFingerprint;
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

void ContextLedger::recordAssetContext( const QString &path, const Json::Value &entity,
                                        const QString &observedKey, const Json::Value &summary )
{
  if ( path.isEmpty() )
    return;
  QMutexLocker locker( &mMutex );
  const std::string pathUtf8 = path.toStdString();
  for ( Json::ArrayIndex i = 0; i < mAssetContexts.size(); ++i )
  {
    if ( mAssetContexts[i].get( "path", "" ).asString() == pathUtf8 )
    {
      mAssetContexts[i]["entity"] = entity;
      mAssetContexts[i]["observed_key"] = observedKey.toStdString();
      mAssetContexts[i]["summary"] = summary;
      mAssetContexts[i]["recorded_at"] = nowIso().toStdString();
      return;
    }
  }
  Json::Value record( Json::objectValue );
  record["path"] = pathUtf8;
  record["entity"] = entity;
  record["observed_key"] = observedKey.toStdString();
  // Explicit key kind (adversarial review P3): a path containing "|f" must
  // never be misread as a stat key.
  record["observed_kind"] =
    ( observedKey.contains( QStringLiteral( "|r" ) ) &&
              !observedKey.contains( QStringLiteral( "|f" ) ) )
      ? "revision"
      : "stat";
  record["summary"] = summary;
  record["recorded_at"] = nowIso().toStdString();
  mAssetContexts.append( record );
  while ( mAssetContexts.size() > kMaxUnderstandingEntries )
    mAssetContexts.removeIndex( 0, nullptr );
}

namespace {

/// True when the recorded observation key no longer matches the file on
/// disk. Keys are "<path>|r<revision>" for registered assets (staleness
/// authority is the catalog; only a vanished file can be detected here) and
/// "<path>|f<size>|<mtimeMs>" for unregistered files (stat identity).
bool assetContextStale( const std::string &path, const std::string &observedKey,
                        const std::string &kind )
{
  const QString qpath = QString::fromStdString( path );
  const QString key = QString::fromStdString( observedKey );
  const QFileInfo info( qpath );
  if ( !info.exists() )
    return true;
  // Stat keys fold (size, mtime): a rewritten file with a different size or
  // timestamp stales the record. Same-size, same-granularity rewrites are
  // NOT detectable (documented limitation — the catalog revision is the
  // authority for registered assets).
  if ( kind == "stat" )
  {
    const QString expected =
      qpath + QStringLiteral( "|f" ) + QString::number( info.size() ) + QStringLiteral( "|" ) +
      QString::number( info.lastModified().toMSecsSinceEpoch() );
    return expected != key;
  }
  return false;
}

} // namespace

Json::Value ContextLedger::assetContexts() const
{
  Json::Value records;
  {
    // Copy under the lock; the staleness stats run outside it so a slow
    // network filesystem cannot stall every ledger consumer (review P2).
    QMutexLocker locker( &mMutex );
    records = mAssetContexts;
  }
  Json::Value out( Json::arrayValue );
  for ( const Json::Value &record : records )
  {
    Json::Value entry = record;
    const std::string kind = record.get( "observed_kind", "stat" ).asString();
    entry["stale"] = assetContextStale( record.get( "path", "" ).asString(),
                                        record.get( "observed_key", "" ).asString(), kind );
    out.append( entry );
  }
  return out;
}

void ContextLedger::recordModelContract( const std::string &modelId, const Json::Value &contract )
{
  if ( modelId.empty() || !contract.isObject() )
    return;
  QMutexLocker locker( &mMutex );
  for ( Json::ArrayIndex i = 0; i < mModelContracts.size(); ++i )
  {
    if ( mModelContracts[i].get( "model_id", "" ).asString() == modelId )
    {
      mModelContracts[i] = contract;
      mModelContracts[i]["model_id"] = modelId;
      mModelContracts[i]["recorded_at"] = nowIso().toStdString();
      return;
    }
  }
  Json::Value record = contract;
  record["model_id"] = modelId;
  record["recorded_at"] = nowIso().toStdString();
  mModelContracts.append( record );
  constexpr int kMaxModelContracts = 8;
  while ( mModelContracts.size() > kMaxModelContracts )
    mModelContracts.removeIndex( 0, nullptr );
}

Json::Value ContextLedger::modelContracts() const
{
  QMutexLocker locker( &mMutex );
  return mModelContracts;
}

} // namespace sicnu::agent::harness
