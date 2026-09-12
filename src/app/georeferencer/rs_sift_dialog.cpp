#include "rs_sift_dialog.h"
#include "dialogs/dialog_help_catalog.h"
#include "dialogs/dialog_utils.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

RsSiftDialog::RsSiftDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "SIFT Auto-Matching Parameters" ) );
  setObjectName( QStringLiteral( "rsSiftDialog" ) );
  SicnuUi::polishDialog( this, 420 );
  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "sift_match" ) );

  const RsSiftMatcher::Params defaults;

  mContrast = new QDoubleSpinBox( this );
  mContrast->setRange( 0.01, 0.10 );
  mContrast->setSingleStep( 0.01 );
  mContrast->setDecimals( 2 );
  mContrast->setValue( defaults.contrastThreshold );

  mMaxMatches = new QSpinBox( this );
  mMaxMatches->setRange( 10, 500 );
  mMaxMatches->setSingleStep( 10 );
  mMaxMatches->setValue( defaults.maxMatches );

  mMinInlier = new QDoubleSpinBox( this );
  mMinInlier->setRange( 0.1, 0.9 );
  mMinInlier->setSingleStep( 0.05 );
  mMinInlier->setDecimals( 2 );
  mMinInlier->setValue( defaults.minInlierRatio );

  mRansacThresh = new QDoubleSpinBox( this );
  mRansacThresh->setRange( 1.0, 10.0 );
  mRansacThresh->setSingleStep( 0.5 );
  mRansacThresh->setDecimals( 1 );
  mRansacThresh->setSuffix( QStringLiteral( " px" ) );
  mRansacThresh->setValue( defaults.ransacThreshold );

  mMaxImageSide = new QSpinBox( this );
  mMaxImageSide->setRange( 512, 4096 );
  mMaxImageSide->setSingleStep( 256 );
  mMaxImageSide->setSuffix( QStringLiteral( " px" ) );
  mMaxImageSide->setValue( defaults.maxImageSide );

  SicnuDialogHelp::tip( mContrast, tr( "Feature contrast threshold; larger values give fewer but steadier points." ) );
  SicnuDialogHelp::tip( mMaxMatches, tr( "Maximum number of matched pairs." ) );
  SicnuDialogHelp::tip( mMinInlier, tr( "Minimum RANSAC inlier ratio." ) );
  SicnuDialogHelp::tip( mRansacThresh, tr( "RANSAC pixel tolerance." ) );
  SicnuDialogHelp::tip( mMaxImageSide, tr( "Maximum edge length before matching (speed-up)." ) );

  auto *root = SicnuUi::makeDialogRootLayout( this );
  root->addWidget( SicnuUi::makeHintLabel(
    this, SicnuDialogHelp::shortForTool( QStringLiteral( "sift_match" ),
                                         tr( "SIFT Auto Matching" ) ) ) );

  QGroupBox *paramGroup = SicnuUi::makeGroup( this, tr( "SIFT Feature Extraction and Matching Parameters" ) );
  auto *form = SicnuUi::makeFormLayout( paramGroup );
  form->addRow( tr( "Contrast Threshold" ), mContrast );
  form->addRow( tr( "Maximum Matches" ), mMaxMatches );
  form->addRow( tr( "Minimum Inlier Ratio" ), mMinInlier );
  form->addRow( tr( "RANSAC Tolerance" ), mRansacThresh );
  form->addRow( tr( "Maximum Edge Length" ), mMaxImageSide );
  root->addWidget( paramGroup );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "OK" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "Cancel" ) );
  buttons->button( QDialogButtonBox::Help )->setText( tr( "Help" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  SicnuUi::markSecondary( buttons->button( QDialogButtonBox::Cancel ) );
  SicnuUi::markSecondary( buttons->button( QDialogButtonBox::Help ) );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  connect( buttons, &QDialogButtonBox::helpRequested, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "sift_match" ), windowTitle() );
  } );
  root->addWidget( buttons );
}

RsSiftMatcher::Params RsSiftDialog::params() const
{
  RsSiftMatcher::Params p;
  p.contrastThreshold = mContrast->value();
  p.maxMatches        = mMaxMatches->value();
  p.minInlierRatio    = mMinInlier->value();
  p.ransacThreshold   = mRansacThresh->value();
  p.maxImageSide      = mMaxImageSide->value();
  return p;
}
