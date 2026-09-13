/***************************************************************************
 * workbench_guidance.cpp — empty-state guidance implementation
 ***************************************************************************/
#include "app/help/workbench_guidance.h"

#include "app/help/help_system_controller.h"
#include "help/help_presenter.h"
#include "help/help_registry.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace sicnu::app
{

WorkbenchGuidance::WorkbenchGuidance( QWidget *parent )
    : QWidget( parent )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 24, 24, 24, 24 );
    layout->setSpacing( 8 );

    m_headline = new QLabel( this );
    QFont headlineFont = m_headline->font();
    headlineFont.setBold( true );
    headlineFont.setPointSizeF( headlineFont.pointSizeF() + 1.5 );
    m_headline->setFont( headlineFont );
    layout->addWidget( m_headline );

    m_body = new QLabel( this );
    m_body->setWordWrap( true );
    layout->addWidget( m_body );

    m_actionsRow = new QWidget( this );
    auto *actionsLayout = new QHBoxLayout( m_actionsRow );
    actionsLayout->setContentsMargins( 0, 0, 0, 0 );
    layout->addWidget( m_actionsRow );

    layout->addStretch( 1 );
}

void WorkbenchGuidance::setGuidance( const QString &helpTopicId )
{
    const sicnu::help::HelpDescriptor *d = sicnu::help::globalHelpRegistry().find( helpTopicId );
    if ( d && d->guidance.has_value() ) {
        setGuidanceText( d->guidance->headline.isEmpty() ? d->title : d->guidance->headline,
                         d->guidance->body, d->guidance->actionCommandIds, helpTopicId );
    } else if ( d ) {
        setGuidanceText( d->title, d->summary, {}, helpTopicId );
    } else {
        setGuidanceText( QObject::tr( "Start Here" ),
                         QObject::tr( "No guidance content has been configured for this panel yet." ), {}, helpTopicId );
    }
}

void WorkbenchGuidance::setGuidanceText( const QString &headline, const QString &body,
                                         const QStringList &actionCommandIds,
                                         const QString &helpTopicId )
{
    m_headline->setText( headline );
    m_body->setText( body );
    m_helpTopicId = helpTopicId;

    if ( auto *rowLayout = m_actionsRow->layout() ) {
        QLayoutItem *item = nullptr;
        while ( ( item = rowLayout->takeAt( 0 ) ) != nullptr ) {
            if ( item->widget() )
                item->widget()->deleteLater();
            delete item;
        }
    }

    auto *rowLayout = qobject_cast<QHBoxLayout *>( m_actionsRow->layout() );
    if ( rowLayout ) {
        for ( const QString &commandId : actionCommandIds ) {
            const sicnu::help::HelpDescriptor *cmdHelp =
                sicnu::help::globalHelpRegistry().find( QStringLiteral( "command.%1" ).arg( commandId ) );
            auto *button = new QPushButton( cmdHelp ? cmdHelp->title : commandId, m_actionsRow );
            connect( button, &QPushButton::clicked, this, [this, commandId] {
                emit actionRequested( commandId );
            } );
            rowLayout->addWidget( button );
        }
        if ( !helpTopicId.isEmpty() ) {
            auto *learnMore = new QPushButton( QObject::tr( "Learn More (F1)" ), m_actionsRow );
            learnMore->setFlat( true );
            connect( learnMore, &QPushButton::clicked, this, [this, helpTopicId] {
                HelpSystemController::instance().openHelpCenter( helpTopicId );
            } );
            rowLayout->addWidget( learnMore );
        }
        rowLayout->addStretch( 1 );
        m_actionsRow->setVisible( rowLayout->count() > 0 );
    }
}

} // namespace sicnu::app
