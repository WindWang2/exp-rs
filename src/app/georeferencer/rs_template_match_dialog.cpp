#include "rs_template_match_dialog.h"
#include "dialogs/dialog_utils.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

RsTemplateMatchDialog::RsTemplateMatchDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "Template Matching (around the initial coordinates)" ) );
  setObjectName( QStringLiteral( "rsTemplateMatchDialog" ) );
  SicnuUi::polishDialog( this, 420 );

  auto *root = SicnuUi::makeDialogRootLayout( this );

  auto *hint = SicnuUi::makeHintLabel(
    this,
    tr( "Applies when the source image already has approximate geocoordinates: the GeoTransform predicts the search area on the reference image,"
        "Then template correlation matching runs — steadier and more controllable than SIFT." ) );
  hint->setObjectName( QStringLiteral( "rsDialogHelpSummary" ) );
  root->addWidget( hint );

  QGroupBox *paramGroup = SicnuUi::makeGroup( this, tr( "Template Correlation Matching Parameters" ) );
  auto *form = SicnuUi::makeFormLayout( paramGroup );

  m_seedMode = new QComboBox( paramGroup );
  m_seedMode->setObjectName( QStringLiteral( "templateSeedMode" ) );
  m_seedMode->addItem( tr( "Regular grid (search areas predicted from the SRC initial georeference)" ),
                       int( RsTemplateMatcher::SeedMode::Grid ) );
  m_seedMode->addItem( tr( "Existing GCPs as seeds (refinement)" ),
                       int( RsTemplateMatcher::SeedMode::ExistingSeeds ) );
  m_seedMode->setToolTip( tr( "Seed generation mode: a regular grid spread evenly over the scene, or local fine-tuning refinement around existing GCP coordinates" ) );
  form->addRow( tr( "Seed Mode" ), m_seedMode );

  m_templateSize = new QSpinBox( paramGroup );
  m_templateSize->setObjectName( QStringLiteral( "templateSizeSpin" ) );
  m_templateSize->setRange( 17, 257 );
  m_templateSize->setSingleStep( 2 );
  m_templateSize->setValue( 65 );
  m_templateSize->setToolTip( tr( "Template side length cut from the source image (pixels; odd values recommended)" ) );
  form->addRow( tr( "Template Size" ), m_templateSize );

  m_searchRadius = new QSpinBox( paramGroup );
  m_searchRadius->setObjectName( QStringLiteral( "templateSearchRadiusSpin" ) );
  m_searchRadius->setRange( 32, 1024 );
  m_searchRadius->setValue( 96 );
  m_searchRadius->setToolTip(
    tr( "Search half-width around the predicted position on the reference image (pixels)" ) );
  form->addRow( tr( "Search Radius (px)" ), m_searchRadius );

  m_minScore = new QDoubleSpinBox( paramGroup );
  m_minScore->setObjectName( QStringLiteral( "templateMinScoreSpin" ) );
  m_minScore->setRange( 0.30, 0.99 );
  m_minScore->setSingleStep( 0.05 );
  m_minScore->setDecimals( 2 );
  m_minScore->setValue( 0.75 );
  m_minScore->setToolTip( tr( "Minimum normalized cross-correlation coefficient (TM_CCOEFF_NORMED)" ) );
  form->addRow( tr( "Minimum Correlation Score" ), m_minScore );

  m_gridRows = new QSpinBox( paramGroup );
  m_gridRows->setObjectName( QStringLiteral( "templateGridRowsSpin" ) );
  m_gridRows->setRange( 2, 20 );
  m_gridRows->setValue( 5 );
  m_gridRows->setToolTip( tr( "Regular grid rows (number of seed points sampled vertically)" ) );
  form->addRow( tr( "Grid Rows" ), m_gridRows );

  m_gridCols = new QSpinBox( paramGroup );
  m_gridCols->setObjectName( QStringLiteral( "templateGridColsSpin" ) );
  m_gridCols->setRange( 2, 20 );
  m_gridCols->setValue( 5 );
  m_gridCols->setToolTip( tr( "Regular grid columns (number of seed points sampled horizontally)" ) );
  form->addRow( tr( "Grid Columns" ), m_gridCols );

  root->addWidget( paramGroup );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "template_match" ) );

  auto *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "OK" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "Cancel" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  SicnuUi::markSecondary( buttons->button( QDialogButtonBox::Cancel ) );

  auto *helpBtn = buttons->addButton( tr( "Help" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "Opens the help for this dialog." ) );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "template_match" ), windowTitle() );
  } );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  root->addWidget( buttons );

  const auto syncGrid = [this]() {
    const bool grid = m_seedMode->currentData().toInt()
                      == int( RsTemplateMatcher::SeedMode::Grid );
    m_gridRows->setEnabled( grid );
    m_gridCols->setEnabled( grid );
  };
  connect( m_seedMode, QOverload<int>::of( &QComboBox::currentIndexChanged ),
           this, [syncGrid]( int ) { syncGrid(); } );
  syncGrid();
}

RsTemplateMatcher::Params RsTemplateMatchDialog::params() const
{
  RsTemplateMatcher::Params p;
  p.seedMode = static_cast<RsTemplateMatcher::SeedMode>( m_seedMode->currentData().toInt() );
  p.templateSize = m_templateSize->value();
  p.searchRadiusPx = m_searchRadius->value();
  p.minScore = m_minScore->value();
  p.gridRows = m_gridRows->value();
  p.gridCols = m_gridCols->value();
  return p;
}
