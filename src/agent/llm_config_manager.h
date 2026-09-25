// src/agent/llm_config_manager.h
#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>

#include "sicnu_agent_export.h"

#include <memory>

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

/// Secure storage for LLM API keys (review P1-7). Keys never belong in
/// QSettings (registry / ~/.config, shared lab machines): the default store
/// is the OS keychain through QtKeychain. Stores map profile id -> API key.
class SICNU_AGENT_EXPORT LlmSecretStore
{
  public:
    virtual ~LlmSecretStore() = default;
    /// Human-readable backend name for diagnostics ("keychain", ...).
    virtual QString backendName() const = 0;
    /// Reads every stored key. A store with no entry yet returns true with
    /// an empty map; false means the backend is unusable right now.
    virtual bool readKeys( QMap<QString, QString> *keys, QString *error ) = 0;
    /// Replaces the stored map. False means nothing was persisted.
    virtual bool writeKeys( const QMap<QString, QString> &keys, QString *error ) = 0;
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

    /// Where API keys are persisted: "keychain" when the secure store took
    /// them, "settings" when it was unavailable and the plaintext (0600)
    /// QSettings fallback was used.
    QString apiKeyStorage();

    /// Replaces the secret store (tests / embedders). nullptr restores the
    /// platform default (QtKeychain when compiled in, else no secure store).
    /// Forces a reload on next access.
    void setSecretStore( std::shared_ptr<LlmSecretStore> store );
    /// The platform default store; nullptr when QtKeychain is not available.
    static std::shared_ptr<LlmSecretStore> defaultSecretStore();

  signals:
    void activeProfileChanged( const sicnu::agent::LlmProviderProfile &profile );
    void profilesChanged();

  private:
    void ensureLoaded();
    void writeSettings( const QList<LlmProviderProfile> &profiles, bool includeApiKeys );
    LlmSecretStore *secretStore();

    QList<LlmProviderProfile> m_cachedProfiles;
    QString m_activeProfileId;
    bool m_loaded = false;
    std::shared_ptr<LlmSecretStore> m_secretStore;
    bool m_secretStoreResolved = false;
    QString m_apiKeyStorage;
};

} // namespace sicnu::agent
