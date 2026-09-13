// rs_classifier_load_dialog.cpp — Phase 10A.1.3.

#include "rs_classifier_load_dialog.h"
#include "dialogs/dialog_help_catalog.h"
#include "dialogs/dialog_utils.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

RsClassifierLoadDialog::RsClassifierLoadDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "Load Classifier Model" ) );
  SicnuUi::polishDialog( this, 440 );

  auto *layout = SicnuUi::makeDialogRootLayout( this );
  layout->addWidget( SicnuUi::makeHintLabel(
    this, tr( "Choose the algorithm type and specify the trained OpenCV model file (.yml)." ) ) );

  QFrame *sec = SicnuUi::makeSection( this, tr( "Model" ) );
  mRbBayes = new QRadioButton( tr( "Normal Bayes (maximum likelihood)" ), sec );
  SicnuDialogHelp::tip( mRbBayes, tr( "Normal Bayes classifier: a maximum-likelihood model assuming multivariate normal class distributions" ) );
  mRbSvm = new QRadioButton( tr( "SVM (RBF kernel)" ), sec );
  SicnuDialogHelp::tip( mRbSvm, tr( "Support vector machine classifier: uses the radial basis kernel (RBF) for non-linearly separable land covers" ) );
  mRbBayes->setChecked( true );
  auto *grp = new QButtonGroup( this );
  grp->addButton( mRbBayes );
  grp->addButton( mRbSvm );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  mRbBayes );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  mRbSvm );

  auto *pathRow = new QHBoxLayout;
  mPathEdit = new QLineEdit( sec );
  mPathEdit->setPlaceholderText( tr( "Model file path (.yml)" ) );
  SicnuDialogHelp::tip( mPathEdit, tr( "Path of the saved OpenCV trained model parameter file (*.yml)" ) );
  auto *browse = new QPushButton( tr( "Browse..." ), sec );
  SicnuUi::markSecondary( browse );
  SicnuDialogHelp::tip( browse, tr( "Browse and choose the classifier model file" ) );
  pathRow->addWidget( mPathEdit, 1 );
  pathRow->addWidget( browse );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( pathRow );
  layout->addWidget( sec );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "classifier_load" ) );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "OK" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "Cancel" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  auto *helpBtn = buttons->addButton( tr( "Help" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "Opens the help for this dialog." ) );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "classifier_load" ), windowTitle() );
  } );
  layout->addWidget( buttons );

  connect( browse, &QPushButton::clicked,
           this, &RsClassifierLoadDialog::browseForFile );
  connect( buttons, &QDialogButtonBox::accepted,
           this, &RsClassifierLoadDialog::onAccept );
  connect( buttons, &QDialogButtonBox::rejected,
           this, &QDialog::reject );
}

QString RsClassifierLoadDialog::modelPath() const
{
  return mPathEdit ? mPathEdit->text() : QString();
}

void RsClassifierLoadDialog::browseForFile()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Load Classifier Model" ), QString(),
    tr( "OpenCV YAML (*.yml *.yaml *.xml);;All Files (*)" ) );
  if ( !path.isEmpty() && mPathEdit )
    mPathEdit->setText( path );
}

void RsClassifierLoadDialog::onAccept()
{
  mKind = mRbSvm && mRbSvm->isChecked()
            ? BackendKind::SvmRbf
            : BackendKind::NormalBayes;
  const QString p = modelPath();
  if ( p.isEmpty() || !QFileInfo::exists( p ) )
    return;
  accept();
}
