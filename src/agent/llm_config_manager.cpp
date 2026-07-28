// src/agent/llm_config_manager.cpp
#include "llm_config_manager.h"

namespace sicnu::agent
{

QList<LlmProviderProfile> LlmConfigManager::presetProfiles()
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

LlmProviderProfile LlmConfigManager::activeProfile()
{
  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_Agent" ) );

  QString activeId = settings.value( QStringLiteral( "activeProfileId" ), QStringLiteral( "deepseek" ) ).toString();
  QList<LlmProviderProfile> profiles = loadProfiles();

  for ( const auto &profile : profiles )
  {
    if ( profile.id == activeId )
    {
      settings.endGroup();
      return profile;
    }
  }

  settings.endGroup();
  return presetProfiles().first();
}

void LlmConfigManager::setActiveProfile( const LlmProviderProfile &profile )
{
  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_Agent" ) );
  settings.setValue( QStringLiteral( "activeProfileId" ), profile.id );

  QList<LlmProviderProfile> profiles = loadProfiles();
  bool found = false;
  for ( auto &p : profiles )
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
    profiles.append( profile );
  }

  settings.endGroup();
  saveProfiles( profiles );
}

QList<LlmProviderProfile> LlmConfigManager::loadProfiles()
{
  QSettings settings;
  settings.beginGroup( QStringLiteral( "AI_AgentProfiles" ) );

  int size = settings.beginReadArray( QStringLiteral( "profiles" ) );
  if ( size == 0 )
  {
    settings.endArray();
    settings.endGroup();
    return presetProfiles();
  }

  QList<LlmProviderProfile> profiles;
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
    profiles.append( p );
  }

  settings.endArray();
  settings.endGroup();
  return profiles;
}

void LlmConfigManager::saveProfiles( const QList<LlmProviderProfile> &profiles )
{
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
}

} // namespace sicnu::agent
