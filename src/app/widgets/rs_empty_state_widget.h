#pragma once

#include <QWidget>
#include <QIcon>
#include <QString>
#include <QSize>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace sicnu {

/**
 * \brief Standardized Empty State presentation widget across RS Studio panels and views.
 *
 * Provides a clean, centered layout with:
 * - High-DPI scalable icon / illustration
 * - Clear, bold headline title
 * - Helpful explanatory / guidance text
 * - Optional Call-To-Action (CTA) button emitting actionClicked()
 */
class RsEmptyStateWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RsEmptyStateWidget( const QString &iconName,
                                 const QString &title,
                                 const QString &description,
                                 const QString &actionText = QString(),
                                 QWidget *parent = nullptr );

    explicit RsEmptyStateWidget( const QIcon &icon,
                                 const QString &title,
                                 const QString &description,
                                 const QString &actionText = QString(),
                                 QWidget *parent = nullptr );

    ~RsEmptyStateWidget() override = default;

    // Dynamic mutators
    void setIcon( const QIcon &icon );
    void setIcon( const QString &iconName );
    void setIconSize( const QSize &size );
    void setTitle( const QString &title );
    void setDescription( const QString &description );
    void setActionText( const QString &actionText );
    void setActionVisible( bool visible );

    // Accessors
    QString title() const;
    QString description() const;
    QString actionText() const;
    bool isActionVisible() const;

signals:
    void actionClicked();

private:
    void setupUi( const QIcon &icon,
                  const QString &title,
                  const QString &description,
                  const QString &actionText );
    static QIcon resolveIcon( const QString &iconName );

    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_descLabel = nullptr;
    QPushButton *m_actionButton = nullptr;
    QVBoxLayout *m_mainLayout = nullptr;
    QSize m_iconSize{ 48, 48 };
    QIcon m_icon;
};

} // namespace sicnu
