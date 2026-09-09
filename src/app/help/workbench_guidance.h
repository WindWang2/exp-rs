/***************************************************************************
 * workbench_guidance.h — empty-state / next-step guidance widget
 *
 * Small, actionable, non-modal onboarding: headline + 1–3 sentence body +
 * command buttons (actionCommandIds from the workbench GuidanceDescriptor)
 * + a "learn more" link into the Help Center. Panels embed this instead of
 * hand-rolling per-panel empty states.
 ***************************************************************************/
#pragma once

#include <QWidget>

class QLabel;

namespace sicnu::app
{

class WorkbenchGuidance : public QWidget
{
    Q_OBJECT
  public:
    explicit WorkbenchGuidance( QWidget *parent = nullptr );

    /// Populates from the guidance payload of @p helpTopicId's descriptor.
    /// Unknown ids render generic guidance (never crash, never empty).
    void setGuidance( const QString &helpTopicId );

    /// Direct form (no registry lookup) for ad-hoc empty states.
    void setGuidanceText( const QString &headline, const QString &body,
                          const QStringList &actionCommandIds, const QString &helpTopicId );

  signals:
    /// User clicked a recommended command (shell resolves id → trigger).
    void actionRequested( const QString &commandId );

  private:
    QLabel *m_headline = nullptr;
    QLabel *m_body = nullptr;
    QWidget *m_actionsRow = nullptr;
    QString m_helpTopicId;
};

} // namespace sicnu::app
