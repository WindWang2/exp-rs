// rs_classifier_load_dialog.cpp — Phase 10A.1.3.

#include "rs_classifier_load_dialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

RsClassifierLoadDialog::RsClassifierLoadDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "Load classifier model" ) );
  setMinimumWidth( 420 );

  auto *layout = new QVBoxLayout( this );
  layout->addWidget( new QLabel( tr( "Select model algorithm:" ), this ) );

  mRbBayes = new QRadioButton( tr( "NormalBayes (Maximum Likelihood)" ), this );
  mRbSvm = new QRadioButton( tr( "SVM (RBF kernel)" ), this );
  mRbBayes->setChecked( true );
  auto *grp = new QButtonGroup( this );
  grp->addButton( mRbBayes );
  grp->addButton( mRbSvm );
  layout->addWidget( mRbBayes );
  layout->addWidget( mRbSvm );

  auto *pathRow = new QHBoxLayout;
  mPathEdit = new QLineEdit( this );
  mPathEdit->setPlaceholderText( tr( "Path to .yml model file" ) );
  auto *browse = new QPushButton( tr( "Browse…" ), this );
  pathRow->addWidget( mPathEdit, 1 );
  pathRow->addWidget( browse );
  layout->addLayout( pathRow );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
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
    this, tr( "Load classifier model" ), QString(),
    tr( "OpenCV YAML (*.yml *.yaml *.xml);;All files (*)" ) );
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
    return; // keep dialog open
  accept();
}
