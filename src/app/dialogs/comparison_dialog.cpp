// src/app/dialogs/comparison_dialog.cpp
#include "comparison_dialog.h"
#include "dialog_help_catalog.h"
#include "widgets/comparison_widget.h"

#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>

ComparisonDialog::ComparisonDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Compare Layers"));
    
    SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "comparison" ) );
resize(800, 600);
    setupUi();
}

void ComparisonDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Layer selection bar
    auto *layerLayout = new QHBoxLayout();

    layerLayout->addWidget(new QLabel(tr("Left Layer:"), this));
    m_leftLayerCombo = new QComboBox(this);
    m_leftLayerCombo->setMinimumWidth(200);
    SicnuDialogHelp::tip( m_leftLayerCombo, tr( "左侧对比栅格图层。" ) );
    layerLayout->addWidget(m_leftLayerCombo);

    layerLayout->addWidget(new QLabel(tr("Right Layer:"), this));
    m_rightLayerCombo = new QComboBox(this);
    m_rightLayerCombo->setMinimumWidth(200);
    SicnuDialogHelp::tip( m_rightLayerCombo, tr( "右侧对比栅格图层。" ) );
    layerLayout->addWidget(m_rightLayerCombo);

    m_loadButton = new QPushButton(tr("Load"), this);
    SicnuDialogHelp::tip( m_loadButton, tr( "加载两侧图层到对比视图。" ) );
    connect(m_loadButton, &QPushButton::clicked, this, &ComparisonDialog::onLoadLayers);
    layerLayout->addWidget(m_loadButton);

    mainLayout->addLayout(layerLayout);

    // Comparison widget
    m_comparisonWidget = new ComparisonWidget(this);
    mainLayout->addWidget(m_comparisonWidget);

    // Populate layer combos with raster layers from project
    const auto layers = QgsProject::instance()->mapLayers().values();
    for (QgsMapLayer *layer : layers) {
        if (layer->type() == Qgis::LayerType::Raster) {
            QString name = layer->name();
            m_leftLayerCombo->addItem(name, layer->id());
            m_rightLayerCombo->addItem(name, layer->id());
        }
    }

    // Select different layers by default if available
    if (m_rightLayerCombo->count() > 1) {
        m_rightLayerCombo->setCurrentIndex(1);
    }

    auto *helpRow = new QHBoxLayout();
    helpRow->addStretch();
    auto *helpBtn = new QPushButton( tr( "帮助" ), this );
    SicnuDialogHelp::tip( helpBtn, tr( "查看本对话框说明。" ) );
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
