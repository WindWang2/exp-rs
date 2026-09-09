/***************************************************************************
 * inspector_host.cpp — section lifecycle: lazy populate + stale cancel
 ***************************************************************************/
#include "inspector_host.h"

#include <QLabel>
#include <QSet>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace sicnu::app
{

InspectorHost::InspectorHost( QWidget *parent )
    : QWidget( parent )
{
    setObjectName( QStringLiteral( "rsInspectorHost" ) );
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );

    m_stack = new QStackedWidget( this );
    m_stack->setObjectName( QStringLiteral( "rsInspectorStack" ) );
    layout->addWidget( m_stack );

    m_placeholder = new QLabel( tr( "未选中对象" ), this );
    m_placeholder->setObjectName( QStringLiteral( "rsInspectorPlaceholder" ) );
    m_placeholder->setAlignment( Qt::AlignCenter );
    m_stack->addWidget( m_placeholder );
}

void InspectorHost::registerSection( InspectorSection *section )
{
    if ( !section || m_sections.contains( section ) )
        return;
    m_sections.append( section );
    // Stable ordering by (order, id).
    std::sort( m_sections.begin(), m_sections.end(),
               []( InspectorSection *a, InspectorSection *b ) {
                   const int oa = a->order(), ob = b->order();
                   if ( oa != ob )
                       return oa < ob;
                   return a->sectionId() < b->sectionId();
               } );
    rebuildTabs();
}

void InspectorHost::attachSelectionContext( SelectionContext *context )
{
    if ( !context )
        return;
    connect( context, &SelectionContext::changed, this, &InspectorHost::onContextChanged );
    onContextChanged( context->snapshot() );
}

void InspectorHost::setSnapshot( const SelectionContextSnapshot &snapshot )
{
    onContextChanged( snapshot );
}

void InspectorHost::setPlaceholderText( const QString &text )
{
    m_placeholder->setText( text );
}

void InspectorHost::onContextChanged( const SelectionContextSnapshot &snapshot )
{
    m_snapshot = snapshot;
    m_hasSnapshot = true;

    // Unsupported sections drop in-flight work; supported ones repopulate in
    // rebuildTabs() (the shown section) or lazily on first show.
    for ( InspectorSection *section : m_sections )
    {
        if ( !section->supports( m_snapshot ) )
            section->cancelPending();
    }
    rebuildTabs();
}

void InspectorHost::rebuildTabs()
{
    // Rebuild the tab widget contents while preserving the current section.
    QTabWidget *oldTabs = m_stack->findChild<QTabWidget *>( QStringLiteral( "rsInspectorTabs" ) );
    InspectorSection *current = currentSection();

    const bool anySupported = std::any_of(
        m_sections.cbegin(), m_sections.cend(),
        [this]( InspectorSection *s ) { return s->supports( m_snapshot ); } );

    if ( !m_hasSnapshot || !anySupported )
    {
        // #777: QTabWidget owns its pages — deleting it would destroy the
        // registered InspectorSections while m_sections keeps their pointers
        // (use-after-free on the next selection change). Detach the sections
        // first; they are re-added when a supported selection returns.
        rescueSectionsFrom( oldTabs );
        m_stack->setCurrentWidget( m_placeholder );
        return;
    }

    QTabWidget *tabs = oldTabs;
    if ( !tabs )
    {
        tabs = new QTabWidget( this );
        tabs->setObjectName( QStringLiteral( "rsInspectorTabs" ) );
        tabs->setDocumentMode( true );
        m_stack->addWidget( tabs );
        // #812: the shown section must populate on user tab switches too —
        // without this, secondary tabs stay permanently blank.
        connect( tabs, &QTabWidget::currentChanged, this, &InspectorHost::onTabChanged );
    }

    // Sync tabs with supported sections.
    QSet<QString> wanted;
    for ( InspectorSection *section : m_sections )
    {
        if ( !section->supports( m_snapshot ) )
            continue;
        wanted.insert( section->sectionId() );
        const int existing = tabs->indexOf( section );
        if ( existing < 0 )
            tabs->addTab( section, section->title() );
        else
            tabs->setTabText( existing, section->title() );
    }
    for ( int i = tabs->count() - 1; i >= 0; --i )
    {
        auto *page = tabs->widget( i );
        if ( auto *section = qobject_cast<InspectorSection *>( page ) )
        {
            if ( !wanted.contains( section->sectionId() ) )
            {
                tabs->removeTab( i );
                // Keep orphaned sections alive under the host, not the tab
                // widget's internal stack (same #777 ownership hazard).
                section->setParent( this );
                section->hide();
            }
        }
    }

    m_stack->setCurrentWidget( tabs );

    // Restore / choose selection without re-entering onTabChanged; the shown
    // section is populated exactly once below.
    if ( current && tabs->indexOf( current ) >= 0 )
    {
        const QSignalBlocker blocker( tabs );
        tabs->setCurrentWidget( current );
    }
    else if ( tabs->count() > 0 )
    {
        const QSignalBlocker blocker( tabs );
        tabs->setCurrentIndex( 0 );
    }
    if ( InspectorSection *shown = currentSection() )
    {
        shown->populate( m_snapshot );
        shown->setProperty( "rsLazyPopulated", true );
    }
}

void InspectorHost::onTabChanged( int index )
{
    QTabWidget *tabs = m_stack->findChild<QTabWidget *>( QStringLiteral( "rsInspectorTabs" ) );
    if ( !tabs || index < 0 || index >= tabs->count() )
        return;
    InspectorSection *section = qobject_cast<InspectorSection *>( tabs->widget( index ) );
    if ( !section || !m_hasSnapshot || !section->supports( m_snapshot ) )
        return;
    section->populate( m_snapshot );
    section->setProperty( "rsLazyPopulated", true );
}

void InspectorHost::rescueSectionsFrom( QTabWidget *tabs )
{
    if ( !tabs )
        return;
    while ( tabs->count() > 0 )
    {
        QWidget *page = tabs->widget( 0 );
        tabs->removeTab( 0 );
        if ( page )
        {
            page->setParent( this );
            page->hide();
        }
    }
    delete tabs;
}

InspectorSection *InspectorHost::currentSection() const
{
    QTabWidget *tabs = m_stack->findChild<QTabWidget *>( QStringLiteral( "rsInspectorTabs" ) );
    if ( !tabs )
        return nullptr;
    return qobject_cast<InspectorSection *>( tabs->currentWidget() );
}

} // namespace sicnu::app
