// src/agent/llm_settings_dialog.h
#pragma once

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "llm_config_manager.h"
#include "llm_streaming_client.h"

namespace sicnu::agent
{

class LlmSettingsDialog : public QDialog
{
  Q_OBJECT

  public:
    explicit LlmSettingsDialog( QWidget *parent = nullptr );
    ~LlmSettingsDialog() override = default;

    LlmProviderProfile selectedProfile() const;

  private slots:
    void onProviderIndexChanged( int index );
    void onTestConnectionClicked();

  private:
    QComboBox *m_providerCombo = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_modelNameEdit = nullptr;
    QDoubleSpinBox *m_tempSpin = nullptr;
    QPushButton *m_testBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    LlmStreamingClient *m_testClient = nullptr;
    QList<LlmProviderProfile> m_profiles;
};

} // namespace sicnu::agent
