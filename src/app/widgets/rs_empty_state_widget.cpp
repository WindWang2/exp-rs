#include "rs_empty_state_widget.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QStyle>
#include <QApplication>
#include <QFont>
#include <QFile>

namespace sicnu {

QIcon RsEmptyStateWidget::resolveIcon( const QString &iconName )
{
    if ( iconName.isEmpty() )
        return {};
    if ( iconName.startsWith( QLatin1String( ":/" ) ) || iconName.startsWith( QLatin1Char( '/' ) ) )
        return QIcon( iconName );

    // Check :/icons/<alias> resource alias
    const QString resPath = QStringLiteral( ":/icons/" ) + iconName;
    if ( QFile::exists( resPath ) || !QIcon( resPath ).isNull() )
        return QIcon( resPath );

    // Check system theme icon
    QIcon themeIcon = QIcon::fromTheme( iconName );
    if ( !themeIcon.isNull() )
        return themeIcon;

    // Fallback to standard style icon
    if ( qApp && qApp->style() )
        return qApp->style()->standardIcon( QStyle::SP_FileIcon );

    return {};
}

RsEmptyStateWidget::RsEmptyStateWidget( const QString &iconName,
                                       const QString &title,
                                       const QString &description,
                                       const QString &actionText,
                                       QWidget *parent )
    : QWidget( parent )
{
    setupUi( resolveIcon( iconName ), title, description, actionText );
}

RsEmptyStateWidget::RsEmptyStateWidget( const QIcon &icon,
                                       const QString &title,
                                       const QString &description,
                                       const QString &actionText,
                                       QWidget *parent )
    : QWidget( parent )
{
    setupUi( icon, title, description, actionText );
}

void RsEmptyStateWidget::setupUi( const QIcon &icon,
                                  const QString &title,
                                  const QString &description,
                                  const QString &actionText )
{
    setObjectName( QStringLiteral( "RsEmptyStateWidget" ) );
    setAttribute( Qt::WA_StyledBackground, true );

    m_mainLayout = new QVBoxLayout( this );
    m_mainLayout->setContentsMargins( 20, 24, 20, 24 );
    m_mainLayout->setSpacing( 8 );
    m_mainLayout->setAlignment( Qt::AlignCenter );

    // 1. Icon / illustration
    m_iconLabel = new QLabel( this );
    m_iconLabel->setObjectName( QStringLiteral( "rsEmptyStateIcon" ) );
    m_iconLabel->setAlignment( Qt::AlignCenter );
    m_mainLayout->addWidget( m_iconLabel );

    // 2. Title
    m_titleLabel = new QLabel( this );
    m_titleLabel->setObjectName( QStringLiteral( "rsEmptyStateTitle" ) );
    m_titleLabel->setAlignment( Qt::AlignCenter );
    m_titleLabel->setWordWrap( true );
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize( titleFont.pointSize() + 1 );
    titleFont.setWeight( QFont::DemiBold );
    m_titleLabel->setFont( titleFont );
    m_mainLayout->addWidget( m_titleLabel );

    // 3. Description
    m_descLabel = new QLabel( this );
    m_descLabel->setObjectName( QStringLiteral( "rsEmptyStateDesc" ) );
    m_descLabel->setAlignment( Qt::AlignCenter );
    m_descLabel->setWordWrap( true );
    m_descLabel->setMaximumWidth( 420 );
    m_mainLayout->addWidget( m_descLabel );

    // 4. CTA Button
    m_actionButton = new QPushButton( this );
    m_actionButton->setObjectName( QStringLiteral( "rsEmptyStateBtn" ) );
    m_actionButton->setProperty( "primary", true );
    m_actionButton->setCursor( Qt::PointingHandCursor );
    connect( m_actionButton, &QPushButton::clicked, this, &RsEmptyStateWidget::actionClicked );
    m_mainLayout->addWidget( m_actionButton, 0, Qt::AlignCenter );

    // Populate initial values
    setIcon( icon );
    setTitle( title );
    setDescription( description );
    setActionText( actionText );
}

void RsEmptyStateWidget::setIcon( const QIcon &icon )
{
    m_icon = icon;
    if ( !m_iconLabel )
        return;
    if ( m_icon.isNull() || m_iconSize.isEmpty() || m_iconSize.width() <= 0 || m_iconSize.height() <= 0 )
    {
        m_iconLabel->clear();
        m_iconLabel->hide();
    }
    else
    {
        QPixmap pix = m_icon.pixmap( m_iconSize );
        if ( !pix.isNull() && pix.size() != m_iconSize )
            pix = pix.scaled( m_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation );
        m_iconLabel->setPixmap( pix );
        m_iconLabel->show();
    }
}

void RsEmptyStateWidget::setIcon( const QString &iconName )
{
    setIcon( resolveIcon( iconName ) );
}

void RsEmptyStateWidget::setIconSize( const QSize &size )
{
    m_iconSize = size;
    if ( !m_icon.isNull() && m_iconLabel )
    {
        if ( m_iconSize.isEmpty() || m_iconSize.width() <= 0 || m_iconSize.height() <= 0 )
        {
            m_iconLabel->clear();
            m_iconLabel->hide();
        }
        else
        {
            QPixmap pix = m_icon.pixmap( m_iconSize );
            if ( !pix.isNull() && pix.size() != m_iconSize )
                pix = pix.scaled( m_iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation );
            m_iconLabel->setPixmap( pix );
            m_iconLabel->show();
        }
    }
}

void RsEmptyStateWidget::setTitle( const QString &title )
{
    if ( m_titleLabel )
    {
        m_titleLabel->setText( title );
        m_titleLabel->setVisible( !title.isEmpty() );
    }
}

void RsEmptyStateWidget::setDescription( const QString &description )
{
    if ( m_descLabel )
    {
        m_descLabel->setText( description );
        m_descLabel->setVisible( !description.isEmpty() );
    }
}

void RsEmptyStateWidget::setActionText( const QString &actionText )
{
    if ( m_actionButton )
    {
        m_actionButton->setText( actionText );
        m_actionButton->setVisible( !actionText.isEmpty() );
    }
}

void RsEmptyStateWidget::setActionVisible( bool visible )
{
    if ( m_actionButton )
        m_actionButton->setVisible( visible && !m_actionButton->text().isEmpty() );
}

QString RsEmptyStateWidget::title() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

QString RsEmptyStateWidget::description() const
{
    return m_descLabel ? m_descLabel->text() : QString();
}

QString RsEmptyStateWidget::actionText() const
{
    return m_actionButton ? m_actionButton->text() : QString();
}

bool RsEmptyStateWidget::isActionVisible() const
{
    return m_actionButton && !m_actionButton->isHidden() && !m_actionButton->text().isEmpty();
}

} // namespace sicnu
