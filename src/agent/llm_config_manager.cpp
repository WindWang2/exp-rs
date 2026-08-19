// src/agent/llm_config_manager.cpp
#include "llm_config_manager.h"
#include <QFile>

namespace sicnu::agent
{

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
  m_cachedProfiles = profiles;
  m_loaded = true;

  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_AgentProfiles" ) );

  settings.beginWriteArray( QStringLiteral( "profiles" ), profiles.size() );
  for ( int i = 0; i < profiles.size(); ++i )
  {
    settings.setArrayIndex( i );
    const auto &p = profiles[i];
    settings.setValue( QStringLiteral( "id" ), p.id );
    settings.setValue( QStringLiteral( "name" ), p.name );
    settings.setValue( QStringLiteral( "baseUrl" ), p.baseUrl );
    settings.setValue( QStringLiteral( "apiKey" ), p.apiKey );
    settings.setValue( QStringLiteral( "modelName" ), p.modelName );
    settings.setValue( QStringLiteral( "temperature" ), p.temperature );
    settings.setValue( QStringLiteral( "stream" ), p.stream );
  }
  settings.endArray();
  settings.endGroup();
  settings.sync();
  // Restrict permissions on the ini file that stores plaintext API keys
  // (best-effort hardening; future migration to OS keyring tracked separately).
  if (!settings.fileName().isEmpty())
  {
    QFile::setPermissions(settings.fileName(), QFile::ReadOwner | QFile::WriteOwner);
  }

  emit profilesChanged();
}

} // namespace sicnu::agent
