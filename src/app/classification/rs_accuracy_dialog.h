// rs_accuracy_dialog.h — Phase 10A Task 10.9.
//
// Thin modal-style wrapper around RsAccuracyPanel for optional "popup"
// viewing. The dialog is shown non-modally by the main window so the
// QgsTask's taskCompleted lambda can return promptly; the dialog deletes
// itself on close.
#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

#include "rs_accuracy_assessment.h"

class RsAccuracyPanel;

class RsAccuracyDialog : public QDialog
{
    Q_OBJECT
  public:
    RsAccuracyDialog( const RsAccuracyAssessment::Result &result,
                      const QHash<int, QString> &classNames,
                      QWidget *parent = nullptr );

  private:
    RsAccuracyPanel *mPanel = nullptr;
};
