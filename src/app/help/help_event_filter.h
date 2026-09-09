/***************************************************************************
 * help_event_filter.h — F1 context help resolution
 *
 * Application-level event filter: F1 on any widget resolves the most relevant
 * Help ID and opens the Help Center anchored at that topic.
 *
 * Resolution order:
 *   1. dynamic property "helpId" on the widget or its parents (precise seam
 *      for dialogs/forms: any code can opt in with one line);
 *   2. objectName → id map registered by panels/dialogs;
 *   3. active workbench context id ("workbench.<id>");
 *   4. Help Center home.
 ***************************************************************************/
#pragma once

#include <QHash>
#include <QString>

#include <QObject>

class QWidget;

namespace sicnu::app
{

class HelpEventFilter : public QObject
{
    Q_OBJECT
  public:
    explicit HelpEventFilter( QObject *parent = nullptr );

    /// Registers an objectName → help id mapping (e.g. "kernelSizeEdit" →
    /// "parameter.rs.sar_speckle.kernelSize").
    void registerObjectName( const QString &objectName, const QString &helpId );

    /// Fallback topic for widgets without a mapping (active workbench id is
    /// refreshed by the shell).
    void setWorkbenchContext( const QString &workbenchHelpId );

    /// Resolves the help id for @p widget (documented resolution order).
    QString resolveHelpId( const QWidget *widget ) const;

  signals:
    /// Emitted when F1 resolves to @p helpId (empty id = open home).
    void helpRequested( const QString &helpId );

  protected:
    bool eventFilter( QObject *watched, QEvent *event ) override;

  private:
    QHash<QString, QString> m_objectNameMap;
    QString m_workbenchHelpId;
};

} // namespace sicnu::app
