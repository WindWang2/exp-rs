/***************************************************************************
 * rs_operator_catalog_panel.cpp — searchable rs: operator catalog
 ***************************************************************************/
#include "rs_operator_catalog_panel.h"

#include "agent/harness/capability_catalog.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace sicnu::app
{

namespace
{
constexpr int kMaxRecent = 12;
constexpr int kMaxVisibleRows = 400; // bounded rendering; filter narrows first

QString settingsGroup() { return QStringLiteral( "workbench/processing" ); }
} // namespace

RsOperatorCatalogPanel::RsOperatorCatalogPanel( QWidget *parent )
    : QgsDockWidget( parent )
{
    setObjectName( QStringLiteral( "rsOperatorCatalogDock" ) );
    setWindowTitle( tr( "遥感算子目录" ) );

    auto *central = new QWidget( this );
    auto *layout = new QVBoxLayout( central );
    layout->setContentsMargins( 8, 8, 8, 8 );

    m_search = new QLineEdit( central );
    m_search->setObjectName( QStringLiteral( "rsOperatorCatalogSearch" ) );
    m_search->setAccessibleName( tr( "搜索算子" ) );
    m_search->setPlaceholderText( tr( "搜索算子（名称/描述）…" ) );
    m_search->setClearButtonEnabled( true );
    layout->addWidget( m_search );

    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget( new QLabel( tr( "输入模态：" ), central ) );
    m_modalityFilter = new QComboBox( central );
    m_modalityFilter->setObjectName( QStringLiteral( "rsOperatorCatalogModality" ) );
    m_modalityFilter->setAccessibleName( tr( "输入模态过滤" ) );
    m_modalityFilter->addItem( tr( "全部" ), QString() );
    filterRow->addWidget( m_modalityFilter, 1 );
    filterRow->addStretch( 1 );
    layout->addLayout( filterRow );

    m_list = new QListWidget( central );
    m_list->setObjectName( QStringLiteral( "rsOperatorCatalogList" ) );
    m_list->setAccessibleName( tr( "算子列表" ) );
    m_list->setUniformItemSizes( true );
    m_list->setContextMenuPolicy( Qt::CustomContextMenu );
    m_list->setSelectionMode( QAbstractItemView::SingleSelection );
    layout->addWidget( m_list, 1 );

    m_openBtn = new QPushButton( tr( "在任务面板中打开" ), central );
    m_openBtn->setObjectName( QStringLiteral( "rsOperatorCatalogOpen" ) );
    m_openBtn->setAccessibleName( tr( "在任务面板中打开" ) );
    m_openBtn->setEnabled( false );
    layout->addWidget( m_openBtn );

    connect( m_search, &QLineEdit::textChanged, this, &RsOperatorCatalogPanel::applyFilter );
    connect( m_modalityFilter, &QComboBox::currentIndexChanged,
             this, &RsOperatorCatalogPanel::applyFilter );
    connect( m_list, &QListWidget::currentRowChanged, this,
             [this]( int row ) { m_openBtn->setEnabled( row >= 0 ); } );
    connect( m_list, &QListWidget::itemDoubleClicked, this, [this]( QListWidgetItem * ) {
        onOpenClicked();
    } );
    connect( m_list, &QListWidget::customContextMenuRequested,
             this, &RsOperatorCatalogPanel::onContextRequested );
    connect( m_openBtn, &QPushButton::clicked, this, &RsOperatorCatalogPanel::onOpenClicked );

    QSettings settings;
    m_recent = settings.value( QStringLiteral( "%1/recent" ).arg( settingsGroup() ) )
                   .toStringList();
    m_favorites = settings.value( QStringLiteral( "%1/favorites" ).arg( settingsGroup() ) )
                      .toStringList();

    setWidget( central );
    reloadCatalog();
}

void RsOperatorCatalogPanel::rebuildEntries()
{
    m_entries.clear();
    const auto ids = sicnu::operators::RSOperatorRegistry::instance().operatorNames();
    auto &catalog = sicnu::agent::harness::CapabilityCatalog::instance();
    for ( const auto &rawId : ids )
    {
        auto op = sicnu::operators::RSOperatorRegistry::instance().create( rawId );
        if ( !op )
            continue;
        Entry entry;
        entry.id = QString::fromStdString( rawId );
        entry.displayName = QString::fromStdString( op->displayName() );
        entry.description = QString::fromStdString( op->description() );
        if ( catalog.hasEntry( rawId ) )
        {
            const Json::Value capability = catalog.capability( rawId );
            if ( capability.isMember( "family" ) && capability["family"].isString() )
                entry.family = QString::fromStdString( capability["family"].asString() );
            if ( capability.isMember( "modality" ) && capability["modality"].isArray() &&
                 !capability["modality"].empty() && capability["modality"][0].isString() )
                entry.modality = QString::fromStdString( capability["modality"][0].asString() );
        }
        m_entries.append( entry );
    }
    // Deterministic catalog order; the recent pinning happens in applyFilter.
    std::sort( m_entries.begin(), m_entries.end(),
               []( const Entry &a, const Entry &b ) { return a.id < b.id; } );
}

void RsOperatorCatalogPanel::reloadCatalog()
{
    m_modalityFilter->blockSignals( true );
    const QString currentModality = m_modalityFilter->currentData().toString();
    while ( m_modalityFilter->count() > 1 )
        m_modalityFilter->removeItem( m_modalityFilter->count() - 1 );
    rebuildEntries();

    // Restore/refresh the modality combo now that entries are known.
    QStringList modalities;
    for ( const Entry &entry : m_entries )
        if ( !entry.modality.isEmpty() && !modalities.contains( entry.modality ) )
            modalities.append( entry.modality );
    std::sort( modalities.begin(), modalities.end() );
    for ( const QString &modality : modalities )
        m_modalityFilter->addItem( modality, modality );
    const int restore = m_modalityFilter->findData( currentModality );
    if ( restore >= 0 )
        m_modalityFilter->setCurrentIndex( restore );
    m_modalityFilter->blockSignals( false );

    applyFilter();
}

void RsOperatorCatalogPanel::applyFilter()
{
    const QString needle = m_search->text().trimmed().toLower();
    const QString modality = m_modalityFilter->currentData().toString();

    m_list->clear();
    // Recent block first (pinned when not filtering), then the deterministic
    // catalog order. Recent pinning is skipped while filtering so search
    // stays a pure name/description match.
    int shown = 0;
    QStringList pinned;
    if ( needle.isEmpty() && modality.isEmpty() )
    {
        for ( const QString &recentId : std::as_const( m_recent ) )
        {
            if ( shown >= kMaxVisibleRows )
                break;
            for ( const Entry &entry : m_entries )
            {
                if ( entry.id == recentId )
                {
                    auto *item = new QListWidgetItem(
                        QStringLiteral( "[最近] %1 — %2" ).arg( entry.displayName, entry.id ),
                        m_list );
                    item->setData( Qt::UserRole, entry.id );
                    item->setToolTip( entry.description );
                    pinned.append( entry.id );
                    ++shown;
                    break;
                }
            }
        }
    }
    for ( const Entry &entry : m_entries )
    {
        if ( shown >= kMaxVisibleRows )
            break;
        if ( pinned.contains( entry.id ) )
            continue; // already visible in the recent block — never twice
        if ( !needle.isEmpty() &&
             !entry.id.toLower().contains( needle ) &&
             !entry.displayName.toLower().contains( needle ) &&
             !entry.description.toLower().contains( needle ) )
            continue;
        if ( !modality.isEmpty() && entry.modality != modality )
            continue;
        const bool favorite = m_favorites.contains( entry.id );
        auto *item = new QListWidgetItem(
            QStringLiteral( "%1%2 — %3%4" )
                .arg( favorite ? QStringLiteral( "★ " ) : QString(),
                      entry.displayName, entry.id,
                      entry.family.isEmpty()
                          ? QString()
                          : QStringLiteral( " (%1)" ).arg( entry.family ) ),
            m_list );
        item->setData( Qt::UserRole, entry.id );
        item->setToolTip( entry.description );
        ++shown;
    }
}

QStringList RsOperatorCatalogPanel::visibleOperatorIds() const
{
    QStringList ids;
    for ( int i = 0; i < m_list->count(); ++i )
        ids.append( m_list->item( i )->data( Qt::UserRole ).toString() );
    return ids;
}

QStringList RsOperatorCatalogPanel::recentOperators() const
{
    return m_recent;
}

QStringList RsOperatorCatalogPanel::favoriteOperators() const
{
    return m_favorites;
}

void RsOperatorCatalogPanel::noteOperatorRun( const QString &operatorId )
{
    if ( operatorId.isEmpty() )
        return;
    m_recent.removeAll( operatorId );
    m_recent.prepend( operatorId );
    while ( m_recent.size() > kMaxRecent )
        m_recent.removeLast();
    QSettings settings;
    settings.setValue( QStringLiteral( "%1/recent" ).arg( settingsGroup() ), m_recent );
    applyFilter();
}

void RsOperatorCatalogPanel::toggleFavorite( const QString &operatorId )
{
    if ( m_favorites.contains( operatorId ) )
        m_favorites.removeAll( operatorId );
    else
        m_favorites.append( operatorId );
    QSettings settings;
    settings.setValue( QStringLiteral( "%1/favorites" ).arg( settingsGroup() ), m_favorites );
    applyFilter();
}

void RsOperatorCatalogPanel::onContextRequested( const QPoint &pos )
{
    QListWidgetItem *item = m_list->itemAt( pos );
    if ( !item )
        return;
    const QString id = item->data( Qt::UserRole ).toString();
    QMenu menu( this );
    QAction *favoriteAction =
        menu.addAction( m_favorites.contains( id ) ? tr( "取消收藏" ) : tr( "收藏" ) );
    QAction *chosen = menu.exec( m_list->mapToGlobal( pos ) );
    if ( chosen == favoriteAction )
        toggleFavorite( id );
}

void RsOperatorCatalogPanel::onOpenClicked()
{
    QListWidgetItem *item = m_list->currentItem();
    if ( !item )
        return;
    const QString id = item->data( Qt::UserRole ).toString();
    if ( !id.isEmpty() )
        emit operatorSelected( id );
}

} // namespace sicnu::app
