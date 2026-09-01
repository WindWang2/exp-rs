// src/app/dialogs/pca_dialog.cpp
#include "pca_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSpinBox>
#include <QMessageBox>

PcaDialog::PcaDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void PcaDialog::setupUi()
{
    auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
    setupHelpBanner( mainLayout );

    QGroupBox *paramGroup = setupParamGroup(
      mainLayout, tr( "PCA 变换参数" ) );
    paramGroup->setToolTip(
      tr( "主成分个数不得超过输入影像的波段总数；前几个主成分通常聚集了绝大部分方差信息。" ) );
    auto *form = SicnuUi::makeFormLayout();
    qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

    m_componentsSpin = new QSpinBox( paramGroup );
    m_componentsSpin->setObjectName( QStringLiteral( "pcaComponentsSpin" ) );
    m_componentsSpin->setRange( 1, 100 );
    m_componentsSpin->setValue( 3 );
    SicnuDialogHelp::tip( m_componentsSpin, tr(
      "输出主成分个数，必须 ≤ 输入波段数。"
      "前几个 PC 通常含大部分方差，用于波段去相关与降维压缩。" ) );
    form->addRow( tr( "主成分个数" ), m_componentsSpin );

    setupOutputRow( mainLayout );
    setupButtonBar( mainLayout );
    mainLayout->addStretch( 1 );
}

void PcaDialog::onRun()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, dialogTitle(), tr("请先选择一个有效的栅格图层。"));
        return;
    }

    const int numComponents = m_componentsSpin->value();
    if (numComponents > m_rasterLayer->bandCount()) {
        QMessageBox::warning(this, dialogTitle(),
                             tr("指定的主成分数 (%1) 超出输入栅格的波段总数 (%2)。")
                                 .arg(numComponents).arg(m_rasterLayer->bandCount()));
        return;
    }

    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["numComponents"] = numComponents;

    runOperatorTask(QStringLiteral("rs:pca"), params);
}
