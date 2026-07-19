// rs_accuracy_dialog.cpp — Phase 10A Task 10.9 (panel wrapper).

#include "rs_accuracy_dialog.h"

#include "rs_accuracy_panel.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

RsAccuracyDialog::RsAccuracyDialog( const RsAccuracyAssessment::Result &result,
                                    const QHash<int, QString> &classNames,
                                    QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "Accuracy Assessment" ) );
  resize( 720, 560 );

  auto *layout = new QVBoxLayout( this );
  mPanel = new RsAccuracyPanel( this );
  mPanel->setResult( result, classNames );
  layout->addWidget( mPanel, 1 );

  auto *bb = new QDialogButtonBox( QDialogButtonBox::Close, this );
  connect( bb, &QDialogButtonBox::rejected, this, &QDialog::reject );
  layout->addWidget( bb );
}
