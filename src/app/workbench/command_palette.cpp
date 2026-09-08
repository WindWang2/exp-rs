/***************************************************************************
 * command_palette.cpp — palette implementation
 ***************************************************************************/
#include "command_palette.h"

#include "app/design_tokens.h"

#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace sicnu::app
{

namespace
{
constexpr char kRecentKey[] = "workbench/palette/recent";
constexpr int kMaxRecent = 8;
} // namespace

CommandPalette::CommandPalette( CommandRegistry *registry, QWidget *parent )
    : QDialog( parent )
    , m_registry( registry )
{
    setObjectName( QStringLiteral( "rsCommandPalette" ) );
    setWindowTitle( tr( "命令面板" ) );
    setModal( false );
    setWindowFlags( Qt::Dialog | Qt::FramelessWindowHint );
    setMinimumSize( 520, 120 );
    setMaximumHeight( 480 );

    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( SicnuUi::Tokens::spacing( 2 ), SicnuUi::Tokens::spacing( 2 ),
                                SicnuUi::Tokens::spacing( 2 ), SicnuUi::Tokens::spacing( 2 ) );
    layout->setSpacing( SicnuUi::Tokens::spacing( 1 ) );

    m_input = new QLineEdit( this );
    m_input->setObjectName( QStringLiteral( "rsCommandPaletteInput" ) );
    m_input->setPlaceholderText( tr( "输入命令名称 / 关键词…" ) );
    m_input->setAccessibleName( tr( "命令搜索" ) );
    layout->addWidget( m_input );

    m_list = new QListWidget( this );
    m_list->setObjectName( QStringLiteral( "rsCommandPaletteList" ) );
    m_list->setUniformItemSizes( true );
    m_list->setAlternatingRowColors( false );
    m_list->setAccessibleName( tr( "命令结果" ) );
    layout->addWidget( m_list, 1 );

    m_hint = new QLabel( tr( "↑↓ 选择 · Enter 执行 · Esc 关闭" ), this );
    m_hint->setObjectName( QStringLiteral( "rsCommandPaletteHint" ) );
    layout->addWidget( m_hint );

    connect( m_input, &QLineEdit::textChanged, this, &CommandPalette::reapplyFilter );
    connect( m_list, &QListWidget::itemActivated, this, &CommandPalette::runCurrent );
    m_input->installEventFilter( this );
    m_list->installEventFilter( this );

    rebuildRecency();
    rebuildEntries();
}

void CommandPalette::openPalette()
{
    rebuildRecency();
    rebuildEntries();
    m_input->clear();
    reapplyFilter();

    if ( QWidget *host = parentWidget() )
    {
        const QRect parentGeom = host->geometry();
        const int x = parentGeom.x() + ( parentGeom.width() - width() ) / 2;
        const int y = parentGeom.y() + parentGeom.height() / 8;
        move( x, y );
    }
    resize( 520, sizeHint().height() );
    show();
    raise();
    activateWindow();
    m_input->setFocus();
}

void CommandPalette::rebuildRecency()
{
    QSettings settings;
    m_recent = settings.value( QLatin1String( kRecentKey ) ).toStringList();
}

void CommandPalette::rebuildEntries()
{
    m_entries.clear();
    if ( !m_registry )
        return;
    for ( const CommandDefinition *def : m_registry->definitions() )
    {
        Entry entry;
        entry.id = def->id;
        entry.title = def->title;
        entry.category = def->category;
        entry.keywords = def->keywords;
        entry.lastUsedRank = m_recent.indexOf( def->id );
        m_entries.append( entry );
    }
}

QListWidgetItem *CommandPalette::makeItem( const Entry &entry, const QString &reason )
{
    const QString label = reason.isEmpty()
                              ? QStringLiteral( "%1 — %2" ).arg( entry.title, entry.category )
                              : QStringLiteral( "%1 — %2 (%3)" ).arg( entry.title, entry.category, reason );
    auto *item = new QListWidgetItem( label, m_list );
    item->setData( Qt::UserRole, entry.id );
    item->setData( Qt::AccessibleTextRole, entry.title );
    if ( !reason.isEmpty() )
    {
        item->setForeground( SicnuUi::Tokens::themeIsDark( this )
                                 ? SicnuUi::Tokens::Dark::inkSecondary
                                 : SicnuUi::Tokens::Light::inkSecondary );
    }
    return item;
}

void CommandPalette::reapplyFilter()
{
    if ( !m_registry )
        return;
    m_list->clear();
    const QString query = m_input->text().trimmed();
    const QString lowered = query.toLower();

    // Rank: recency-first when no query; otherwise prefix > keyword > id > recency.
    QVector<QPair<int, const Entry *>> ranked;
    for ( const Entry &entry : m_entries )
    {
        int score = -1;
        if ( lowered.isEmpty() )
        {
            score = entry.lastUsedRank >= 0 ? 1000 - entry.lastUsedRank : 0;
        }
        else
        {
            const QString title = entry.title.toLower();
            const QString id = entry.id.toLower();
            if ( title.startsWith( lowered ) || id.startsWith( lowered ) )
                score = 3000;
            else if ( title.contains( lowered ) )
                score = 2000;
            else
            {
                bool keywordHit = false;
                for ( const QString &kw : entry.keywords )
                {
                    if ( kw.toLower().contains( lowered ) )
                    {
                        keywordHit = true;
                        break;
                    }
                }
                if ( keywordHit )
                    score = 1000;
                else if ( id.contains( lowered ) )
                    score = 500;
            }
            if ( score > 0 && entry.lastUsedRank >= 0 )
                score += 100 - qMin( 99, entry.lastUsedRank );
        }
        if ( score >= 0 )
            ranked.append( { score, &entry } );
    }
    std::sort( ranked.begin(), ranked.end(),
               []( const auto &a, const auto &b ) { return a.first > b.first; } );

    int shown = 0;
    for ( const auto &match : ranked )
    {
        if ( shown >= kMaxRows )
            break;
        const QString reason = m_registry->unavailabilityReason( match.second->id );
        makeItem( *match.second, reason );
        ++shown;
    }

    if ( m_list->count() > 0 )
        m_list->setCurrentRow( 0 );
}

void CommandPalette::runCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if ( !item )
        return;
    const QString id = item->data( Qt::UserRole ).toString();
    if ( !m_registry || !m_registry->trigger( id ) )
        return; // unavailable — reason already visible on the row

    // Record recency (bounded list, most-recent-first).
    m_recent.removeAll( id );
    m_recent.prepend( id );
    while ( m_recent.size() > kMaxRecent )
        m_recent.removeLast();
    QSettings settings;
    settings.setValue( QLatin1String( kRecentKey ), m_recent );

    accept();
}

bool CommandPalette::eventFilter( QObject *watched, QEvent *event )
{
    if ( event->type() == QEvent::KeyPress )
    {
        auto *keyEvent = static_cast<QKeyEvent *>( event );
        switch ( keyEvent->key() )
        {
            case Qt::Key_Escape:
                reject();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                runCurrent();
                return true;
            case Qt::Key_Down:
                if ( m_list->count() > 0 )
                    m_list->setCurrentRow( qMin( m_list->currentRow() + 1, m_list->count() - 1 ) );
                return true;
            case Qt::Key_Up:
                if ( m_list->count() > 0 )
                    m_list->setCurrentRow( qMax( m_list->currentRow() - 1, 0 ) );
                return true;
            default:
                break;
        }
    }
    else if ( event->type() == QEvent::FocusOut && watched == m_input )
    {
        // Clicking the list steals focus legitimately; only close when focus
        // leaves the whole palette.
        if ( !geometry().contains( QCursor::pos() ) )
            reject();
    }
    return QDialog::eventFilter( watched, event );
}

} // namespace sicnu::app
