// src/app/dialogs/spatial_filter_dialog.cpp
#include "spatial_filter_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QComboBox>

SpatialFilterDialog::SpatialFilterDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setupUi();
}

void SpatialFilterDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

  setupHelpBanner( mainLayout );

  QFrame *sec = SicnuUi::makeSection(
    this, tr( "滤波参数" ),
    tr( "选择滤波器与卷积核大小。" ) );
  auto *form = new QFormLayout();
  form->setContentsMargins( 0, 0, 0, 0 );
  form->setHorizontalSpacing( 12 );

  m_filterTypeCombo = new QComboBox( sec );
  m_filterTypeCombo->addItem( tr( "均值 Mean" ), QStringLiteral( "opencv:mean_blur" ) );
  m_filterTypeCombo->addItem( tr( "高斯 Gaussian" ), QStringLiteral( "opencv:gaussian_blur" ) );
  m_filterTypeCombo->addItem( tr( "中值 Median" ), QStringLiteral( "opencv:median_blur" ) );
  m_filterTypeCombo->addItem( tr( "Sobel 边缘" ), QStringLiteral( "opencv:sobel" ) );
  m_filterTypeCombo->addItem( tr( "Laplacian 边缘" ), QStringLiteral( "opencv:laplacian" ) );
  SicnuDialogHelp::tip( m_filterTypeCombo, tr(
    "• 均值/高斯/中值：平滑\n"
    "• Sobel/Laplacian：边缘增强\n"
    "中值滤波对椒盐噪声更稳。" ) );
  form->addRow( tr( "滤波器" ), m_filterTypeCombo );

  m_kernelSizeCombo = new QComboBox( sec );
  m_kernelSizeCombo->addItem( tr( "3×3" ), 3 );
  m_kernelSizeCombo->addItem( tr( "5×5" ), 5 );
  SicnuDialogHelp::tip( m_kernelSizeCombo, tr( "卷积核大小。越大平滑/边缘响应范围越大。" ) );
  form->addRow( tr( "核大小" ), m_kernelSizeCombo );

  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( form );
  mainLayout->addWidget( sec );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );
}

void SpatialFilterDialog::onRun()
{
    const QString operatorId = m_filterTypeCombo->currentData().toString();
    const int kernelSize = m_kernelSizeCombo->currentData().toInt();

    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["kernelSize"] = kernelSize;
    if (operatorId == QLatin1String("opencv:gaussian_blur")) {
        params["sigma"] = 1.0;
    }
    if (operatorId == QLatin1String("opencv:sobel")) {
        params["dx"] = 1;
        params["dy"] = 1;
    }
    runOperatorTask(operatorId, params);
}
