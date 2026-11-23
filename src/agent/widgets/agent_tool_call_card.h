#pragma once

#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace sicnu::agent
{

/// Read-only presentation of host events. Has no approval, retry or execution
/// callback: expanding diagnostics must never resubmit a native operation.
class AgentToolCallCard final : public QFrame
{
  public:
    AgentToolCallCard( const QString &toolName, const QString &initialTitle,
                       const QString &initialStatus, const QString &detailsText, QWidget *parent = nullptr )
      : QFrame( parent ), m_toolName( toolName )
    {
      setObjectName( QStringLiteral( "toolCallCard" ) );
      setProperty( "toolName", toolName );
      setFrameShape( QFrame::StyledPanel );
      auto *layout = new QVBoxLayout( this );
      layout->setContentsMargins( 10, 8, 10, 8 );
      layout->setSpacing( 6 );
      m_title = new QLabel( initialTitle, this );
      m_title->setObjectName( QStringLiteral( "ToolCallCardTitle" ) );
      m_title->setTextFormat( Qt::RichText );
      m_title->setWordWrap( true );
      m_title->setProperty( "rsTone", "info" );
      layout->addWidget( m_title );
      m_summary = new QLabel( initialStatus, this );
      m_summary->setObjectName( QStringLiteral( "ToolCallCardSummary" ) );
      m_summary->setTextFormat( Qt::PlainText );
      m_summary->setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );
      m_summary->setWordWrap( true );
      layout->addWidget( m_summary );
      auto *toggle = new QPushButton( detailsText, this );
      toggle->setObjectName( QStringLiteral( "agentToolDetailsToggle" ) );
      toggle->setCheckable( true );
      toggle->setFlat( true );
      toggle->setSizePolicy( QSizePolicy::Maximum, QSizePolicy::Fixed );
      layout->addWidget( toggle );
      auto *area = new QScrollArea( this );
      area->setObjectName( QStringLiteral( "agentToolDetailsArea" ) );
      area->viewport()->setProperty( "rsSurface", "panel" );
      area->setWidgetResizable( true );
      area->setFrameShape( QFrame::NoFrame );
      area->setMaximumHeight( 180 );
      area->hide();
      m_details = new QLabel( initialStatus, area );
      m_details->setObjectName( QStringLiteral( "ToolCallCardDetails" ) );
      // QScrollArea::setWidget enables autoFillBackground. Theme the actual
      // content as well as its viewport, not only the outer scroll frame.
      m_details->setProperty( "rsSurface", "panel" );
      m_details->setTextFormat( Qt::PlainText );
      m_details->setTextInteractionFlags( Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard );
      m_details->setWordWrap( true );
      m_details->setAlignment( Qt::AlignTop | Qt::AlignLeft );
      area->setWidget( m_details );
      layout->addWidget( area );
      connect( toggle, &QPushButton::toggled, area, &QWidget::setVisible );
    }

    void updatePresentation( const QString &statusText, const QString &state, const QString &detail )
    {
      if ( !statusText.isEmpty() )
        m_title->setText( QStringLiteral( "<b>%1</b> [%2]" ).arg( m_toolName.toHtmlEscaped(), statusText.toHtmlEscaped() ) );
      // Never infer semantics from a translated status label.
      const char *tone = state == QLatin1String( "failed" ) ? "error"
        : state == QLatin1String( "succeeded" ) ? "ok"
        : state == QLatin1String( "approval" ) || state == QLatin1String( "rejected" )
          || state == QLatin1String( "preparing_approval" ) || state == QLatin1String( "verifying_approval" )
          || state == QLatin1String( "cancelling" ) || state == QLatin1String( "canceling" )
          || state == QLatin1String( "cancelled" ) ? "warn" : "info";
      m_title->setProperty( "rsTone", tone );
      m_title->style()->unpolish( m_title );
      m_title->style()->polish( m_title );
      m_title->update();
      m_details->setText( detail );
      const auto result = QJsonDocument::fromJson( detail.toUtf8() ).object();
      // Short errors remain visible without exposing the entire protocol JSON.
      // Full, literal diagnostics remain available behind the disclosure.
      const QString summary = result.isEmpty() ? detail : result.value( QStringLiteral( "errorMessage" ) ).toString();
      m_summary->setText( summary.left( 512 ) );
      m_summary->setVisible( !summary.isEmpty() );
    }

  private:
    QString m_toolName;
    QLabel *m_title = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_details = nullptr;
};

} // namespace sicnu::agent
