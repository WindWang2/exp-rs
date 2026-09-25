// src/agent/llm_config_manager.cpp
#include "llm_config_manager.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtGlobal>

#include <memory>

// QtKeychain is already a hard dependency of qgis_core (qgsauthmanager.h),
// which sicnu_agent links publicly; __has_include keeps platforms without it
// building (they fall back to the 0600 QSettings store, see apiKeyStorage()).
#if __has_include( <qt6keychain/keychain.h> )
#include <qt6keychain/keychain.h>
#define SICNU_LLM_HAVE_QTKEYCHAIN 1
#else
#define SICNU_LLM_HAVE_QTKEYCHAIN 0
#endif

namespace sicnu::agent
{

namespace
{
const QString kApiKeyStorageKeychain = QStringLiteral( "keychain" );
const QString kApiKeyStorageSettings = QStringLiteral( "settings" );

#if SICNU_LLM_HAVE_QTKEYCHAIN
/// OS keychain (Windows Credential Manager / macOS Keychain / libsecret or
/// KWallet) through QtKeychain. One entry holds the whole profile->key map
/// so the user sees at most one keychain prompt.
class QtKeychainSecretStore final : public LlmSecretStore
{
  public:
    QString backendName() const override { return kApiKeyStorageKeychain; }

    bool readKeys( QMap<QString, QString> *keys, QString *error ) override
    {
      QString unavailable;
      if ( !backendReachable( &unavailable ) )
        return fail( error, unavailable );
      auto *job = new QKeychain::ReadPasswordJob( serviceName() );
      job->setKey( entryKey() );
      const JobOutcome outcome = runJob( job );
      if ( !outcome.finished )
        return fail( error, QStringLiteral( "keychain read did not complete within %1 ms" ).arg( jobTimeoutMs() ) );
      if ( outcome.error == QKeychain::EntryNotFound )
      {
        keys->clear();
        return true;
      }
      if ( outcome.error != QKeychain::NoError )
        return fail( error, outcome.errorString );
      const QJsonObject obj = QJsonDocument::fromJson( outcome.textData.toUtf8() ).object();
      keys->clear();
      for ( auto it = obj.constBegin(); it != obj.constEnd(); ++it )
        keys->insert( it.key(), it.value().toString() );
      return true;
    }

    bool writeKeys( const QMap<QString, QString> &keys, QString *error ) override
    {
      QString unavailable;
      if ( !backendReachable( &unavailable ) )
        return fail( error, unavailable );
      QJsonObject obj;
      for ( auto it = keys.constBegin(); it != keys.constEnd(); ++it )
        obj.insert( it.key(), it.value() );
      auto *job = new QKeychain::WritePasswordJob( serviceName() );
      job->setKey( entryKey() );
      job->setTextData( QString::fromUtf8( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) ) );
      const JobOutcome outcome = runJob( job );
      if ( !outcome.finished )
        return fail( error, QStringLiteral( "keychain write did not complete within %1 ms" ).arg( jobTimeoutMs() ) );
      if ( outcome.error != QKeychain::NoError )
        return fail( error, outcome.errorString );
      return true;
    }

  private:
    struct JobOutcome
    {
      bool finished = false;
      QKeychain::Error error = QKeychain::OtherError;
      QString errorString;
      QString textData;
    };

    /// Set once a job timed out: the backend is wedged for this process, so
    /// later calls fail fast instead of blocking the caller again.
    bool m_timedOut = false;

    static QString serviceName() { return QStringLiteral( "exp-rs" ); }
    static QString entryKey() { return QStringLiteral( "llm-api-keys" ); }

    static int jobTimeoutMs()
    {
      bool ok = false;
      const int configured = qEnvironmentVariableIntValue( "SICNU_LLM_KEYCHAIN_TIMEOUT_MS", &ok );
      return ( ok && configured > 0 ) ? configured : 5000;
    }

    static bool fail( QString *error, const QString &message )
    {
      if ( error )
        *error = message;
      return false;
    }

    bool backendReachable( QString *why ) const
    {
      if ( !QCoreApplication::instance() )
        return fail( why, QStringLiteral( "no QCoreApplication for keychain access" ) );
      if ( m_timedOut )
        return fail( why, QStringLiteral( "keychain backend timed out earlier in this process" ) );
#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS ) && !defined( Q_OS_ANDROID ) && !defined( Q_OS_IOS )
      // libsecret / KWallet live on the D-Bus session bus. Without one (SSH
      // sessions, headless CI, containers) QtKeychain never reports back, so
      // do not even start a job.
      const bool haveAddress = !qEnvironmentVariableIsEmpty( "DBUS_SESSION_BUS_ADDRESS" );
      const QString runtimeDir = qEnvironmentVariable( "XDG_RUNTIME_DIR" );
      const bool haveSocket = !runtimeDir.isEmpty() && QFile::exists( runtimeDir + QStringLiteral( "/bus" ) );
      if ( !haveAddress && !haveSocket )
        return fail( why, QStringLiteral( "no D-Bus session bus for the secret service" ) );
#endif
      return true;
    }

    /// QtKeychain jobs are asynchronous; the config manager API is not, so
    /// wait in a nested loop — bounded, because some backends (a secret
    /// service that never answers) never emit finished(). The job lives on
    /// the heap with autoDelete: on timeout it is abandoned rather than
    /// destroyed, so a late backend callback still hits a live object, and
    /// the shared outcome keeps the late finished() handler from touching
    /// this stack frame.
    JobOutcome runJob( QKeychain::Job *job )
    {
      auto outcome = std::make_shared<JobOutcome>();
      auto loop = std::make_shared<QEventLoop>();
      job->setAutoDelete( true );
      QObject::connect( job, &QKeychain::Job::finished, job, [outcome, loop]( QKeychain::Job *done ) {
        outcome->finished = true;
        outcome->error = done->error();
        outcome->errorString = done->errorString();
        if ( auto *read = qobject_cast<QKeychain::ReadPasswordJob *>( done ) )
          outcome->textData = read->textData();
        loop->quit();
      } );
      QTimer timer;
      timer.setSingleShot( true );
      QObject::connect( &timer, &QTimer::timeout, loop.get(), &QEventLoop::quit );
      timer.start( jobTimeoutMs() );
      job->start();
      if ( !outcome->finished )
        loop->exec();
      if ( !outcome->finished )
        m_timedOut = true;
      return *outcome;
    }
};
#endif
} // namespace

std::shared_ptr<LlmSecretStore> LlmConfigManager::defaultSecretStore()
{
#if SICNU_LLM_HAVE_QTKEYCHAIN
  // SICNU_LLM_KEYCHAIN=0 opts out explicitly (headless CI, kiosk images
  // without a secret service); keys then use the 0600 QSettings fallback.
  const QString optOut = qEnvironmentVariable( "SICNU_LLM_KEYCHAIN" ).trimmed().toLower();
  if ( optOut == QLatin1String( "0" ) || optOut == QLatin1String( "false" ) || optOut == QLatin1String( "off" ) )
    return nullptr;
  return std::make_shared<QtKeychainSecretStore>();
#else
  return nullptr;
#endif
}

void LlmConfigManager::setSecretStore( std::shared_ptr<LlmSecretStore> store )
{
  m_secretStore = std::move( store );
  m_secretStoreResolved = m_secretStore != nullptr;
  m_loaded = false;
  m_apiKeyStorage.clear();
}

LlmSecretStore *LlmConfigManager::secretStore()
{
  if ( !m_secretStoreResolved )
  {
    m_secretStore = defaultSecretStore();
    m_secretStoreResolved = true;
  }
  return m_secretStore.get();
}

QString LlmConfigManager::apiKeyStorage()
{
  ensureLoaded();
  return m_apiKeyStorage;
}

LlmConfigManager::LlmConfigManager( QObject *parent )
  : QObject( parent )
{
}

LlmConfigManager &LlmConfigManager::instance()
{
  static LlmConfigManager s_instance;
  return s_instance;
}

QList<LlmProviderProfile> LlmConfigManager::presetProfiles()
{
  return instance().getPresetProfiles();
}

QList<LlmProviderProfile> LlmConfigManager::getPresetProfiles() const
{
  QList<LlmProviderProfile> presets;

  LlmProviderProfile deepseek;
  deepseek.id = QStringLiteral( "deepseek" );
  deepseek.name = QStringLiteral( "DeepSeek R1/V3" );
  deepseek.baseUrl = QStringLiteral( "https://api.deepseek.com/v1" );
  deepseek.modelName = QStringLiteral( "deepseek-reasoner" );
  deepseek.temperature = 0.2;
  deepseek.stream = true;
  presets.append( deepseek );

  LlmProviderProfile qwen;
  qwen.id = QStringLiteral( "qwen" );
  qwen.name = QStringLiteral( "Qwen (DashScope)" );
  qwen.baseUrl = QStringLiteral( "https://dashscope.aliyuncs.com/compatible-mode/v1" );
  qwen.modelName = QStringLiteral( "qwen-max" );
  qwen.temperature = 0.2;
  qwen.stream = true;
  presets.append( qwen );

  LlmProviderProfile ollama;
  ollama.id = QStringLiteral( "ollama" );
  ollama.name = QStringLiteral( "Ollama Local (vLLM)" );
  ollama.baseUrl = QStringLiteral( "http://localhost:11434/v1" );
  ollama.modelName = QStringLiteral( "qwen2.5-coder:14b" );
  ollama.temperature = 0.2;
  ollama.stream = true;
  presets.append( ollama );

  LlmProviderProfile openai;
  openai.id = QStringLiteral( "openai" );
  openai.name = QStringLiteral( "OpenAI (Compatible)" );
  openai.baseUrl = QStringLiteral( "https://api.openai.com/v1" );
  openai.modelName = QStringLiteral( "gpt-4o" );
  openai.temperature = 0.2;
  openai.stream = true;
  presets.append( openai );

  LlmProviderProfile stepfun;
  stepfun.id = QStringLiteral( "stepfun" );
  stepfun.name = QStringLiteral( "StepFun" );
  stepfun.baseUrl = QStringLiteral( "https://api.stepfun.com/step_plan/v1" );
  stepfun.modelName = QStringLiteral( "step-5-preview" );
  stepfun.temperature = 0.2;
  stepfun.stream = true;
  presets.append( stepfun );

  return presets;
}

void LlmConfigManager::ensureLoaded()
{
  if ( m_loaded )
    return;

  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_Agent" ) );
  m_activeProfileId = settings.value( QStringLiteral( "activeProfileId" ), QStringLiteral( "deepseek" ) ).toString();
  settings.endGroup();

  settings.beginGroup( QStringLiteral( "AI_AgentProfiles" ) );
  int size = settings.beginReadArray( QStringLiteral( "profiles" ) );
  if ( size == 0 )
  {
    settings.endArray();
    settings.endGroup();
    m_cachedProfiles = getPresetProfiles();
  }
  else
  {
    m_cachedProfiles.clear();
    for ( int i = 0; i < size; ++i )
    {
      settings.setArrayIndex( i );
      LlmProviderProfile p;
      p.id = settings.value( QStringLiteral( "id" ) ).toString();
      p.name = settings.value( QStringLiteral( "name" ) ).toString();
      p.baseUrl = settings.value( QStringLiteral( "baseUrl" ) ).toString();
      p.apiKey = settings.value( QStringLiteral( "apiKey" ) ).toString();
      p.modelName = settings.value( QStringLiteral( "modelName" ) ).toString();
      p.temperature = settings.value( QStringLiteral( "temperature" ), 0.2 ).toDouble();
      p.stream = settings.value( QStringLiteral( "stream" ), true ).toBool();
      m_cachedProfiles.append( p );
    }
    settings.endArray();
    settings.endGroup();
  }

  // Review P1-7: API keys live in the secure store. Keys found there win;
  // plaintext keys still in QSettings (written by older builds) are migrated
  // into the store and the plaintext copy is removed. When no secure store
  // is usable the plaintext value keeps working (documented fallback).
  m_apiKeyStorage = kApiKeyStorageSettings;
  if ( LlmSecretStore *store = secretStore() )
  {
    QMap<QString, QString> stored;
    QString error;
    if ( store->readKeys( &stored, &error ) )
    {
      bool migrated = false;
      for ( auto &p : m_cachedProfiles )
      {
        const auto it = stored.constFind( p.id );
        if ( it != stored.constEnd() )
        {
          p.apiKey = it.value();
        }
        else if ( !p.apiKey.isEmpty() )
        {
          stored.insert( p.id, p.apiKey );
          migrated = true;
        }
      }
      bool storeOk = true;
      if ( migrated )
        storeOk = store->writeKeys( stored, &error );
      if ( storeOk )
      {
        m_apiKeyStorage = store->backendName();
        // Drop any plaintext copy (only rewrites when one exists).
        if ( size > 0 )
        {
          bool plaintextPresent = false;
          settings.beginGroup( QStringLiteral( "AI_AgentProfiles" ) );
          const int n = settings.beginReadArray( QStringLiteral( "profiles" ) );
          for ( int i = 0; i < n && !plaintextPresent; ++i )
          {
            settings.setArrayIndex( i );
            plaintextPresent = settings.contains( QStringLiteral( "apiKey" ) );
          }
          settings.endArray();
          settings.endGroup();
          if ( plaintextPresent )
            writeSettings( m_cachedProfiles, /*includeApiKeys=*/false );
        }
      }
      else
      {
        qWarning( "LlmConfigManager: cannot migrate API keys to %s (%s); keeping QSettings copy",
                  qPrintable( store->backendName() ), qPrintable( error ) );
      }
    }
    else
    {
      qWarning( "LlmConfigManager: %s unavailable (%s); API keys fall back to QSettings",
                qPrintable( store->backendName() ), qPrintable( error ) );
    }
  }

  m_loaded = true;
}

LlmProviderProfile LlmConfigManager::activeProfile()
{
  return instance().getActiveProfile();
}

LlmProviderProfile LlmConfigManager::getActiveProfile()
{
  ensureLoaded();
  for ( const auto &profile : m_cachedProfiles )
  {
    if ( profile.id == m_activeProfileId )
      return profile;
  }
  return getPresetProfiles().first();
}

void LlmConfigManager::setActiveProfile( const LlmProviderProfile &profile )
{
  instance().updateActiveProfile( profile );
}

void LlmConfigManager::updateActiveProfile( const LlmProviderProfile &profile )
{
  ensureLoaded();

  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_Agent" ) );
  settings.setValue( QStringLiteral( "activeProfileId" ), profile.id );
  settings.endGroup();

  m_activeProfileId = profile.id;

  bool found = false;
  for ( auto &p : m_cachedProfiles )
  {
    if ( p.id == profile.id )
    {
      p = profile;
      found = true;
      break;
    }
  }
  if ( !found )
  {
    m_cachedProfiles.append( profile );
  }

  updateProfiles( m_cachedProfiles );
  emit activeProfileChanged( profile );
}

QList<LlmProviderProfile> LlmConfigManager::loadProfiles()
{
  return instance().getProfiles();
}

QList<LlmProviderProfile> LlmConfigManager::getProfiles()
{
  ensureLoaded();
  return m_cachedProfiles;
}

void LlmConfigManager::saveProfiles( const QList<LlmProviderProfile> &profiles )
{
  instance().updateProfiles( profiles );
}

void LlmConfigManager::updateProfiles( const QList<LlmProviderProfile> &profiles )
{
  if ( !m_loaded )
    ensureLoaded();
  m_cachedProfiles = profiles;
  m_loaded = true;

  // Review P1-7: keys go to the secure store; QSettings only keeps the
  // non-secret profile fields. Plaintext is written ONLY as the documented
  // fallback when no secure store is usable.
  bool keysSecured = false;
  if ( LlmSecretStore *store = secretStore() )
  {
    QMap<QString, QString> keys;
    for ( const auto &p : profiles )
    {
      if ( !p.apiKey.isEmpty() )
        keys.insert( p.id, p.apiKey );
    }
    QString error;
    keysSecured = store->writeKeys( keys, &error );
    if ( keysSecured )
      m_apiKeyStorage = store->backendName();
    else
      qWarning( "LlmConfigManager: cannot store API keys in %s (%s); falling back to QSettings",
                qPrintable( store->backendName() ), qPrintable( error ) );
  }
  if ( !keysSecured )
    m_apiKeyStorage = kApiKeyStorageSettings;

  writeSettings( profiles, /*includeApiKeys=*/!keysSecured );
  emit profilesChanged();
}

void LlmConfigManager::writeSettings( const QList<LlmProviderProfile> &profiles, bool includeApiKeys )
{
  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_AgentProfiles" ) );
  // Clear the old array first so stale entries (and stale plaintext keys of
  // removed or migrated profiles) cannot survive a shorter rewrite.
  settings.remove( QStringLiteral( "profiles" ) );
  settings.beginWriteArray( QStringLiteral( "profiles" ), profiles.size() );
  for ( int i = 0; i < profiles.size(); ++i )
  {
    settings.setArrayIndex( i );
    const auto &p = profiles[i];
    settings.setValue( QStringLiteral( "id" ), p.id );
    settings.setValue( QStringLiteral( "name" ), p.name );
    settings.setValue( QStringLiteral( "baseUrl" ), p.baseUrl );
    if ( includeApiKeys )
      settings.setValue( QStringLiteral( "apiKey" ), p.apiKey );
    else
      settings.remove( QStringLiteral( "apiKey" ) );
    settings.setValue( QStringLiteral( "modelName" ), p.modelName );
    settings.setValue( QStringLiteral( "temperature" ), p.temperature );
    settings.setValue( QStringLiteral( "stream" ), p.stream );
  }
  settings.endArray();
  settings.setValue( QStringLiteral( "apiKeyStorage" ),
                     includeApiKeys ? kApiKeyStorageSettings : kApiKeyStorageKeychain );
  settings.endGroup();
  settings.sync();
  // Restrict permissions on the settings file (it holds plaintext keys in
  // the fallback mode; harmless hardening otherwise).
  if ( !settings.fileName().isEmpty() && QFile::exists( settings.fileName() ) )
  {
    QFile::setPermissions( settings.fileName(), QFile::ReadOwner | QFile::WriteOwner );
  }
}

} // namespace sicnu::agent
