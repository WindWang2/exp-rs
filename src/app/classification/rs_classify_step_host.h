// rs_classify_step_host.h — Right-side stacked panels for classification steps.
#pragma once

#include "rs_classify_workflow_controller.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;

/**
 * \brief Host for the seven classification step panels.
 *
 * Each panel is a skeleton (title, completion tip, gate label, empty body,
 * prev/next). Main window fills body widgets with real step controls.
 */
class RsClassifyStepHost : public QWidget
{
    Q_OBJECT

  public:
    explicit RsClassifyStepHost( QWidget *parent = nullptr );

    void setCurrentStep( RsClassifyStep s );
    QWidget *panel( RsClassifyStep s ) const;

    /// Content area for step-specific controls (objectName classifyStepBody).
    QWidget *body( RsClassifyStep s ) const;

    /// Soft-gate status label for the given step (objectName classifyStepGate).
    QLabel *gateLabel( RsClassifyStep s ) const;

    QPushButton *prevButton( RsClassifyStep s ) const;
    QPushButton *nextButton( RsClassifyStep s ) const;

  signals:
    void prevClicked();
    void nextClicked();

  private:
    QWidget *buildPanel( RsClassifyStep s );

    QStackedWidget *mStack = nullptr;
    QVector<QWidget *> mPanels; // size Count
};
