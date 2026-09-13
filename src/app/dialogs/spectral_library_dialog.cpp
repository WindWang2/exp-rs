// src/app/dialogs/spectral_library_dialog.cpp — Spectral Library Matching
#include "spectral_library_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

SpectralLibraryDialog::SpectralLibraryDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "Spectral Library Matching" ) );
  setMinimumWidth( 620 );
  setupUi();
}

void SpectralLibraryDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  // 1. Input & Library Group
  QGroupBox *inputGroup = SicnuUi::makeGroup(
    this, tr( "Spectrum to Match and Library" ),
    tr( "Uses pixel spectra collected by the Spectral Profile panel; they can be matched against library entries or saved back to the library." ) );
  auto *inputLayout = new QVBoxLayout( inputGroup );
  inputLayout->setContentsMargins( 12, 10, 12, 10 );
  inputLayout->setSpacing( 8 );

  auto *form = SicnuUi::makeFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );

  m_spectrumSummary = new QLabel( tr( "(no spectrum collected)" ), inputGroup );
  m_spectrumSummary->setWordWrap( true );
  form->addRow( tr( "Current Profile" ), m_spectrumSummary );

  m_libraryPathEdit = new QLineEdit( inputGroup );
  m_libraryPathEdit->setObjectName( QStringLiteral( "spectralLibPathEdit" ) );
  m_libraryPathEdit->setPlaceholderText( tr( "Select a spectral library JSON file (*.json)..." ) );
  SicnuDialogHelp::tip( m_libraryPathEdit, tr(
    "Spectral library file (SpectralLibrary JSON): named spectra plus optional wavelength rasters."
    "You can also save the current spectrum into the library below.")  );
  connect( m_libraryPathEdit, &QLineEdit::textChanged, this, [this]( const QString &text ) {
    m_libraryLoaded = ( !m_loadedPath.isEmpty() && text.trimmed() == m_loadedPath );
    m_saveButton->setEnabled( !m_values.isEmpty() && m_libraryLoaded );
  } );

  auto *browseButton = new QPushButton( tr( "Browse..." ), inputGroup );
  browseButton->setObjectName( QStringLiteral( "spectralLibBrowseBtn" ) );
  browseButton->setFixedWidth( 76 );
  SicnuUi::markSecondary( browseButton );
  SicnuDialogHelp::tip( browseButton, tr( "Browse and choose the spectral library JSON file" ) );
  connect( browseButton, &QPushButton::clicked, this, &SpectralLibraryDialog::browseLibrary );

  auto *pathRow = new QHBoxLayout();
  pathRow->setContentsMargins( 0, 0, 0, 0 );
  pathRow->setSpacing( 8 );
  pathRow->addWidget( m_libraryPathEdit, 1 );
  pathRow->addWidget( browseButton );
  form->addRow( tr( "Spectral Library" ), pathRow );

  inputLayout->addLayout( form );
  mainLayout->addWidget( inputGroup );

  // 2. Matching & Results Group
  QGroupBox *resultGroup = SicnuUi::makeGroup( this, tr( "Matching and Search Results" ) );
  auto *resultLayout = new QVBoxLayout( resultGroup );
  resultLayout->setContentsMargins( 12, 10, 12, 10 );
  resultLayout->setSpacing( 8 );

  m_matchButton = new QPushButton( tr( "Run Matching" ), resultGroup );
  SicnuUi::markPrimary( m_matchButton );
  m_matchButton->setObjectName( QStringLiteral( "spectralMatchBtn" ) );
  SicnuDialogHelp::tip( m_matchButton, tr( "Run the SAM / SID spectral matching algorithms and rank library entries by similarity" ) );
  connect( m_matchButton, &QPushButton::clicked, this, &SpectralLibraryDialog::runMatch );

  m_saveButton = new QPushButton( tr( "Save Current Spectrum to Library" ), resultGroup );
  SicnuUi::markSecondary( m_saveButton );
  m_saveButton->setObjectName( QStringLiteral( "spectralSaveBtn" ) );
  SicnuDialogHelp::tip( m_saveButton, tr( "Append or save the currently collected pixel spectrum into the loaded spectral library" ) );
  connect( m_saveButton, &QPushButton::clicked, this, &SpectralLibraryDialog::saveCurrentToLibrary );

  auto *actionRow = new QHBoxLayout();
  actionRow->setContentsMargins( 0, 0, 0, 0 );
  actionRow->setSpacing( 8 );
  actionRow->addWidget( m_matchButton );
  actionRow->addWidget( m_saveButton );
  actionRow->addStretch( 1 );
  resultLayout->addLayout( actionRow );

  m_matchTable = new QTableWidget( resultGroup );
  m_matchTable->setObjectName( QStringLiteral( "spectralMatchTable" ) );
  SicnuDialogHelp::tip( m_matchTable, tr( "Match list: shows the SAM angle (smaller is more similar) and the SID divergence" ) );
  m_matchTable->setColumnCount( 5 );
  m_matchTable->setHorizontalHeaderLabels(
    { tr( "Rank" ), tr( "Name" ), tr( "Material / Class" ), tr( "SAM (°)" ), tr( "SID" ) } );
  m_matchTable->horizontalHeader()->setStretchLastSection( true );
  m_matchTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
  m_matchTable->setSelectionBehavior( QAbstractItemView::SelectRows );
  m_matchTable->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::ResizeToContents );
  m_matchTable->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::ResizeToContents );
  m_matchTable->horizontalHeader()->setSectionResizeMode( 2, QHeaderView::Stretch );
  resultLayout->addWidget( m_matchTable, 1 );

  m_statusLabel = SicnuUi::makeHintLabel( resultGroup, tr( "Ready" ) );
  m_statusLabel->setWordWrap( true );
  resultLayout->addWidget( m_statusLabel );

  mainLayout->addWidget( resultGroup, 1 );

  // 3. Bottom Button Bar
  auto *buttonBox = new QDialogButtonBox( this );
  buttonBox->setObjectName( QStringLiteral( "rsDialogButtonBox" ) );

  auto *helpButton = buttonBox->addButton( tr( "Help" ), QDialogButtonBox::HelpRole );
  helpButton->setObjectName( QStringLiteral( "rsDialogHelpButton" ) );
  SicnuUi::markSecondary( helpButton );
  SicnuDialogHelp::tip( helpButton, tr( "Opens help for spectral library matching." ) );
  connect( helpButton, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "spectral_library" ), windowTitle() );
  } );

  auto *closeButton = buttonBox->addButton( tr( "Close" ), QDialogButtonBox::RejectRole );
  closeButton->setObjectName( QStringLiteral( "rsDialogCloseButton" ) );
  SicnuUi::markSecondary( closeButton );
  SicnuDialogHelp::tip( closeButton, tr( "Closes the dialog." ) );
  connect( buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept );

  mainLayout->addWidget( buttonBox );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "spectral_library" ) );
}

void SpectralLibraryDialog::setSpectrum( const QVector<double> &values,
                                         const QVector<double> &wavelengths,
                                         const QVector<QString> &labels )
{
  m_values = values;
  m_wavelengths = wavelengths;
  m_labels = labels;
  updateSpectrumSummary();
}

void SpectralLibraryDialog::updateSpectrumSummary()
{
  if ( m_values.isEmpty() )
  {
    m_spectrumSummary->setText( tr( "(no spectrum collected)" ) );
    m_matchButton->setEnabled( false );
    m_saveButton->setEnabled( false );
    return;
  }
  m_spectrumSummary->setText( tr( "%1 bands%2" )
                                .arg( m_values.size() )
                                .arg( m_labels.isEmpty()
                                        ? QString()
                                        : tr( "（%1）" ).arg( m_labels.join( tr( ", " ) ) ) ) );
  m_matchButton->setEnabled( true );
  m_saveButton->setEnabled( m_libraryLoaded );
}

void SpectralLibraryDialog::browseLibrary()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "Select Spectral Library" ), QString(), tr( "Spectral Library JSON (*.json);;All Files (*)" ) );
  if ( !path.isEmpty() )
    m_libraryPathEdit->setText( path );
}

bool SpectralLibraryDialog::loadAndMatch( const QString &path, QString *errorMessage )
{
  SpectralLibrary::Library library;
  if ( !SpectralLibrary::Library::load( path, &library, errorMessage ) )
    return false;
  m_library = library;
  m_libraryLoaded = true;
  m_loadedPath = path;
  m_libraryPathEdit->setText( path );
  m_libraryPathEdit->setProperty( "loadedPath", path );
  m_saveButton->setEnabled( !m_values.isEmpty() );
  runMatch();
  return true;
}

void SpectralLibraryDialog::runMatch()
{
  if ( m_values.isEmpty() )
  {
    m_statusLabel->setText( tr( "Collect a spectral profile on the map first." ) );
    return;
  }
  const QString path = m_libraryPathEdit->text().trimmed();
  if ( path.isEmpty() )
  {
    m_statusLabel->setText( tr( "Select a spectral library file." ) );
    return;
  }
  if ( !m_libraryLoaded || m_loadedPath != path )
  {
    QString errorMessage;
    SpectralLibrary::Library library;
    if ( !SpectralLibrary::Library::load( path, &library, &errorMessage ) )
    {
      m_statusLabel->setText( tr( "Load failed: %1" ).arg( errorMessage ) );
      return;
    }
    m_library = library;
    m_libraryLoaded = true;
    m_loadedPath = path;
    m_libraryPathEdit->setProperty( "loadedPath", path );
    m_saveButton->setEnabled( !m_values.isEmpty() );
  }

  std::vector<float> spectrum;
  spectrum.reserve( m_values.size() );
  for ( double v : m_values )
    spectrum.push_back( static_cast<float>( v ) );

  std::vector<float> wavelengths;
  if ( m_wavelengths.size() == m_values.size() )
  {
    wavelengths.reserve( m_wavelengths.size() );
    for ( double w : m_wavelengths )
      wavelengths.push_back( static_cast<float>( w ) );
  }

  const auto scores = SpectralLibrary::matchSpectrum( spectrum, wavelengths, m_library );
  m_matchTable->setRowCount( static_cast<int>( scores.size() ) );
  m_tableRowCount = static_cast<int>( scores.size() );
  for ( int row = 0; row < m_tableRowCount; ++row )
  {
    const SpectralLibrary::MatchScore &score = scores[row];
    auto *rankItem = new QTableWidgetItem( QString::number( row + 1 ) );
    auto *nameItem = new QTableWidgetItem( score.name );
    auto *materialItem = new QTableWidgetItem( score.material );
    auto *angleItem = new QTableWidgetItem(
      std::isnan( score.angleDegrees )
        ? tr( "—" )
        : QString::number( score.angleDegrees, 'f', 3 ) );
    auto *sidItem = new QTableWidgetItem(
      std::isnan( score.divergence )
        ? tr( "—" )
        : QString::number( score.divergence, 'f', 4 ) );
    m_matchTable->setItem( row, 0, rankItem );
    m_matchTable->setItem( row, 1, nameItem );
    m_matchTable->setItem( row, 2, materialItem );
    m_matchTable->setItem( row, 3, angleItem );
    m_matchTable->setItem( row, 4, sidItem );
  }

  const int comparable = m_tableRowCount;
  m_statusLabel->setText(
    tr( "Matching finished: %1 comparable entries (ascending SAM). The library holds %2 entries in total."
        "Unmatched entries usually have a different band count and no wavelength raster."
        "Entries with wavelength rasters were resampled automatically before matching." )
      .arg( comparable )
      .arg( m_library.entries.size() ) );
}

void SpectralLibraryDialog::saveCurrentToLibrary()
{
  if ( m_values.isEmpty() )
    return;

  const QString path = m_libraryPathEdit->text().trimmed();
  if ( path.isEmpty() )
  {
    m_statusLabel->setText( tr( "Select a spectral library file." ) );
    return;
  }
  if ( !m_libraryLoaded || m_loadedPath != path )
  {
    QString errorMessage;
    SpectralLibrary::Library library;
    if ( !SpectralLibrary::Library::load( path, &library, &errorMessage ) )
    {
      m_statusLabel->setText( tr( "Load failed: %1" ).arg( errorMessage ) );
      return;
    }
    m_library = library;
    m_libraryLoaded = true;
    m_loadedPath = path;
    m_libraryPathEdit->setProperty( "loadedPath", path );
  }

  SpectralLibrary::Entry entry;
  entry.name = QStringLiteral( "profile_%1" ).arg( m_library.entries.size() + 1 );
  entry.material = tr( "Untitled" );
  entry.source = tr( "Spectral Profile Panel" );
  entry.spectrum.reserve( m_values.size() );
  for ( double v : m_values )
    entry.spectrum.push_back( static_cast<float>( v ) );
  if ( m_wavelengths.size() == m_values.size() )
  {
    entry.wavelengths.reserve( m_wavelengths.size() );
    for ( double w : m_wavelengths )
      entry.wavelengths.push_back( static_cast<float>( w ) );
  }

  m_library.entries.append( entry );
  QString errorMessage;
  if ( !m_library.save( path, &errorMessage ) )
  {
    m_statusLabel->setText( tr( "Save failed: %1" ).arg( errorMessage ) );
    return;
  }
  m_statusLabel->setText( tr( "Saved entry %1 to %2" ).arg( entry.name, path ) );
}

void SpectralLibraryDialog::showEvent( QShowEvent *event )
{
  QDialog::showEvent( event );
  updateSpectrumSummary();
}
