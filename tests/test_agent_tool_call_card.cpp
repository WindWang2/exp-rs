#include "agent/widgets/agent_tool_call_card.h"

#include <QTest>
#include <QFile>

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
        { QFINDTESTDATA( "../resources/styles.qss" ), QColor( QStringLiteral( "#CF222E" ) ) },
        { QFINDTESTDATA( "../resources/styles-dark.qss" ), QColor( QStringLiteral( "#FF7B86" ) ) },
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
