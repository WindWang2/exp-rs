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
  setWindowTitle( QStringLiteral( "AI Copilot 模型配置 (LLM Settings)" ) );
  resize( 500, 320 );

  auto *mainLayout = new QVBoxLayout( this );
  auto *formLayout = new QFormLayout();

  m_providerCombo = new QComboBox( this );
  m_baseUrlEdit = new QLineEdit( this );
  m_apiKeyEdit = new QLineEdit( this );
  m_apiKeyEdit->setEchoMode( QLineEdit::Password );
  m_modelNameEdit = new QLineEdit( this );
  m_tempSpin = new QDoubleSpinBox( this );
  m_tempSpin->setRange( 0.0, 1.0 );
  m_tempSpin->setSingleStep( 0.1 );
  m_tempSpin->setValue( 0.2 );

  formLayout->addRow( QStringLiteral( "供应商 (Provider):" ), m_providerCombo );
  formLayout->addRow( QStringLiteral( "Base URL:" ), m_baseUrlEdit );
  formLayout->addRow( QStringLiteral( "API Key:" ), m_apiKeyEdit );
  formLayout->addRow( QStringLiteral( "模型名称 (Model Name):" ), m_modelNameEdit );
  formLayout->addRow( QStringLiteral( "采样温度 (Temperature):" ), m_tempSpin );

  mainLayout->addLayout( formLayout );

  auto *testLayout = new QHBoxLayout();
  m_testBtn = new QPushButton( QStringLiteral( "测试网络连通性" ), this );
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
  m_statusLabel->setText( QStringLiteral( "正在测试连接..." ) );
  m_statusLabel->setStyleSheet( QStringLiteral( "color: blue;" ) );

  if ( !m_testClient )
  {
    m_testClient = new LlmStreamingClient( this );
    connect( m_testClient, &LlmStreamingClient::contentTokenReceived, this, [this]( const QString & ) {
      m_statusLabel->setText( QStringLiteral( "连接成功！(Connection OK)" ) );
      m_statusLabel->setStyleSheet( QStringLiteral( "color: green;" ) );
    } );
    connect( m_testClient, &LlmStreamingClient::errorOccurred, this, [this]( const QString &err ) {
      m_statusLabel->setText( QString( "连接失败: %1" ).arg( err ) );
      m_statusLabel->setStyleSheet( QStringLiteral( "color: red;" ) );
    } );
  }

  LlmProviderProfile prof = selectedProfile();
  m_testClient->setProfile( prof );

  QJsonArray messages;
  QJsonObject msg;
  msg[QStringLiteral( "role" )] = QStringLiteral( "user" );
  msg[QStringLiteral( "content" )] = QStringLiteral( "ping" );
  messages.append( msg );

  m_testClient->sendChatCompletion( messages, false );
}

} // namespace sicnu::agent
