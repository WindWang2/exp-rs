// rs_classifier_setup_bar.cpp — Phase 10A Task 10.8.

#include "rs_classifier_setup_bar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

RsClassifierSetupBar::RsClassifierSetupBar( QWidget *parent )
  : QWidget( parent )
{
  buildLayout();
}

void RsClassifierSetupBar::buildLayout()
{
  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 8, 4, 8, 4 );
  root->setSpacing( 4 );

  auto *row = new QHBoxLayout();
  row->setSpacing( 8 );

  // --- Algorithm buttons ----------------------------------------------------
  auto makeAlgoBtn = [this]( const QString &text, bool enabled ) {
    auto *b = new QToolButton( this );
    b->setText( text );
    b->setCheckable( true );
    b->setEnabled( enabled );
    return b;
  };
  mBtnNormalBayes = makeAlgoBtn( tr( "NormalBayes" ), true );
  mBtnSvm = makeAlgoBtn( tr( "SVM (RBF)" ), true );
  mBtnKMeans = makeAlgoBtn( tr( "K-Means" ), true );
  mBtnRfDisabled = makeAlgoBtn( tr( "Random Forest" ), false );
  mBtnMahaDisabled = makeAlgoBtn( tr( "Mahalanobis" ), false );
  mBtnUnetDisabled = makeAlgoBtn( tr( "UNet" ), false );
  mBtnNormalBayes->setChecked( true );

  // Use an explicit single-toggle group so the three enabled buttons are
  // mutually exclusive. Disabled placeholders are not added to the group.
  const QVector<QToolButton *> algoBtns = { mBtnNormalBayes, mBtnSvm, mBtnKMeans };
  for ( QToolButton *b : algoBtns )
  {
    connect( b, &QToolButton::toggled, this, [this, b, algoBtns]( bool on ) {
      if ( !on )
        return;
      for ( QToolButton *other : algoBtns )
        if ( other != b )
          other->setChecked( false );
      if ( b == mBtnNormalBayes )
        mKind = RsClassifierKind::NormalBayes;
      else if ( b == mBtnSvm )
        mKind = RsClassifierKind::SvmRbf;
      else if ( b == mBtnKMeans )
        mKind = RsClassifierKind::KMeans;
    } );
  }

  row->addWidget( new QLabel( tr( "算法:" ), this ) );
  for ( QToolButton *b : algoBtns )
    row->addWidget( b );
  row->addSpacing( 6 );
  row->addWidget( mBtnRfDisabled );
  row->addWidget( mBtnMahaDisabled );
  row->addWidget( mBtnUnetDisabled );

  // --- Band picker ----------------------------------------------------------
  row->addSpacing( 12 );
  row->addWidget( new QLabel( tr( "波段:" ), this ) );
  mBandsEdit = new QLineEdit( this );
  mBandsEdit->setPlaceholderText( tr( "e.g. 1,2,3" ) );
  mBandsEdit->setMaximumWidth( 120 );
  mBandsEdit->setObjectName( QStringLiteral( "rsClassifierBands" ) );
  row->addWidget( mBandsEdit );

  // --- Train ratio ----------------------------------------------------------
  row->addSpacing( 8 );
  row->addWidget( new QLabel( tr( "训练比例:" ), this ) );
  mTrainRatioSpin = new QDoubleSpinBox( this );
  mTrainRatioSpin->setRange( 0.1, 0.95 );
  mTrainRatioSpin->setSingleStep( 0.05 );
  mTrainRatioSpin->setValue( 0.7 );
  mTrainRatioSpin->setDecimals( 2 );
  mTrainRatioSpin->setObjectName( QStringLiteral( "rsClassifierTrainRatio" ) );
  row->addWidget( mTrainRatioSpin );

  // --- Output path ----------------------------------------------------------
  row->addSpacing( 8 );
  row->addWidget( new QLabel( tr( "输出:" ), this ) );
  mOutputEdit = new QLineEdit( this );
  mOutputEdit->setPlaceholderText( tr( "/path/to/classified.tif (留空则提示)" ) );
  mOutputEdit->setObjectName( QStringLiteral( "rsClassifierOutput" ) );
  row->addWidget( mOutputEdit, /*stretch*/ 1 );

  // --- Action buttons -------------------------------------------------------
  mBtnCv = new QPushButton( tr( "⚖ 交叉验证" ), this );
  mBtnCv->setObjectName( QStringLiteral( "rsClassifierBtnCv" ) );
  mBtnPreview = new QPushButton( tr( "▶ 快速预览" ), this );
  mBtnPreview->setObjectName( QStringLiteral( "rsClassifierBtnPreview" ) );
  mBtnApply = new QPushButton( tr( "▶ 训练并分类" ), this );
  mBtnApply->setObjectName( QStringLiteral( "rsClassifierBtnApply" ) );
  mBtnApply->setDefault( true );

  row->addWidget( mBtnCv );
  row->addWidget( mBtnPreview );
  row->addWidget( mBtnApply );

  connect( mBtnApply, &QPushButton::clicked,
           this, &RsClassifierSetupBar::applyRequested );
  connect( mBtnPreview, &QPushButton::clicked,
           this, &RsClassifierSetupBar::previewRequested );
  connect( mBtnCv, &QPushButton::clicked,
           this, &RsClassifierSetupBar::crossValidateRequested );

  root->addLayout( row );

  // --- NoData / ignore values (edge handling) -----------------------------
  auto *row2 = new QHBoxLayout();
  row2->setSpacing( 8 );
  mUseSrcNodataCheck = new QCheckBox( tr( "使用源 NoData" ), this );
  mUseSrcNodataCheck->setObjectName( QStringLiteral( "rsClassifierUseSrcNodata" ) );
  mUseSrcNodataCheck->setChecked( true );
  mUseSrcNodataCheck->setToolTip( tr(
    "启用后，各输入波段的 GDAL NoData 像元不参与分类，输出为未分类 (0)。"
    "适合影像边缘或无效区。" ) );
  row2->addWidget( mUseSrcNodataCheck );

  row2->addWidget( new QLabel( tr( "忽略值:" ), this ) );
  mIgnoreValuesEdit = new QLineEdit( this );
  mIgnoreValuesEdit->setObjectName( QStringLiteral( "rsClassifierIgnoreValues" ) );
  mIgnoreValuesEdit->setPlaceholderText( tr( "如 0 或 0,-9999（逗号分隔）" ) );
  mIgnoreValuesEdit->setMaximumWidth( 160 );
  mIgnoreValuesEdit->setToolTip( tr(
    "额外忽略的像元值（任意波段等于该值则视为背景/边缘）。"
    "常见：填充 0、背景 -9999。可与源 NoData 同时生效。" ) );
  row2->addWidget( mIgnoreValuesEdit );

  row2->addWidget( new QLabel( tr( "匹配:" ), this ) );
  mIgnoreModeCombo = new QComboBox( this );
  mIgnoreModeCombo->setObjectName( QStringLiteral( "rsClassifierIgnoreMode" ) );
  mIgnoreModeCombo->addItem( tr( "任一波段" ), 0 );
  mIgnoreModeCombo->addItem( tr( "全部波段" ), 1 );
  mIgnoreModeCombo->setToolTip( tr(
    "任一波段：只要有一个波段为 NoData/忽略值 → 整像素忽略（默认，适合边缘）。\n"
    "全部波段：仅当所有波段均为忽略值时才忽略。" ) );
  row2->addWidget( mIgnoreModeCombo );
  row2->addStretch( 1 );
  root->addLayout( row2 );
}

bool RsClassifierSetupBar::useSourceNodata() const
{
  return mUseSrcNodataCheck ? mUseSrcNodataCheck->isChecked() : true;
}

void RsClassifierSetupBar::setUseSourceNodata( bool on )
{
  if ( mUseSrcNodataCheck )
    mUseSrcNodataCheck->setChecked( on );
}

QString RsClassifierSetupBar::ignoreValuesText() const
{
  return mIgnoreValuesEdit ? mIgnoreValuesEdit->text().trimmed() : QString();
}

void RsClassifierSetupBar::setIgnoreValuesText( const QString &text )
{
  if ( mIgnoreValuesEdit )
    mIgnoreValuesEdit->setText( text );
}

int RsClassifierSetupBar::ignoreMatchMode() const
{
  return mIgnoreModeCombo ? mIgnoreModeCombo->currentData().toInt() : 0;
}

void RsClassifierSetupBar::setIgnoreMatchMode( int mode )
{
  if ( !mIgnoreModeCombo )
    return;
  const int idx = mIgnoreModeCombo->findData( mode );
  if ( idx >= 0 )
    mIgnoreModeCombo->setCurrentIndex( idx );
}

QVector<int> RsClassifierSetupBar::selectedBands() const
{
  QVector<int> out;
  const QString text = mBandsEdit ? mBandsEdit->text().trimmed() : QString();
  if ( text.isEmpty() )
    return out;
  const QStringList parts = text.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
  for ( const QString &p : parts )
  {
    bool ok = false;
    const int v = p.trimmed().toInt( &ok );
    if ( ok && v > 0 )
      out.push_back( v );
  }
  return out;
}

double RsClassifierSetupBar::trainRatio() const
{
  return mTrainRatioSpin ? mTrainRatioSpin->value() : 0.7;
}

void RsClassifierSetupBar::setTrainRatio( double ratio )
{
  if ( mTrainRatioSpin )
    mTrainRatioSpin->setValue( ratio );
}

void RsClassifierSetupBar::setCurrentKind( RsClassifierKind kind )
{
  mKind = kind;
  if ( mBtnNormalBayes )
    mBtnNormalBayes->setChecked( kind == RsClassifierKind::NormalBayes );
  if ( mBtnSvm )
    mBtnSvm->setChecked( kind == RsClassifierKind::SvmRbf );
  if ( mBtnKMeans )
    mBtnKMeans->setChecked( kind == RsClassifierKind::KMeans );
}

QString RsClassifierSetupBar::outputPath() const
{
  return mOutputEdit ? mOutputEdit->text().trimmed() : QString();
}

void RsClassifierSetupBar::setSourceBands( int count )
{
  mSourceBands = count;
  if ( mBandsEdit && mBandsEdit->text().trimmed().isEmpty() && count > 0 )
  {
    // Pre-fill with all bands up to a sensible cap (3 for RGB-typical defaults
    // when the source has 3+ bands; otherwise all bands).
    QStringList parts;
    const int n = std::min( count, 3 );
    for ( int i = 1; i <= n; ++i )
      parts << QString::number( i );
    mBandsEdit->setText( parts.join( QLatin1Char( ',' ) ) );
  }
}

void RsClassifierSetupBar::setOutputPath( const QString &path )
{
  if ( mOutputEdit )
    mOutputEdit->setText( path );
}
