#include "agent/widgets/agent_tool_call_card.h"
#include "agent/widgets/agent_prompt_edit.h"
#include "app/design_tokens.h"

#include <QTest>
#include <QFile>
#include <QImage>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>

using sicnu::agent::AgentToolCallCard;

class AgentToolCallCardTest : public QObject
{
    Q_OBJECT
  private slots:
    void detailsStartCollapsedAndStayBoundedWithoutChangingPayload()
    {
      AgentToolCallCard card( QStringLiteral( "python:run_script" ), QStringLiteral( "Preparing" ),
                              QStringLiteral( "Submitted" ), QStringLiteral( "Details" ) );
      card.show();
      auto *toggle = card.findChild<QPushButton *>( QStringLiteral( "agentToolDetailsToggle" ) );
      auto *area = card.findChild<QScrollArea *>( QStringLiteral( "agentToolDetailsArea" ) );
      auto *details = card.findChild<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) );
      QVERIFY( toggle && area && details );
      QVERIFY( !toggle->isChecked() );
      QVERIFY( area->isHidden() );
      const QString payload = QStringLiteral( "{\"output\":\"/tmp/result.tif\",\"diagnostic\":\"" )
                            + QString( 20000, QLatin1Char( 'x' ) ) + QStringLiteral( "\"}" );
      card.updatePresentation( QStringLiteral( "Succeeded" ), QStringLiteral( "succeeded" ), payload );
      QCOMPARE( details->text(), payload );
      QTest::mouseClick( toggle, Qt::LeftButton );
      QCoreApplication::processEvents();
      QVERIFY( area->isVisible() );
      QVERIFY( area->height() <= 180 );
      // Disclosure state survives host updates; toggling cannot resubmit work.
      card.updatePresentation( QStringLiteral( "Succeeded" ), QStringLiteral( "succeeded" ), payload );
      QVERIFY( toggle->isChecked() );
      QTest::mouseClick( toggle, Qt::LeftButton );
      QVERIFY( area->isHidden() );
      QCOMPARE( details->text(), payload );
    }

    void canonicalStateNotTranslatedTextDeterminesTone()
    {
      AgentToolCallCard card( QStringLiteral( "rs:ndvi" ), QStringLiteral( "Preparing" ),
                              QStringLiteral( "Submitted" ), QStringLiteral( "Details" ) );
      auto *title = card.findChild<QLabel *>( QStringLiteral( "ToolCallCardTitle" ) );
      QVERIFY( title );
      const QList<QPair<QString, QString>> states = {
        { QStringLiteral( "running" ), QStringLiteral( "info" ) },
        { QStringLiteral( "awaiting_commit" ), QStringLiteral( "info" ) },
        { QStringLiteral( "approval" ), QStringLiteral( "warn" ) },
        { QStringLiteral( "rejected" ), QStringLiteral( "warn" ) },
        { QStringLiteral( "cancelled" ), QStringLiteral( "warn" ) },
        { QStringLiteral( "succeeded" ), QStringLiteral( "ok" ) },
        { QStringLiteral( "failed" ), QStringLiteral( "error" ) },
      };
      for ( const auto &entry : states )
      {
        card.updatePresentation( QStringLiteral( "任意译文" ), entry.first, QString() );
        QCOMPARE( title->property( "rsTone" ).toString(), entry.second );
        QVERIFY( !title->text().contains( QStringLiteral( "color:" ) ) );
      }
    }

    void bothProductThemesRepolishCanonicalStates()
    {
      const QList<QPair<QString, QColor>> themes = {
        { QFINDTESTDATA( "../resources/styles.qss" ), SicnuUi::Tokens::Light::err },
        { QFINDTESTDATA( "../resources/styles-dark.qss" ), SicnuUi::Tokens::Dark::err },
      };
      for ( const auto &theme : themes )
      {
        QFile file( theme.first );
        QVERIFY( file.open( QIODevice::ReadOnly ) );
        AgentToolCallCard card( QStringLiteral( "rs:ndvi" ), QStringLiteral( "Preparing" ),
                                QStringLiteral( "Submitted" ), QStringLiteral( "详情" ) );
        card.setStyleSheet( QString::fromUtf8( file.readAll() ) );
        card.show();
        card.updatePresentation( QStringLiteral( "失败" ), QStringLiteral( "failed" ), QStringLiteral( "diagnostic" ) );
        auto *title = card.findChild<QLabel *>( QStringLiteral( "ToolCallCardTitle" ) );
        QVERIFY( title );
        QCOMPARE( title->palette().color( QPalette::WindowText ), theme.second );
        card.updatePresentation( QStringLiteral( "成功" ), QStringLiteral( "succeeded" ), QString() );
        QVERIFY( title->palette().color( QPalette::WindowText ) != theme.second );
      }
    }

    void bothThemesRenderUnselectedDiagnosticsAndInputWithReadableInk()
    {
      const QString originalStyle = qApp->styleSheet();
      const auto restoreStyle = qScopeGuard( [&] { qApp->setStyleSheet( originalStyle ); } );
      const QList<QPair<QString, QColor>> themes = {
        { QFINDTESTDATA( "../resources/styles.qss" ), QColor( "#1C2430" ) },
        { QFINDTESTDATA( "../resources/styles-dark.qss" ), QColor( "#E8ECF1" ) },
      };
      const auto luminance = []( const QColor &color ) {
        const auto linear = []( double value ) { return value <= 0.04045 ? value / 12.92 : std::pow( ( value + 0.055 ) / 1.055, 2.4 ); };
        return 0.2126 * linear( color.redF() ) + 0.7152 * linear( color.greenF() ) + 0.0722 * linear( color.blueF() );
      };
      for ( const auto &theme : themes )
      {
        QFile file( theme.first );
        QVERIFY( file.open( QIODevice::ReadOnly ) );
        // Match the product/preview application-level QSS, not just palettes.
        qApp->setStyleSheet( QString::fromUtf8( file.readAll() ) );
        AgentToolCallCard card( "host:fixture", "Preparing", "Submitted", "Details" );
        card.resize( 460, 300 );
        card.updatePresentation( "Succeeded", "succeeded", "{\"result\":\"Unselected diagnostics 0123456789\"}" );
        card.show();
        auto *toggle = card.findChild<QPushButton *>( "agentToolDetailsToggle" );
        auto *area = card.findChild<QScrollArea *>( "agentToolDetailsArea" );
        auto *details = card.findChild<QLabel *>( "ToolCallCardDetails" );
        QVERIFY( toggle && area && details );
        QTest::mouseClick( toggle, Qt::LeftButton );
        sicnu::agent::AgentPromptEdit input;
        input.resize( 460, 100 );
        input.setPlainText( "Unselected prompt 0123456789" );
        input.show();
        QCoreApplication::processEvents();
        QVERIFY( !details->hasSelectedText() );
        QVERIFY( !input.textCursor().hasSelection() );
        for ( QWidget *viewport : { area->viewport(), input.viewport() } )
        {
          // Paint actual child/viewport content into memory. A foreground-only
          // palette assertion missed the white autofilled QLabel background.
          QImage pixels( viewport->size(), QImage::Format_RGB32 );
          pixels.fill( Qt::magenta );
          viewport->render( &pixels );
          const QColor background = pixels.pixelColor( pixels.width() - 3, pixels.height() - 3 );
          const double ink = luminance( theme.second );
          const double paper = luminance( background );
          QVERIFY2( ( std::max( ink, paper ) + 0.05 ) / ( std::min( ink, paper ) + 0.05 ) >= 4.5,
                    qPrintable( theme.first + " background=" + background.name() ) );
          int inkPixels = 0;
          for ( int y = 0; y < pixels.height(); ++y )
            for ( int x = 0; x < pixels.width(); ++x )
            {
              const QColor actual = pixels.pixelColor( x, y );
              if ( std::abs( actual.red() - theme.second.red() ) < 12
                   && std::abs( actual.green() - theme.second.green() ) < 12
                   && std::abs( actual.blue() - theme.second.blue() ) < 12 ) ++inkPixels;
            }
          QVERIFY2( inkPixels > 20, "Unselected text must actually paint readable glyphs" );
        }
      }
    }

    void multilineErrorStaysCompactUntilDetailsAreExpanded()
    {
      AgentToolCallCard card( "python:run_script", "Preparing", "Submitted", "Details" );
      auto *summary = card.findChild<QLabel *>( "ToolCallCardSummary" );
      auto *details = card.findChild<QLabel *>( "ToolCallCardDetails" );
      auto *area = card.findChild<QScrollArea *>( "agentToolDetailsArea" );
      auto *toggle = card.findChild<QPushButton *>( "agentToolDetailsToggle" );
      QVERIFY( summary && details && area && toggle );
      const QString firstLine = QStringLiteral( "Failed to open raster" );
      card.updatePresentation( "Failed", "failed", firstLine );
      const int compactHeight = card.heightForWidth( 320 );
      QVERIFY( compactHeight > 0 );
      for ( const auto &separator : { QStringLiteral( "\n" ), QStringLiteral( "\r\n" ), QStringLiteral( "\r" ),
                                      QString( QChar::LineSeparator ), QString( QChar::ParagraphSeparator ) } )
      {
        const QString error = separator + firstLine + ( separator + QStringLiteral( "frame" ) ).repeated( 85 );
        const QString payload = QString::fromUtf8( QJsonDocument( QJsonObject{
          { QStringLiteral( "errorMessage" ), error } } ).toJson() );
        card.updatePresentation( "Failed", "failed", payload );
        QVERIFY( area->isHidden() );
        QCOMPARE( summary->text(), firstLine );
        QCOMPARE( card.heightForWidth( 320 ), compactHeight );
        toggle->click();
        QVERIFY( !area->isHidden() );
        QCOMPARE( details->text(), payload );
        toggle->click();
      }
    }

    void errorsAreVisibleAndLiteralWhileFullDiagnosticsRemainAvailable()
    {
      AgentToolCallCard card( QStringLiteral( "<b>tool</b>" ), QStringLiteral( "Preparing" ),
                              QStringLiteral( "Submitted" ), QStringLiteral( "详情" ) );
      const QString error = QStringLiteral( "<img src='https://invalid.example/track'>" ) + QString( 1000, QLatin1Char( 'e' ) );
      const QString payload = QString::fromUtf8( QJsonDocument( QJsonObject{
        { QStringLiteral( "status" ), QStringLiteral( "error" ) },
        { QStringLiteral( "errorMessage" ), error } } ).toJson() );
      card.updatePresentation( QStringLiteral( "<b>Failed</b>" ), QStringLiteral( "failed" ), payload );
      auto *summary = card.findChild<QLabel *>( QStringLiteral( "ToolCallCardSummary" ) );
      auto *details = card.findChild<QLabel *>( QStringLiteral( "ToolCallCardDetails" ) );
      auto *title = card.findChild<QLabel *>( QStringLiteral( "ToolCallCardTitle" ) );
      QVERIFY( summary && details && title );
      QVERIFY( !summary->isHidden() );
      QCOMPARE( summary->textFormat(), Qt::PlainText );
      QCOMPARE( details->textFormat(), Qt::PlainText );
      QCOMPARE( summary->text(), error.left( 512 ) );
      QCOMPARE( details->text(), payload );
      QVERIFY( title->text().contains( QStringLiteral( "&lt;b&gt;tool&lt;/b&gt;" ) ) );
      QVERIFY( title->text().contains( QStringLiteral( "&lt;b&gt;Failed&lt;/b&gt;" ) ) );
    }
};

QTEST_MAIN( AgentToolCallCardTest )
#include "test_agent_tool_call_card.moc"
