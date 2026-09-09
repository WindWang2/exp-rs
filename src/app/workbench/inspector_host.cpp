/***************************************************************************
 * inspector_host.cpp — section lifecycle: lazy populate + stale cancel
 ***************************************************************************/
#include "inspector_host.h"

#include <QLabel>
#include <QSet>
#include <QSignalBlocker>
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
    section->setParent( this );
    section->hide();
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
        if ( oldTabs )
        {
            for ( InspectorSection *section : m_sections )
            {
                if ( section )
                {
                    section->setParent( this );
                    section->hide();
                }
            }
            delete oldTabs;
        }
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
        connect( tabs, &QTabWidget::currentChanged, this, [this, tabs]( int index ) {
            if ( index < 0 )
                return;
            if ( auto *sec = qobject_cast<InspectorSection *>( tabs->widget( index ) ) )
            {
                sec->populate( m_snapshot );
                sec->setProperty( "rsLazyPopulated", true );
            }
        } );
    }

    {
        QSignalBlocker blocker( tabs );

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
                    tabs->removeTab( i );
            }
        }

        m_stack->setCurrentWidget( tabs );

        // Restore / choose selection.
        if ( current && tabs->indexOf( current ) >= 0 )
            tabs->setCurrentWidget( current );
        else if ( tabs->count() > 0 )
            tabs->setCurrentIndex( 0 );
    }

    // Populate exactly the shown section.
    if ( InspectorSection *shown = currentSection() )
    {
        shown->populate( m_snapshot );
        shown->setProperty( "rsLazyPopulated", true );
    }
}

InspectorSection *InspectorHost::currentSection() const
{
    QTabWidget *tabs = m_stack->findChild<QTabWidget *>( QStringLiteral( "rsInspectorTabs" ) );
    if ( !tabs )
        return nullptr;
    return qobject_cast<InspectorSection *>( tabs->currentWidget() );
}

} // namespace sicnu::app
