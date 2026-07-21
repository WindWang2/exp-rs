// src/app/dialogs/band_math_dialog.cpp
#include "band_math_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>

void BandMathDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  setupHelpBanner( mainLayout );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "表达式" ),
    tr( "用 b1、b2… 引用波段（从 1 起），支持 + − * / 与括号。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );

  m_expressionEdit = new QLineEdit( sec );
  m_expressionEdit->setPlaceholderText( tr( "例如：(b1 - b2) / (b1 + b2)" ) );
  m_expressionEdit->setMinimumHeight( 32 );
  SicnuDialogHelp::tip( m_expressionEdit, tr(
    "波段运算表达式。波段写作 b1,b2…（从 1 起）。\n"
    "示例：(b1-b2)/(b1+b2)；b1*0.0001\n"
    "支持 + − * / 与括号。" ) );
  form->addRow( tr( "公式" ), m_expressionEdit );
  sec->layout()->addItem( form );
  sec->layout()->addWidget( SicnuUi::makeHintLabel(
    sec, tr( "常用：NDVI ≈ (b_nir − b_red) / (b_nir + b_red)；注意除零。" ) ) );
  mainLayout->addWidget( sec );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );
}

BandMathDialog::BandMathDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 480 );
  setupUi();
}

void BandMathDialog::onRun()
{
  const QString expression = m_expressionEdit->text().trimmed();
  if ( expression.isEmpty() )
  {
    QMessageBox::warning( this, tr( "波段运算" ), tr( "请输入表达式。" ) );
    m_expressionEdit->setFocus();
    return;
  }

  Json::Value params( Json::objectValue );
  params["input"] = m_rasterLayer->source().toStdString();
  params["output"] = outputPath().toStdString();
  params["expression"] = expression.toStdString();

  runOperatorTask( QStringLiteral( "rs:band_math" ), params );
}
