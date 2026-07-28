// src/agent/llm_config_manager.h
#pragma once

#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>

namespace sicnu::agent
{

struct LlmProviderProfile
{
  QString id;          // e.g. "deepseek", "qwen", "ollama", "openai", "custom"
  QString name;        // e.g. "DeepSeek R1/V3"
  QString baseUrl;     // e.g. "https://api.deepseek.com/v1"
  QString apiKey;      // API Secret Key
  QString modelName;   // e.g. "deepseek-reasoner", "deepseek-chat", "qwen-max"
  double temperature = 0.2;
  bool stream = true;

  bool operator==( const LlmProviderProfile &other ) const
  {
    return id == other.id && name == other.name && baseUrl == other.baseUrl &&
           apiKey == other.apiKey && modelName == other.modelName;
  }
};

class LlmConfigManager
{
  public:
    static LlmProviderProfile activeProfile();
    static void setActiveProfile( const LlmProviderProfile &profile );
    static QList<LlmProviderProfile> presetProfiles();
    static QList<LlmProviderProfile> loadProfiles();
    static void saveProfiles( const QList<LlmProviderProfile> &profiles );
};

} // namespace sicnu::agent
