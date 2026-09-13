// src/app/dialogs/mosaic_dialog.cpp
#include "mosaic_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

MosaicDialog::MosaicDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 520 );
  setupUi();
}

void MosaicDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  QGroupBox *inputGroup = setupInputGroup(
    mainLayout, tr( "Input Image List" ) );
  inputGroup->setToolTip(
    tr( "Add at least 2 raster files; a common spatial reference and resolution are recommended. Overlaps are merged with the default strategy." ) );

  auto *groupLayout = new QVBoxLayout( inputGroup );
  groupLayout->setContentsMargins( 10, 8, 10, 8 );
  groupLayout->setSpacing( 8 );

  m_inputList = new QListWidget( inputGroup );
  m_inputList->setObjectName( QStringLiteral( "mosaicInputList" ) );
  m_inputList->setMinimumHeight( 140 );
  m_inputList->setAlternatingRowColors( true );
  SicnuDialogHelp::tip( m_inputList, tr( "List of raster files taking part in the mosaic." ) );
  groupLayout->addWidget( m_inputList );

  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing( 8 );
  auto *addBtn = new QPushButton( tr( "Add Files..." ), inputGroup );
  SicnuUi::markSecondary( addBtn );
  SicnuDialogHelp::tip( addBtn, tr( "Adds one or more raster files to the mosaic input list." ) );
  connect( addBtn, &QPushButton::clicked, this, &MosaicDialog::addInputFile );
  btnRow->addWidget( addBtn );

  auto *removeBtn = new QPushButton( tr( "Remove Selected" ), inputGroup );
  SicnuUi::markSecondary( removeBtn );
  SicnuDialogHelp::tip( removeBtn, tr( "Removes the selected raster files from the mosaic input list." ) );
  connect( removeBtn, &QPushButton::clicked, this, &MosaicDialog::removeInputFile );
  btnRow->addWidget( removeBtn );
  btnRow->addStretch();
  groupLayout->addLayout( btnRow );

  groupLayout->addWidget( SicnuUi::makeHintLabel(
    inputGroup, tr( "Tip: align all images to the same CRS and pixel resolution before mosaicking." ) ) );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );
}

void MosaicDialog::addInputFile()
{
  QStringList paths = QFileDialog::getOpenFileNames(
    this, tr( "Add Input Raster" ), QString(),
    tr( "Rasters (*.tif *.tiff *.img *.asc);;All Files (*)" ) );
  for ( const QString &path : paths )
  {
    if ( !path.isEmpty() )
      m_inputList->addItem( path );
  }
}

void MosaicDialog::removeInputFile()
{
  QList<QListWidgetItem *> selected = m_inputList->selectedItems();
  for ( QListWidgetItem *item : selected )
    delete m_inputList->takeItem( m_inputList->row( item ) );
}

bool MosaicDialog::validateInputs()
{
  if ( m_inputList->count() < 2 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "At least 2 input rasters are required." ) );
    return false;
  }
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Specify the output file path." ) );
    return false;
  }
  return true;
}

void MosaicDialog::onRun()
{
  Json::Value params( Json::objectValue );
  params["inputs"] = Json::Value( Json::arrayValue );
  for ( int i = 0; i < m_inputList->count(); ++i )
    params["inputs"].append( m_inputList->item( i )->text().toStdString() );
  params["output"] = outputPath().toStdString();
  runOperatorTask( QStringLiteral( "rs:mosaic" ), params );
}
