// rs_accuracy_dialog.cpp — Phase 10A Task 10.9 (panel wrapper).

#include "rs_accuracy_dialog.h"

#include "dialogs/dialog_help_catalog.h"
#include "rs_accuracy_panel.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

RsAccuracyDialog::RsAccuracyDialog( const RsAccuracyAssessment::Result &result,
                                    const QHash<int, QString> &classNames,
                                    QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "Accuracy Assessment" ) );
  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "accuracy" ) );
  resize( 720, 560 );

  auto *layout = new QVBoxLayout( this );
  mPanel = new RsAccuracyPanel( this );
  mPanel->setResult( result, classNames );
  layout->addWidget( mPanel, 1 );

  auto *row = new QHBoxLayout();
  auto *helpBtn = new QPushButton( tr( "Help" ), this );
  SicnuDialogHelp::tip( helpBtn, tr( "View accuracy metric explanations (OA, Kappa, confusion matrix, etc.)." ) );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "accuracy" ), windowTitle() );
  } );
  row->addWidget( helpBtn );
  row->addStretch();
  auto *bb = new QDialogButtonBox( QDialogButtonBox::Close, this );
  connect( bb, &QDialogButtonBox::rejected, this, &QDialog::reject );
  row->addWidget( bb );
  layout->addLayout( row );
}
