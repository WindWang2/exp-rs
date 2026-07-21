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

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "输入影像" ),
    tr( "至少 2 个栅格；投影宜一致。重叠区按引擎默认策略合并。" ) );

  m_inputList = new QListWidget( sec );
  m_inputList->setMinimumHeight( 140 );
  SicnuDialogHelp::tip( m_inputList, tr( "参与镶嵌的栅格列表。" ) );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  m_inputList );

  auto *btnRow = new QHBoxLayout();
  auto *addBtn = new QPushButton( tr( "添加…" ), sec );
  SicnuUi::markSecondary( addBtn );
  SicnuDialogHelp::tip( addBtn, tr( "添加一个或多个栅格文件。" ) );
  connect( addBtn, &QPushButton::clicked, this, &MosaicDialog::addInputFile );
  btnRow->addWidget( addBtn );

  auto *removeBtn = new QPushButton( tr( "移除选中" ), sec );
  SicnuUi::markSecondary( removeBtn );
  SicnuDialogHelp::tip( removeBtn, tr( "从列表移除选中文件。" ) );
  connect( removeBtn, &QPushButton::clicked, this, &MosaicDialog::removeInputFile );
  btnRow->addWidget( removeBtn );
  btnRow->addStretch();
  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( btnRow );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  SicnuUi::makeHintLabel(
    sec, tr( "建议：先统一投影与分辨率，再镶嵌。" ) ) );

  mainLayout->addWidget( sec );
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
