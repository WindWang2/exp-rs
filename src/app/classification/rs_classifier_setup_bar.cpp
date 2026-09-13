// rs_classifier_setup_bar.cpp — Phase 10A Task 10.8.

#include "rs_classifier_setup_bar.h"

#include "dialogs/dialog_help_catalog.h"
#include "dialogs/dialog_utils.h"

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
  setObjectName( QStringLiteral( "rsClassifierSetupBar" ) );
  buildLayout();
}

void RsClassifierSetupBar::buildLayout()
{
  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 10, 8, 10, 8 );
  root->setSpacing( 6 );

  SicnuDialogHelp::tip( this, SicnuDialogHelp::shortForTool(
                          QStringLiteral( "classify_setup" ), tr( "Classifier Setup Bar" ) ) );

  auto *flowHint = SicnuUi::makeHintLabel(
    this, tr( "Workflow: pick an algorithm → set bands / training ratio → collect ROIs → preview or train the classification → accuracy assessment" ) );
  root->addWidget( flowHint );

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
  SicnuDialogHelp::tip( mBtnNormalBayes, tr(
    "Normal Bayes: assumes multivariate normal class spectra; suits well-sampled, separable classes.")  );
  SicnuDialogHelp::tip( mBtnSvm, tr(
    "SVM (RBF): support vector machine with a radial basis kernel; suits medium samples and non-linear boundaries.")  );
  SicnuDialogHelp::tip( mBtnKMeans, tr(
    "K-means: unsupervised clustering; the class count comes from the labeled samples, and labels may need mapping to ROI class ids.")  );
  SicnuDialogHelp::tip( mBtnRfDisabled, tr( "Random forest: planned; not enabled in this build." ) );
  SicnuDialogHelp::tip( mBtnMahaDisabled, tr( "Mahalanobis distance classification: planned." ) );
  SicnuDialogHelp::tip( mBtnUnetDisabled, tr( "UNet deep learning: planned." ) );

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

  row->addWidget( new QLabel( tr( "Algorithm:" ), this ) );
  for ( QToolButton *b : algoBtns )
    row->addWidget( b );
  row->addSpacing( 6 );
  row->addWidget( mBtnRfDisabled );
  row->addWidget( mBtnMahaDisabled );
  row->addWidget( mBtnUnetDisabled );

  // --- Band picker ----------------------------------------------------------
  row->addSpacing( 12 );
  row->addWidget( new QLabel( tr( "Bands:" ), this ) );
  mBandsEdit = new QLineEdit( this );
  mBandsEdit->setPlaceholderText( tr( "e.g. 1,2,3" ) );
  mBandsEdit->setMaximumWidth( 120 );
  mBandsEdit->setObjectName( QStringLiteral( "rsClassifierBands" ) );
  SicnuDialogHelp::tip( mBandsEdit, tr(
    "Band numbers taking part in the classification (starting at 1), comma-separated.\n"
    "e.g. 1,2,3 or 2,3,4,5. Left empty, the first few bands are used by default.")  );
  row->addWidget( mBandsEdit );

  // --- Train ratio ----------------------------------------------------------
  row->addSpacing( 8 );
  row->addWidget( new QLabel( tr( "Training ratio:" ), this ) );
  mTrainRatioSpin = new QDoubleSpinBox( this );
  mTrainRatioSpin->setRange( 0.1, 0.95 );
  mTrainRatioSpin->setSingleStep( 0.05 );
  mTrainRatioSpin->setValue( 0.7 );
  mTrainRatioSpin->setDecimals( 2 );
  mTrainRatioSpin->setObjectName( QStringLiteral( "rsClassifierTrainRatio" ) );
  SicnuDialogHelp::tip( mTrainRatioSpin, tr(
    "Training share in stratified sampling (0.1–0.95).\n"
    "The remaining samples measure accuracy (confusion matrix). Defaults to 0.7.")  );
  row->addWidget( mTrainRatioSpin );

  // --- Output path ----------------------------------------------------------
  row->addSpacing( 8 );
  row->addWidget( new QLabel( tr( "Output:" ), this ) );
  mOutputEdit = new QLineEdit( this );
  mOutputEdit->setPlaceholderText( tr( "/path/to/classified.tif (prompt if empty)" ) );
  mOutputEdit->setObjectName( QStringLiteral( "rsClassifierOutput" ) );
  SicnuDialogHelp::tip( mOutputEdit, tr(
    "Classification result GeoTIFF path. Left empty, a save dialog pops up on run.")  );
  row->addWidget( mOutputEdit, /*stretch*/ 1 );

  // --- Action buttons -------------------------------------------------------
  mBtnCv = new QPushButton( tr( "Cross-Validation" ), this );
  mBtnCv->setObjectName( QStringLiteral( "rsClassifierBtnCv" ) );
  SicnuDialogHelp::tip( mBtnCv, tr(
    "Stratified K-fold cross-validation to estimate model stability (writes no full-scene classification map).")  );
  mBtnPreview = new QPushButton( tr( "Quick Preview" ), this );
  mBtnPreview->setObjectName( QStringLiteral( "rsClassifierBtnPreview" ) );
  SicnuDialogHelp::tip( mBtnPreview, tr(
    "Classifies only the current map viewport and loads it temporarily, for quick parameter trials.")  );
  mBtnApply = new QPushButton( tr( "Train and Classify" ), this );
  mBtnApply->setObjectName( QStringLiteral( "rsClassifierBtnApply" ) );
  SicnuUi::markPrimary( mBtnApply );
  SicnuDialogHelp::tip( mBtnApply, tr(
    "Trains on the ROI samples and classifies the whole scene, writing the output raster; accuracy assessment follows.")  );

  auto *helpBtn = new QPushButton( tr( "Help" ), this );
  helpBtn->setObjectName( QStringLiteral( "rsClassifierHelpBtn" ) );
  SicnuUi::markSecondary( helpBtn );
  SicnuDialogHelp::tip( helpBtn, tr( "Opens the full Classifier Setup Bar explanation." ) );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "classify_setup" ),
                                   tr( "Classification Settings" ) );
  } );

  row->addWidget( mBtnCv );
  row->addWidget( mBtnPreview );
  row->addWidget( mBtnApply );
  row->addWidget( helpBtn );

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
  mUseSrcNodataCheck = new QCheckBox( tr( "Use source NoData" ), this );
  mUseSrcNodataCheck->setObjectName( QStringLiteral( "rsClassifierUseSrcNodata" ) );
  mUseSrcNodataCheck->setChecked( true );
  mUseSrcNodataCheck->setToolTip( tr(
    "When enabled, GDAL NoData pixels of the input bands are excluded from classification and output as unclassified (0)."
    "Suits image edges or invalid areas.")  );
  row2->addWidget( mUseSrcNodataCheck );

  row2->addWidget( new QLabel( tr( "Ignored values:" ), this ) );
  mIgnoreValuesEdit = new QLineEdit( this );
  mIgnoreValuesEdit->setObjectName( QStringLiteral( "rsClassifierIgnoreValues" ) );
  mIgnoreValuesEdit->setPlaceholderText( tr( "e.g. 0 or 0,-9999 (comma-separated)" ) );
  mIgnoreValuesEdit->setMaximumWidth( 160 );
  mIgnoreValuesEdit->setToolTip( tr(
    "Extra ignored pixel values (any band equal to them counts as background / edge)."
    "Common: 0 fill, -9999 background. Can apply together with the source NoData.")  );
  row2->addWidget( mIgnoreValuesEdit );

  row2->addWidget( new QLabel( tr( "Matches:" ), this ) );
  mIgnoreModeCombo = new QComboBox( this );
  mIgnoreModeCombo->setObjectName( QStringLiteral( "rsClassifierIgnoreMode" ) );
  mIgnoreModeCombo->addItem( tr( "Any band" ), 0 );
  mIgnoreModeCombo->addItem( tr( "All Bands" ), 1 );
  mIgnoreModeCombo->setToolTip( tr(
    "Any band: if one band is NoData / ignored, the whole pixel is ignored (default; suits edges).\n"
    "All bands: the pixel is ignored only when every band is an ignored value.")  );
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
