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
    mainLayout, tr( "输入影像列表" ) );
  inputGroup->setToolTip(
    tr( "至少添加 2 个栅格文件；建议统一空间参考与分辨率。重叠区按默认策略拼接合并。" ) );

  auto *groupLayout = new QVBoxLayout( inputGroup );
  groupLayout->setContentsMargins( 10, 8, 10, 8 );
  groupLayout->setSpacing( 8 );

  m_inputList = new QListWidget( inputGroup );
  m_inputList->setObjectName( QStringLiteral( "mosaicInputList" ) );
  m_inputList->setMinimumHeight( 140 );
  m_inputList->setAlternatingRowColors( true );
  SicnuDialogHelp::tip( m_inputList, tr( "参与镶嵌的栅格文件列表。" ) );
  groupLayout->addWidget( m_inputList );

  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing( 8 );
  auto *addBtn = new QPushButton( tr( "添加文件…" ), inputGroup );
  SicnuUi::markSecondary( addBtn );
  SicnuDialogHelp::tip( addBtn, tr( "添加一个或多个栅格文件到待镶嵌列表。" ) );
  connect( addBtn, &QPushButton::clicked, this, &MosaicDialog::addInputFile );
  btnRow->addWidget( addBtn );

  auto *removeBtn = new QPushButton( tr( "移除选中" ), inputGroup );
  SicnuUi::markSecondary( removeBtn );
  SicnuDialogHelp::tip( removeBtn, tr( "从待镶嵌列表中移除选中的栅格文件。" ) );
  connect( removeBtn, &QPushButton::clicked, this, &MosaicDialog::removeInputFile );
  btnRow->addWidget( removeBtn );
  btnRow->addStretch();
  groupLayout->addLayout( btnRow );

  groupLayout->addWidget( SicnuUi::makeHintLabel(
    inputGroup, tr( "提示：建议在镶嵌前统一各影像的坐标参考系 (CRS) 与像元分辨率。" ) ) );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );
}

void MosaicDialog::addInputFile()
{
  QStringList paths = QFileDialog::getOpenFileNames(
    this, tr( "添加输入栅格" ), QString(),
    tr( "栅格 (*.tif *.tiff *.img *.asc);;所有文件 (*)" ) );
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
    QMessageBox::warning( this, dialogTitle(), tr( "至少需要 2 个输入栅格。" ) );
    return false;
  }
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请指定输出文件路径。" ) );
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
