/***************************************************************************
 * help_center_dialog.cpp — Help Center implementation
 ***************************************************************************/
#include "app/help/help_center_dialog.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{

/// Goal-defined information architecture. Categories not in this list are
/// appended (stable order: first-seen by sorted id) so new content surfaces
/// without code changes.
QStringList canonicalCategories()
{
    return {
        QObject::tr( "快速上手" ),
        QObject::tr( "数据与工程" ),
        QObject::tr( "光学遥感" ),
        QObject::tr( "SAR" ),
        QObject::tr( "时序分析" ),
        QObject::tr( "分类" ),
        QObject::tr( "变化检测" ),
        QObject::tr( "地形" ),
        QObject::tr( "工作区" ),
        QObject::tr( "制图" ),
        QObject::tr( "智能体 / Pi" ),
        QObject::tr( "错误与诊断" ),
        QObject::tr( "快捷键" ),
        QObject::tr( "处理框架" ),
    };
}

} // namespace

HelpCenterDialog::HelpCenterDialog( QWidget *parent )
    : QDialog( parent )
{
    setWindowTitle( dialogTitle() );
    resize( 960, 640 );

    auto *root = new QVBoxLayout( this );

    m_searchBox = new QLineEdit( this );
    m_searchBox->setPlaceholderText( tr( "搜索帮助主题（中文 / English / Help ID）…" ) );
    m_searchBox->setClearButtonEnabled( true );
    root->addWidget( m_searchBox );

    auto *splitter = new QSplitter( Qt::Horizontal, this );
    root->addWidget( splitter, 1 );

    m_tree = new QTreeWidget( splitter );
    m_tree->setHeaderLabel( tr( "主题" ) );
    splitter->addWidget( m_tree );

    auto *rightPane = new QWidget( splitter );
    auto *rightLayout = new QVBoxLayout( rightPane );
    rightLayout->setContentsMargins( 0, 0, 0, 0 );
    m_countLabel = new QLabel( rightPane );
    rightLayout->addWidget( m_countLabel );
    m_browser = new QTextBrowser( rightPane );
    m_browser->setOpenLinks( false );
    rightLayout->addWidget( m_browser, 1 );
    splitter->addWidget( rightPane );
    splitter->setStretchFactor( 1, 1 );

    buildCategories();

    connect( m_searchBox, &QLineEdit::textChanged, this, &HelpCenterDialog::runSearch );
    connect( m_tree, &QTreeWidget::itemActivated, this, [this]( QTreeWidgetItem *item, int ) {
        const QString id = item->data( 0, Qt::UserRole ).toString();
        if ( !id.isEmpty() )
            showTopic( id );
    } );
    connect( m_tree, &QTreeWidget::itemClicked, this, [this]( QTreeWidgetItem *item, int ) {
        const QString id = item->data( 0, Qt::UserRole ).toString();
        if ( !id.isEmpty() )
            showTopic( id );
    } );
    connect( m_browser, &QTextBrowser::anchorClicked, this, [this]( const QUrl &url ) {
        if ( url.scheme() == QLatin1String( "helpid" ) ) {
            // NB: ids travel in the PATH, never the host — QUrl lowercases
            // hosts (RFC 3986 normalization) and Help IDs are case-sensitive.
            QString id = url.path();
            while ( id.startsWith( u'/' ) )
                id.remove( 0, 1 );
            if ( !id.isEmpty() )
                showTopic( id );
        }
    } );

    m_index.build( sicnu::help::globalHelpRegistry().all() );
    renderHome();
}

void HelpCenterDialog::buildCategories()
{
    m_tree->clear();
    m_categoryItems.clear();

    QStringList categories = canonicalCategories();
    for ( const sicnu::help::HelpDescriptor *d : sicnu::help::globalHelpRegistry().all() ) {
        const QString category = d->category;
        if ( !category.isEmpty() && !categories.contains( category ) )
            categories << category;
    }
    for ( const QString &category : categories ) {
        auto *item = new QTreeWidgetItem( m_tree, QStringList{ category } );
        item->setFlags( item->flags() & ~Qt::ItemIsSelectable );
        m_categoryItems.insert( category, item );
    }
    for ( const sicnu::help::HelpDescriptor *d : sicnu::help::globalHelpRegistry().all() ) {
        QTreeWidgetItem *category = m_categoryItems.value( d->category );
        if ( !category )
            continue;
        auto *item = new QTreeWidgetItem(
            category, QStringList{ QStringLiteral( "%1 — %2" ).arg( d->title, d->summary.left( 40 ) ) } );
        item->setToolTip( 0, d->id );
        item->setData( 0, Qt::UserRole, d->id );
    }
    m_tree->expandAll();
}

void HelpCenterDialog::navigateTo( const QString &helpId )
{
    m_searchBox->blockSignals( true );
    m_searchBox->clear();
    m_searchBox->blockSignals( false );
    buildCategories();
    showTopic( helpId );
}

void HelpCenterDialog::showTopic( const QString &helpId )
{
    const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find( helpId );
    if ( !d ) {
        m_browser->setHtml( QStringLiteral( "<h1>%1</h1><p>%2</p>" )
                                .arg( escapeHtml( tr( "未找到主题" ) ),
                                      escapeHtml( helpId ) ) );
        return;
    }

    // highlight the topic in the tree
    QTreeWidgetItem *category = m_categoryItems.value( d->category );
    if ( category ) {
        for ( int i = 0; i < category->childCount(); ++i ) {
            if ( category->child( i )->data( 0, Qt::UserRole ).toString() == d->id ) {
                m_tree->setCurrentItem( category->child( i ) );
                break;
            }
        }
    }

    m_browser->setHtml( renderTopicHtml( *d ) );
    m_countLabel->setText( d->id );
}

void HelpCenterDialog::runSearch( const QString &query )
{
    if ( query.trimmed().isEmpty() ) {
        buildCategories(); // browse mode: every topic under its category
        m_countLabel->clear();
        return;
    }

    // search mode: strip browse-mode children, keep only matched topics
    for ( QTreeWidgetItem *category : m_categoryItems ) {
        const QList<QTreeWidgetItem *> children = category->takeChildren();
        for ( QTreeWidgetItem *child : children )
            delete child;
    }

    QElapsedTimer timer;
    timer.start();
    const QVector<sicnu::help::SearchHit> hits = m_index.search( query, 30 );
    const qint64 elapsedMs = timer.elapsed();
    for ( const sicnu::help::SearchHit &hit : hits ) {
        const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find( hit.id );
        if ( !d )
            continue;
        QTreeWidgetItem *category = m_categoryItems.value( d->category );
        if ( !category )
            continue;
        auto *item = new QTreeWidgetItem( category, QStringList{ QStringLiteral( "%1 — %2" ).arg( d->title, d->summary.left( 40 ) ) } );
        item->setToolTip( 0, d->id );
        item->setData( 0, Qt::UserRole, d->id );
    }
    m_tree->expandAll();
    m_countLabel->setText( tr( "%1 个结果（%2 ms）" ).arg( hits.size() ).arg( elapsedMs ) );
}

void HelpCenterDialog::renderHome()
{
    QString html = QStringLiteral( "<h1>%1</h1>" ).arg( escapeHtml( tr( "帮助中心" ) ) );
    html += QStringLiteral( "<p>%1</p>" ).arg(
        escapeHtml( tr( "在上方搜索，或从左侧目录浏览。聚焦任意界面元素后按 F1 可直接跳到相关主题。" ) ) );
    const int topics = sicnu::help::globalHelpRegistry().count();
    html += QStringLiteral( "<p>%1</p>" ).arg( escapeHtml( tr( "共收录 %1 个主题。" ).arg( topics ) ) );
    m_browser->setHtml( html );
}

QString HelpCenterDialog::escapeHtml( const QString &text ) const
{
    QString out = text;
    out.replace( u'&', QStringLiteral( "&amp;" ) );
    out.replace( u'<', QStringLiteral( "&lt;" ) );
    out.replace( u'>', QStringLiteral( "&gt;" ) );
    return out;
}

QTreeWidgetItem *HelpCenterDialog::categoryItem( const QString &category )
{
    return m_categoryItems.value( category );
}

QString HelpCenterDialog::renderTopicHtml( const sicnu::help::HelpDescriptor &d ) const
{
    QString html;
    html += QStringLiteral( "<h1>%1</h1>" ).arg( escapeHtml( d.title ) );
    html += QStringLiteral( "<p><code>%1</code></p>" ).arg( escapeHtml( d.id ) );
    if ( !d.summary.isEmpty() )
        html += QStringLiteral( "<p>%1</p>" ).arg( escapeHtml( d.summary ) );

    const auto section = [ this, &html ]( const QString &title, const QStringList &items ) {
        if ( items.isEmpty() )
            return;
        html += QStringLiteral( "<h3>%1</h3><ul>" ).arg( escapeHtml( title ) );
        for ( const QString &item : items )
            html += QStringLiteral( "<li>%1</li>" ).arg( escapeHtml( item ) );
        html += QStringLiteral( "</ul>" );
    };
    const auto paragraph = [ this, &html ]( const QString &title, const QString &body ) {
        if ( body.isEmpty() )
            return;
        html += QStringLiteral( "<h3>%1</h3><p>%2</p>" ).arg( escapeHtml( title ), escapeHtml( body ) );
    };

    if ( d.command.has_value() ) {
        paragraph( tr( "用途" ), d.command->purpose );
        section( tr( "前提" ), d.command->prerequisites );
        paragraph( tr( "建议下一步" ), d.command->suggestedNextAction );
    }
    if ( d.parameter.has_value() ) {
        const sicnu::help::ParameterKnowledge &p = *d.parameter;
        paragraph( tr( "含义" ), p.meaning );
        paragraph( tr( "单位" ), p.unit );
        paragraph( tr( "推荐值" ), p.recommended );
        paragraph( tr( "权衡" ), p.tradeOff );
        paragraph( tr( "性能" ), p.performanceNote );
        section( tr( "注意事项" ), p.warnings );
    }
    if ( d.algorithm.has_value() ) {
        const sicnu::help::AlgorithmPage &a = *d.algorithm;
        paragraph( tr( "原理" ), a.whatItDoes );
        paragraph( tr( "适用" ), a.whenToUse );
        section( tr( "输入要求" ), a.inputs );
        section( tr( "输出" ), a.outputs );
        section( tr( "假设" ), a.assumptions );
        paragraph( tr( "数值域" ), a.unitsDomain );
        section( tr( "局限" ), a.limitations );
        section( tr( "典型失败模式" ), a.failureModes );
    }
    if ( d.guidance.has_value() ) {
        paragraph( tr( "下一步" ), d.guidance->body );
        if ( !d.guidance->actionCommandIds.isEmpty() ) {
            section( tr( "推荐操作" ), d.guidance->actionCommandIds );
        }
    }
    if ( d.diagnostic.has_value() ) {
        const sicnu::help::DiagnosticInfo &info = *d.diagnostic;
        html += QStringLiteral( "<p><code>%1</code> · %2</p>" )
                    .arg( escapeHtml( info.originCode ),
                          escapeHtml( info.originFamily ) );
        paragraph( tr( "发生了什么" ), info.whatHappened );
        paragraph( tr( "为什么重要" ), info.whyItMatters );
        section( tr( "如何解决" ), info.remediation );
        paragraph( tr( "技术细节" ), info.technicalNote );
    }

    if ( !d.relatedIds.isEmpty() ) {
        html += QStringLiteral( "<h3>%1</h3><ul>" ).arg( escapeHtml( tr( "相关主题" ) ) );
        for ( const QString &related : d.relatedIds ) {
            html += QStringLiteral( "<li><a href=\"helpid:/%1\"><code>%1</code></a></li>" ).arg( escapeHtml( related ) );
        }
        html += QStringLiteral( "</ul>" );
    }
    return html;
}

void HelpCenterDialog::keyPressEvent( QKeyEvent *event )
{
    if ( event->key() == Qt::Key_F1 ) {
        // already in the Help Center — ignore to avoid dialog stacking
        event->accept();
        return;
    }
    if ( event->key() == Qt::Key_Escape && m_searchBox->hasFocus() ) {
        m_searchBox->clear();
        event->accept();
        return;
    }
    QDialog::keyPressEvent( event );
}

} // namespace sicnu::app
