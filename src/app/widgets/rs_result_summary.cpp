// rs_result_summary.cpp — shared structured-result renderer
#include "rs_result_summary.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// Keys that carry provenance rather than metric meaning; they are shown
// verbatim in the raw JSON but not in the metrics block.
bool isNonMetricKey( const std::string &key )
{
    static const char *const kKeys[] = {
        "output", "outputs", "expression", "mode", "method", "transform",
        "bands", "width", "height", "status", "message", "warning", "warnings",
        "operator", "algorithm", "input", "inputs", "pan", "ms", "crs",
    };
    for ( const char *k : kKeys )
    {
        if ( key == k )
            return true;
    }
    return false;
}

} // namespace

RsResultSummary::RsResultSummary( QWidget *parent )
  : QWidget( parent )
{
    setObjectName( QStringLiteral( "rsResultSummary" ) );

    auto *root = new QVBoxLayout( this );
    root->setContentsMargins( 0, 0, 0, 0 );
    root->setSpacing( 6 );

    m_statusLine = new QLabel( this );
    m_statusLine->setObjectName( QStringLiteral( "rsResultStatus" ) );
    m_statusLine->setWordWrap( true );
    m_statusLine->hide();
    root->addWidget( m_statusLine );

    m_metrics = new QLabel( this );
    m_metrics->setObjectName( QStringLiteral( "rsResultMetrics" ) );
    m_metrics->setWordWrap( true );
    m_metrics->hide();
    root->addWidget( m_metrics );

    m_warnings = new QLabel( this );
    m_warnings->setObjectName( QStringLiteral( "rsResultWarnings" ) );
    m_warnings->setWordWrap( true );
    m_warnings->hide();
    root->addWidget( m_warnings );

    m_artifacts = new QListWidget( this );
    m_artifacts->setObjectName( QStringLiteral( "rsResultArtifacts" ) );
    m_artifacts->setFrameShape( QFrame::NoFrame );
    m_artifacts->setMaximumHeight( 96 );
    m_artifacts->setUniformItemSizes( true );
    m_artifacts->setAlternatingRowColors( false );
    m_artifacts->hide();
    root->addWidget( m_artifacts );

    connect( m_artifacts, &QListWidget::itemDoubleClicked, this,
             [this]( QListWidgetItem *item ) {
                 if ( item && !item->data( Qt::UserRole ).toString().isEmpty() )
                     emit openPathRequested( item->data( Qt::UserRole ).toString() );
             } );

    m_rawToggle = new QPushButton( tr( "查看原始 JSON" ), this );
    m_rawToggle->setObjectName( QStringLiteral( "rsResultRawToggle" ) );
    m_rawToggle->setCheckable( true );
    m_rawToggle->hide();
    m_rawJson = new QPlainTextEdit( this );
    m_rawJson->setObjectName( QStringLiteral( "rsResultRawJson" ) );
    m_rawJson->setReadOnly( true );
    m_rawJson->setMaximumHeight( 120 );
    QFont mono = m_rawJson->font();
    mono.setFamily( QStringLiteral( "IBM Plex Mono" ) );
    mono.setStyleHint( QFont::Monospace );
    m_rawJson->setFont( mono );
    m_rawJson->hide();
    root->addWidget( m_rawToggle );
    root->addWidget( m_rawJson );
    connect( m_rawToggle, &QPushButton::toggled, m_rawJson, &QWidget::setVisible );
}

void RsResultSummary::setContext( const QString &operatorId, qint64 elapsedMs,
                                  bool fromCache )
{
    m_operatorId = operatorId;
    m_elapsedMs = elapsedMs;
    m_fromCache = fromCache;
    if ( m_hasResult )
        rebuildUi();
}

void RsResultSummary::clear()
{
    m_result = Json::Value();
    m_hasResult = false;
    m_operatorId.clear();
    m_elapsedMs = -1;
    m_fromCache = false;
    m_statusLine->hide();
    m_metrics->hide();
    m_warnings->hide();
    m_artifacts->clear();
    m_artifacts->hide();
    m_rawToggle->hide();
    m_rawToggle->setChecked( false );
    m_rawJson->hide();
    m_rawJson->clear();
}

void RsResultSummary::setResult( const Json::Value &result )
{
    m_result = result;
    m_hasResult = result.isObject() && !result.empty();
    rebuildUi();
}

QString RsResultSummary::prettyKey( const std::string &key )
{
    // Common operator metric keys already carry readable names; just pretty
    // camelCase ones (accuracyOA → accuracy OA).
    QString s = QString::fromStdString( key );
    QString out;
    for ( int i = 0; i < s.size(); ++i )
    {
        const QChar c = s.at( i );
        if ( c.isUpper() && i > 0 && s.at( i - 1 ).isLower() )
            out += QLatin1Char( ' ' );
        out += c;
    }
    return out;
}

void RsResultSummary::rebuildUi()
{
    if ( !m_hasResult )
    {
        clear();
        return;
    }

    QStringList context;
    if ( !m_operatorId.isEmpty() )
        context << m_operatorId;
    if ( m_elapsedMs >= 0 )
        context << QObject::tr( "耗时 %1 s" ).arg( m_elapsedMs / 1000.0, 0, 'f', 1 );
    if ( m_fromCache )
        context << QObject::tr( "缓存命中" );
    m_statusLine->setText( QObject::tr( "✓ 处理完成%1" )
                               .arg( context.isEmpty()
                                         ? QString()
                                         : QStringLiteral( " · %1" ).arg( context.join( QStringLiteral( " · " ) ) ) ) );
    m_statusLine->setProperty( "state", QStringLiteral( "ok" ) );
    m_statusLine->style()->unpolish( m_statusLine );
    m_statusLine->style()->polish( m_statusLine );
    m_statusLine->show();

    // Key metrics: scalar members of the result object, bounded (a result is
    // a summary document, not a table dump).
    QStringList metricLines;
    constexpr int kMaxMetrics = 16;
    for ( const std::string &key : m_result.getMemberNames() )
    {
        if ( metricLines.size() >= kMaxMetrics )
        {
            metricLines << QObject::tr( "…（其余见原始 JSON）" );
            break;
        }
        if ( isNonMetricKey( key ) )
            continue;
        const Json::Value &v = m_result[key];
        if ( v.isConvertibleTo( Json::stringValue ) && !v.isObject() && !v.isArray() )
        {
            QString value = QString::fromStdString( v.asString() );
            metricLines << QStringLiteral( "%1: %2" ).arg( prettyKey( key ), value );
        }
    }
    if ( !metricLines.isEmpty() )
    {
        m_metrics->setText( metricLines.join( QStringLiteral( " · " ) ) );
        m_metrics->show();
    }
    else
    {
        m_metrics->hide();
    }

    // Warnings block (array of strings or array of {message}).
    const Json::Value &warnings = m_result["warnings"];
    if ( warnings.isArray() && !warnings.empty() )
    {
        QStringList lines;
        for ( const Json::Value &w : warnings )
        {
            if ( w.isString() )
                lines << QString::fromStdString( w.asString() );
            else if ( w.isObject() && w.isMember( "message" ) && w["message"].isString() )
                lines << QString::fromStdString( w["message"].asString() );
        }
        if ( !lines.isEmpty() )
        {
            m_warnings->setText( QStringLiteral( "△ %1" ).arg( lines.join( QStringLiteral( "\n△ " ) ) ) );
            m_warnings->show();
        }
        else
        {
            m_warnings->hide();
        }
    }
    else
    {
        m_warnings->hide();
    }

    // Output artifacts: "output" string and/or "outputs" array.
    m_artifacts->clear();
    QStringList paths;
    if ( m_result.isMember( "output" ) && m_result["output"].isString() )
        paths << QString::fromStdString( m_result["output"].asString() );
    if ( m_result.isMember( "outputs" ) && m_result["outputs"].isArray() )
    {
        for ( const Json::Value &o : m_result["outputs"] )
        {
            if ( o.isString() )
                paths << QString::fromStdString( o.asString() );
            else if ( o.isObject() && o.isMember( "path" ) && o["path"].isString() )
                paths << QString::fromStdString( o["path"].asString() );
        }
    }
    if ( !paths.isEmpty() )
    {
        for ( const QString &p : paths )
        {
            if ( p.isEmpty() )
                continue;
            QListWidgetItem *item = new QListWidgetItem( p, m_artifacts );
            item->setData( Qt::UserRole, p );
            item->setToolTip( QObject::tr( "双击加载到主图" ) );
            m_artifacts->addItem( item );
        }
        m_artifacts->show();
    }
    else
    {
        m_artifacts->hide();
    }

    m_rawJson->setPlainText( QString::fromStdString(
        Json::StyledWriter().write( m_result ) ) );
    m_rawToggle->show();
}
