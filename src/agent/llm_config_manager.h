// src/agent/llm_config_manager.h
#pragma once

#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>

#include "sicnu_agent_export.h"

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

class SICNU_AGENT_EXPORT LlmConfigManager : public QObject
{
    Q_OBJECT
  public:
    explicit LlmConfigManager( QObject *parent = nullptr );

    static LlmConfigManager &instance();

    LlmProviderProfile getActiveProfile();
    void updateActiveProfile( const LlmProviderProfile &profile );
    QList<LlmProviderProfile> getPresetProfiles() const;
    QList<LlmProviderProfile> getProfiles();
    void updateProfiles( const QList<LlmProviderProfile> &profiles );

    // Backwards-compatible static facade methods
    static LlmProviderProfile activeProfile();
    static void setActiveProfile( const LlmProviderProfile &profile );
    static QList<LlmProviderProfile> presetProfiles();
    static QList<LlmProviderProfile> loadProfiles();
    static void saveProfiles( const QList<LlmProviderProfile> &profiles );

  signals:
    void activeProfileChanged( const sicnu::agent::LlmProviderProfile &profile );
    void profilesChanged();

  private:
    void ensureLoaded();

    QList<LlmProviderProfile> m_cachedProfiles;
    QString m_activeProfileId;
    bool m_loaded = false;
};

} // namespace sicnu::agent
