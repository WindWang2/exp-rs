#include "rs_sift_dialog.h"
#include "dialogs/dialog_help_catalog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

RsSiftDialog::RsSiftDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "SIFT 自动匹配参数" ) );
  setObjectName( QStringLiteral( "rsSiftDialog" ) );
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

  auto *form = new QFormLayout;
  form->addRow( tr( "对比度阈值 (contrastThreshold)" ),     mContrast );
  form->addRow( tr( "最多匹配数 (maxMatches)" ),            mMaxMatches );
  form->addRow( tr( "最小内点比 (minInlierRatio)" ),        mMinInlier );
  form->addRow( tr( "RANSAC 容差 (ransacThreshold)" ),      mRansacThresh );
  form->addRow( tr( "最大边长 (maxImageSide)" ),            mMaxImageSide );
  SicnuDialogHelp::tip( mContrast, tr( "特征点对比度阈值，越大则匹配点越少、越稳定。" ) );
  SicnuDialogHelp::tip( mMaxMatches, tr( "保留的最大匹配对数上限。" ) );
  SicnuDialogHelp::tip( mMinInlier, tr( "RANSAC 后内点比例下限，过低则结果不可靠。" ) );
  SicnuDialogHelp::tip( mRansacThresh, tr( "RANSAC 像素容差，控制几何一致性。" ) );
  SicnuDialogHelp::tip( mMaxImageSide, tr( "匹配前将影像缩放到此最大边长以加速。" ) );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, this );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  connect( buttons, &QDialogButtonBox::helpRequested, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "sift_match" ), windowTitle() );
  } );

  auto *root = new QVBoxLayout( this );
  root->addLayout( form );
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
