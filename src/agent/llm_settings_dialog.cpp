// src/agent/llm_settings_dialog.cpp
#include "llm_settings_dialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

namespace sicnu::agent
{

LlmSettingsDialog::LlmSettingsDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "AI Copilot Model & Service Settings" ) );
  resize( 500, 320 );

  auto *mainLayout = new QVBoxLayout( this );
  auto *formLayout = new QFormLayout();

  m_providerCombo = new QComboBox( this );
  m_providerCombo->setToolTip( tr( "Pick a preset or custom LLM service provider" ) );
  m_baseUrlEdit = new QLineEdit( this );
  m_baseUrlEdit->setPlaceholderText( QStringLiteral( "https://api.openai.com/v1" ) );
  m_baseUrlEdit->setToolTip( tr( "LLM API endpoint root path" ) );
  m_apiKeyEdit = new QLineEdit( this );
  m_apiKeyEdit->setEchoMode( QLineEdit::Password );
  m_apiKeyEdit->setPlaceholderText( tr( "Enter the API key (leave empty for local models)" ) );
  m_apiKeyEdit->setToolTip( tr( "Authentication token" ) );
  m_modelNameEdit = new QLineEdit( this );
  m_modelNameEdit->setPlaceholderText( QStringLiteral( "gpt-4o, qwen-plus, etc." ) );
  m_modelNameEdit->setToolTip( tr( "Model name to request" ) );
  m_tempSpin = new QDoubleSpinBox( this );
  m_tempSpin->setRange( 0.0, 1.0 );
  m_tempSpin->setSingleStep( 0.1 );
  m_tempSpin->setValue( 0.2 );
  m_tempSpin->setToolTip( tr( "Sampling temperature (0.0-1.0)" ) );

  formLayout->addRow( tr( "Provider:" ), m_providerCombo );
  formLayout->addRow( tr( "Base URL:" ), m_baseUrlEdit );
  formLayout->addRow( tr( "API key:" ), m_apiKeyEdit );
  formLayout->addRow( tr( "Model name:" ), m_modelNameEdit );
  formLayout->addRow( tr( "Temperature:" ), m_tempSpin );

  mainLayout->addLayout( formLayout );

  auto *testLayout = new QHBoxLayout();
  m_testBtn = new QPushButton( tr( "Test connectivity" ), this );
  m_testBtn->setToolTip( tr( "Test connectivity to the LLM service" ) );
  m_statusLabel = new QLabel( this );
  testLayout->addWidget( m_testBtn );
  testLayout->addWidget( m_statusLabel, 1 );

  mainLayout->addLayout( testLayout );

  m_buttonBox = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  mainLayout->addWidget( m_buttonBox );

  m_profiles = LlmConfigManager::loadProfiles();
  for ( const auto &profile : m_profiles )
  {
    m_providerCombo->addItem( profile.name, profile.id );
  }

  LlmProviderProfile active = LlmConfigManager::activeProfile();
  int activeIndex = 0;
  for ( int i = 0; i < m_profiles.size(); ++i )
  {
    if ( m_profiles[i].id == active.id )
    {
      activeIndex = i;
      break;
    }
  }
  m_providerCombo->setCurrentIndex( activeIndex );
  onProviderIndexChanged( activeIndex );

  connect( m_providerCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, &LlmSettingsDialog::onProviderIndexChanged );
  connect( m_testBtn, &QPushButton::clicked, this, &LlmSettingsDialog::onTestConnectionClicked );
  connect( m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject );
}

void LlmSettingsDialog::onProviderIndexChanged( int index )
{
  if ( index < 0 || index >= m_profiles.size() )
    return;

  const auto &profile = m_profiles[index];
  m_baseUrlEdit->setText( profile.baseUrl );
  m_apiKeyEdit->setText( profile.apiKey );
  m_modelNameEdit->setText( profile.modelName );
  m_tempSpin->setValue( profile.temperature );
}

LlmProviderProfile LlmSettingsDialog::selectedProfile() const
{
  LlmProviderProfile profile;
  int idx = m_providerCombo->currentIndex();
  if ( idx >= 0 && idx < m_profiles.size() )
  {
    profile.id = m_profiles[idx].id;
  }
  else
  {
    profile.id = QStringLiteral( "custom" );
  }

  profile.name = m_providerCombo->currentText();
  profile.baseUrl = m_baseUrlEdit->text().trimmed();
  profile.apiKey = m_apiKeyEdit->text().trimmed();
  profile.modelName = m_modelNameEdit->text().trimmed();
  profile.temperature = m_tempSpin->value();
  profile.stream = true;

  return profile;
}

void LlmSettingsDialog::onTestConnectionClicked()
{
  m_statusLabel->setText( tr( "Testing connection..." ) );
  m_statusLabel->setStyleSheet( QStringLiteral( "color: #0284c7; font-weight: 500;" ) );

  if ( !m_testClient )
  {
    m_testClient = new LlmStreamingClient( this );
    connect( m_testClient, &LlmStreamingClient::contentTokenReceived, this, [this]( const QString & ) {
      m_statusLabel->setText( tr( "Connection successful!" ) );
      m_statusLabel->setStyleSheet( QStringLiteral( "color: #16a34a; font-weight: 600;" ) );
    } );
    connect( m_testClient, &LlmStreamingClient::errorOccurred, this, [this]( const QString &err ) {
      m_statusLabel->setText( tr( "Connection failed: %1" ).arg( err ) );
      m_statusLabel->setStyleSheet( QStringLiteral( "color: #dc2626;" ) );
    } );
    connect( m_testClient, &LlmStreamingClient::finished, this, [this]() {
      if (m_statusLabel->text().contains(tr("Testing"))) {
        m_statusLabel->setText( tr( "Connection successful, but no content returned" ) );
        m_statusLabel->setStyleSheet( QStringLiteral( "color: #d97706;" ) );
      }
    } );
  }

  LlmProviderProfile prof = selectedProfile();
  m_testClient->setProfile( prof );

  QJsonArray messages;
  QJsonObject msg;
  msg[QStringLiteral( "role" )] = QStringLiteral( "user" );
  msg[QStringLiteral( "content" )] = QStringLiteral( "ping" );
  messages.append( msg );

  m_testClient->sendChatCompletion( messages );
}

} // namespace sicnu::agent
