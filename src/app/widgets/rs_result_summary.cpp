// rs_result_summary.cpp — shared structured-result renderer
#include "rs_result_summary.h"

#include <QFileInfo>
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

// Terminal-failure vocabulary an operator result may carry in its own
// "status" member. The summary must not paint a green "processing finished"
// line over a result that says it failed.
bool isFailureStatus( const QString &status )
{
    const QString s = status.trimmed().toLower();
    return s == QLatin1String( "failed" ) || s == QLatin1String( "failure" )
           || s == QLatin1String( "fail" ) || s == QLatin1String( "error" )
           || s == QLatin1String( "aborted" ) || s == QLatin1String( "canceled" )
           || s == QLatin1String( "cancelled" );
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

    m_rawToggle = new QPushButton( tr( "View Raw JSON" ), this );
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
    // Skip the rebuild when nothing changed (progress ticks re-feed the same
    // context before a result exists).
    if ( m_operatorId == operatorId && m_elapsedMs == elapsedMs
         && m_fromCache == fromCache )
        return;
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
        context << QObject::tr( "Elapsed %1 s" ).arg( m_elapsedMs / 1000.0, 0, 'f', 1 );
    if ( m_fromCache )
        context << QObject::tr( "Cache Hit" );
    // Honesty: trust the result's own status field when it carries one. A
    // result that reports failure must not render as a finished success.
    const bool failed = m_result.isMember( "status" )
                        && m_result["status"].isString()
                        && isFailureStatus( QString::fromStdString( m_result["status"].asString() ) );
    m_statusLine->setText( failed
                               ? QObject::tr( "✗ Processing failed%1" )
                                     .arg( context.isEmpty()
                                               ? QString()
                                               : QStringLiteral( " · %1" ).arg( context.join( QStringLiteral( " · " ) ) ) )
                               : QObject::tr( "✓ Processing finished%1" )
                                     .arg( context.isEmpty()
                                               ? QString()
                                               : QStringLiteral( " · %1" ).arg( context.join( QStringLiteral( " · " ) ) ) ) );
    m_statusLine->setProperty( "state", failed ? QStringLiteral( "error" )
                                               : QStringLiteral( "ok" ) );
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
            metricLines << QObject::tr( "... (see the raw JSON for the rest)" );
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
            // Also drop the text: an empty-but-hidden label must not resurface
            // with the previous document's warning woven into a later one.
            m_warnings->clear();
        }
    }
    else
    {
        m_warnings->hide();
        m_warnings->clear();
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
        int missing = 0;
        for ( const QString &p : paths )
        {
            if ( p.isEmpty() )
                continue;
            // An operator can report an output path it never wrote (or the
            // file was moved/deleted afterwards). Listing it as a clickable
            // artifact sends the shell into a load that cannot succeed —
            // mark it missing and refuse to emit it.
            const bool exists = QFileInfo::exists( p );
            QListWidgetItem *item = new QListWidgetItem(
                exists ? p : QObject::tr( "%1 — file not found" ).arg( p ), m_artifacts );
            if ( exists )
            {
                item->setData( Qt::UserRole, p );
                item->setToolTip( QObject::tr( "Double-click to load into the main view" ) );
            }
            else
            {
                ++missing;
                // No UserRole: openPathRequested can never fire for it.
                item->setFlags( item->flags() & ~Qt::ItemIsEnabled );
                item->setToolTip( QObject::tr( "This output file was not produced (missing on disk)" ) );
            }
            m_artifacts->addItem( item );
        }
        m_artifacts->show();
        if ( missing > 0 && missing == m_artifacts->count() )
        {
            // Nothing on disk: say so instead of leaving a clean-looking
            // card with a dead artifact row. Appended so real operator
            // warnings are never clobbered (isVisible() is false while the
            // parent panel is still hidden, so test the text, not the flag).
            const QString note = QObject::tr(
                "No output artifact exists on disk — the run reported paths that were never written." );
            m_warnings->setText( m_warnings->text().isEmpty()
                                     ? QObject::tr( "△ %1" ).arg( note )
                                     : m_warnings->text() + QLatin1Char( '\n' ) + QObject::tr( "△ %1" ).arg( note ) );
            m_warnings->show();
        }
    }
    else
    {
        m_artifacts->hide();
    }

    m_rawJson->setPlainText( QString::fromStdString(
        Json::StyledWriter().write( m_result ) ) );
    m_rawToggle->show();
}
