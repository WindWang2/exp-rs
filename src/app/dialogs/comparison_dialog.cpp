// src/app/dialogs/comparison_dialog.cpp
#include "comparison_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "widgets/comparison_widget.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>

ComparisonDialog::ComparisonDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "图层对比" ) );
  SicnuUi::polishDialog( this, 800 );
  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "comparison" ) );
  resize( 860, 640 );
  setupUi();
}

void ComparisonDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  mainLayout->addWidget( SicnuUi::makeHintLabel(
    this, tr( "选择左右图层后点「加载」，并排目视对比配准、变化或分类结果。" ) ) );

  QFrame *bar = SicnuUi::makeSection( this, tr( "图层" ) );
  auto *layerLayout = new QHBoxLayout();
  layerLayout->setContentsMargins( 0, 0, 0, 0 );
  layerLayout->addWidget( new QLabel( tr( "左侧" ), bar ) );
  m_leftLayerCombo = new QComboBox( bar );
  m_leftLayerCombo->setMinimumWidth( 200 );
  SicnuDialogHelp::tip( m_leftLayerCombo, tr( "左侧对比栅格。" ) );
  layerLayout->addWidget( m_leftLayerCombo, 1 );
  layerLayout->addWidget( new QLabel( tr( "右侧" ), bar ) );
  m_rightLayerCombo = new QComboBox( bar );
  m_rightLayerCombo->setMinimumWidth( 200 );
  SicnuDialogHelp::tip( m_rightLayerCombo, tr( "右侧对比栅格。" ) );
  layerLayout->addWidget( m_rightLayerCombo, 1 );
  m_loadButton = new QPushButton( tr( "加载" ), bar );
  SicnuUi::markPrimary( m_loadButton );
  SicnuDialogHelp::tip( m_loadButton, tr( "加载到对比视图。" ) );
  connect( m_loadButton, &QPushButton::clicked, this, &ComparisonDialog::onLoadLayers );
  layerLayout->addWidget( m_loadButton );
  qobject_cast<QVBoxLayout *>( bar->layout() )->addLayout( layerLayout );
  mainLayout->addWidget( bar );

  m_comparisonWidget = new ComparisonWidget( this );
  mainLayout->addWidget( m_comparisonWidget, 1 );

  const auto layers = QgsProject::instance()->mapLayers().values();
  for ( QgsMapLayer *layer : layers )
  {
    if ( layer->type() == Qgis::LayerType::Raster )
    {
      m_leftLayerCombo->addItem( layer->name(), layer->id() );
      m_rightLayerCombo->addItem( layer->name(), layer->id() );
    }
  }
  if ( m_rightLayerCombo->count() > 1 )
    m_rightLayerCombo->setCurrentIndex( 1 );

  auto *helpRow = SicnuUi::makeActionRow( this );
  helpRow->addStretch();
  auto *helpBtn = new QPushButton( tr( "帮助" ), this );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "comparison" ), windowTitle() );
  } );
  helpRow->addWidget( helpBtn );
  mainLayout->addLayout( helpRow );
}

void ComparisonDialog::setLeftLayer(QgsRasterLayer *layer)
{
    m_leftLayer = layer;
    if (layer) {
        loadLayerToWidget(layer, true);
    }
}

void ComparisonDialog::setRightLayer(QgsRasterLayer *layer)
{
    m_rightLayer = layer;
    if (layer) {
        loadLayerToWidget(layer, false);
    }
}

void ComparisonDialog::onBrowseLeft()
{
    // Find and set left layer from combo
    QString layerId = m_leftLayerCombo->currentData().toString();
    QgsMapLayer *layer = QgsProject::instance()->mapLayer(layerId);
    if (layer && layer->type() == Qgis::LayerType::Raster) {
        setLeftLayer(qobject_cast<QgsRasterLayer*>(layer));
    }
}

void ComparisonDialog::onBrowseRight()
{
    // Find and set right layer from combo
    QString layerId = m_rightLayerCombo->currentData().toString();
    QgsMapLayer *layer = QgsProject::instance()->mapLayer(layerId);
    if (layer && layer->type() == Qgis::LayerType::Raster) {
        setRightLayer(qobject_cast<QgsRasterLayer*>(layer));
    }
}

void ComparisonDialog::onLoadLayers()
{
    // Load both layers
    onBrowseLeft();
    onBrowseRight();

    if (!m_leftLayer || !m_rightLayer) {
        QMessageBox::warning(this, tr("Compare Layers"),
                             tr("Please select two raster layers."));
        return;
    }
}

void ComparisonDialog::loadLayerToWidget(QgsRasterLayer *layer, bool isLeft)
{
    if (!layer || !layer->isValid()) return;

    // Render the layer to a QPixmap
    // Use a simple approach: render the layer's preview
    QSize size(400, 400);
    QPixmap pixmap(size);
    pixmap.fill(Qt::darkGray);

    // For now, create a simple colored pixmap based on layer properties
    // In a real implementation, this would render the actual raster data
    QPainter painter(&pixmap);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Arial", 12));
    painter.drawText(pixmap.rect(), Qt::AlignCenter,
                     tr("%1\n%2 bands\n%3 x %4 pixels")
                         .arg(layer->name())
                         .arg(layer->bandCount())
                         .arg(layer->width())
                         .arg(layer->height()));
    painter.end();

    if (isLeft) {
        m_comparisonWidget->setLeftImage(pixmap);
    } else {
        m_comparisonWidget->setRightImage(pixmap);
    }
}
