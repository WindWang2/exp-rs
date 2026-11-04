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
#include <QPoint>
#include <QPushButton>
#include <QStringList>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace
{
// swarm-review APP-R-6: parse the comma-separated band editor while reporting
// the tokens that are NOT positive integers, so callers can refuse loudly
// instead of silently dropping them (an all-invalid list then fell back to the
// default bands — the user's selection was replaced without a word).
struct BandTokenParse
{
    QVector<int> bands;
    QStringList invalidTokens;
};

BandTokenParse parseBandTokens( const QString &text )
{
    BandTokenParse out;
    const QStringList parts = text.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
    for ( const QString &p : parts )
    {
        const QString token = p.trimmed();
        bool ok = false;
        const int v = token.toInt( &ok );
        if ( ok && v > 0 )
            out.bands.push_back( v );
        else
            out.invalidTokens << token;
    }
    return out;
}
} // namespace

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
  row->addStretch( 1 );
  mBtnRfDisabled->hide();
  mBtnMahaDisabled->hide();
  mBtnUnetDisabled->hide();
  root->addLayout( row );

  // --- Parameters -----------------------------------------------------------
  auto *parameterRow = new QHBoxLayout();
  parameterRow->setSpacing( 8 );
  parameterRow->addWidget( new QLabel( tr( "Bands:" ), this ) );
  mBandsEdit = new QLineEdit( this );
  mBandsEdit->setPlaceholderText( tr( "e.g. 1,2,3" ) );
  mBandsEdit->setMaximumWidth( 120 );
  mBandsEdit->setObjectName( QStringLiteral( "rsClassifierBands" ) );
  const QString bandsHelpTip = tr(
    "Band numbers taking part in the classification (starting at 1), comma-separated.\n"
    "e.g. 1,2,3 or 2,3,4,5. Left empty, the first few bands are used by default." );
  mBandsEdit->setProperty( "bandsHelpTip", bandsHelpTip );
  SicnuDialogHelp::tip( mBandsEdit, bandsHelpTip );
  parameterRow->addWidget( mBandsEdit );

  // --- Train ratio ----------------------------------------------------------
  parameterRow->addWidget( new QLabel( tr( "Training ratio:" ), this ) );
  mTrainRatioSpin = new QDoubleSpinBox( this );
  mTrainRatioSpin->setRange( 0.1, 0.95 );
  mTrainRatioSpin->setSingleStep( 0.05 );
  mTrainRatioSpin->setValue( 0.7 );
  mTrainRatioSpin->setDecimals( 2 );
  mTrainRatioSpin->setObjectName( QStringLiteral( "rsClassifierTrainRatio" ) );
  SicnuDialogHelp::tip( mTrainRatioSpin, tr(
    "Training share in stratified sampling (0.1–0.95).\n"
    "The remaining samples measure accuracy (confusion matrix). Defaults to 0.7.")  );
  parameterRow->addWidget( mTrainRatioSpin );

  // --- Output path ----------------------------------------------------------
  parameterRow->addWidget( new QLabel( tr( "Output:" ), this ) );
  mOutputEdit = new QLineEdit( this );
  mOutputEdit->setPlaceholderText( tr( "/path/to/classified.tif (prompt if empty)" ) );
  mOutputEdit->setObjectName( QStringLiteral( "rsClassifierOutput" ) );
  mOutputEdit->setMinimumWidth( 180 );
  SicnuDialogHelp::tip( mOutputEdit, tr(
    "Classification result GeoTIFF path. Left empty, a save dialog pops up on run.")  );
  parameterRow->addWidget( mOutputEdit, /*stretch*/ 1 );
  root->addLayout( parameterRow );

  // --- Action buttons -------------------------------------------------------
  auto *actionRow = new QHBoxLayout();
  actionRow->setSpacing( 8 );
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

  actionRow->addWidget( mBtnCv );
  actionRow->addWidget( mBtnPreview );
  actionRow->addStretch( 1 );
  actionRow->addWidget( mBtnApply );
  actionRow->addWidget( helpBtn );
  root->addLayout( actionRow );

  connect( mBtnApply, &QPushButton::clicked, this, [this]() {
    // swarm-review APP-R-6: refuse an unparsable band list instead of silently
    // substituting the default bands.
    if ( !ensureBandsValid() )
      return;
    emit applyRequested();
  } );
  connect( mBtnPreview, &QPushButton::clicked, this, [this]() {
    if ( !ensureBandsValid() )
      return;
    emit previewRequested();
  } );
  connect( mBtnCv, &QPushButton::clicked, this, [this]() {
    if ( !ensureBandsValid() )
      return;
    emit crossValidateRequested();
  } );
  // Clear the invalid marker as soon as the text becomes valid again.
  connect( mBandsEdit, &QLineEdit::textChanged, this, [this]() {
    if ( bandSelectionValid() && !mBandsEdit->styleSheet().isEmpty() )
      setBandsInvalid( false, QString() );
  } );

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
  return parseBandTokens( text ).bands;
}

bool RsClassifierSetupBar::bandSelectionValid() const
{
  const QString text = mBandsEdit ? mBandsEdit->text().trimmed() : QString();
  return parseBandTokens( text ).invalidTokens.isEmpty();
}

bool RsClassifierSetupBar::ensureBandsValid()
{
  if ( !mBandsEdit )
    return true;
  const BandTokenParse parsed = parseBandTokens( mBandsEdit->text().trimmed() );
  if ( parsed.invalidTokens.isEmpty() )
  {
    setBandsInvalid( false, QString() );
    return true;
  }
  const QString message =
      tr( "Invalid band list: %1\nEnter positive band numbers separated by commas (e.g. 1,2,3)." )
          .arg( parsed.invalidTokens.join( QLatin1String( ", " ) ) );
  setBandsInvalid( true, message );
  QToolTip::showText( mBandsEdit->mapToGlobal( QPoint( 0, mBandsEdit->height() ) ),
                      message, mBandsEdit );
  mBandsEdit->setFocus();
  return false;
}

void RsClassifierSetupBar::setBandsInvalid( bool invalid, const QString &error )
{
  if ( !mBandsEdit )
    return;
  if ( invalid )
  {
    mBandsEdit->setStyleSheet( QStringLiteral( "border: 1px solid #B42318;" ) );
    mBandsEdit->setToolTip( error );
  }
  else
  {
    mBandsEdit->setStyleSheet( QString() );
    // Restore the original help text (kept as a property so the tip survives
    // an invalid → valid round trip).
    SicnuDialogHelp::tip( mBandsEdit, mBandsEdit->property( "bandsHelpTip" ).toString() );
  }
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
  // Restored settings can carry any int. An unknown value would leave every
  // algorithm button unchecked and Apply without a backend until deep inside
  // the pipeline; fall back to the default instead.
  switch ( kind )
  {
    case RsClassifierKind::NormalBayes:
    case RsClassifierKind::SvmRbf:
    case RsClassifierKind::KMeans:
      break;
    default:
      kind = RsClassifierKind::NormalBayes;
      break;
  }
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
