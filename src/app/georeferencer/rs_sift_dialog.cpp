#include "rs_sift_dialog.h"
#include "dialogs/dialog_help_catalog.h"
#include "dialogs/dialog_utils.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

RsSiftDialog::RsSiftDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "SIFT 自动匹配参数" ) );
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

  SicnuDialogHelp::tip( mContrast, tr( "特征对比度阈值，越大点越少越稳。" ) );
  SicnuDialogHelp::tip( mMaxMatches, tr( "最大匹配对数。" ) );
  SicnuDialogHelp::tip( mMinInlier, tr( "RANSAC 内点比例下限。" ) );
  SicnuDialogHelp::tip( mRansacThresh, tr( "RANSAC 像素容差。" ) );
  SicnuDialogHelp::tip( mMaxImageSide, tr( "匹配前最大边长（加速）。" ) );

  auto *root = SicnuUi::makeDialogRootLayout( this );
  root->addWidget( SicnuUi::makeHintLabel(
    this, SicnuDialogHelp::shortForTool( QStringLiteral( "sift_match" ),
                                         tr( "SIFT 自动匹配" ) ) ) );

  QFrame *sec = SicnuUi::makeSection( this, tr( "匹配参数" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );
  form->setVerticalSpacing( 8 );
  form->addRow( tr( "对比度阈值" ), mContrast );
  form->addRow( tr( "最多匹配数" ), mMaxMatches );
  form->addRow( tr( "最小内点比" ), mMinInlier );
  form->addRow( tr( "RANSAC 容差" ), mRansacThresh );
  form->addRow( tr( "最大边长" ), mMaxImageSide );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  root->addWidget( sec );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "确定" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "取消" ) );
  buttons->button( QDialogButtonBox::Help )->setText( tr( "帮助" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
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
