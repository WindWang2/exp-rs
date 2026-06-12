// src/app/dialogs/comparison_dialog.h
#pragma once

#include <QDialog>

class ComparisonWidget;
class QComboBox;
class QPushButton;
class QgsRasterLayer;

/**
 * Dialog for comparing two raster layers side-by-side or with flicker mode.
 */
class ComparisonDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ComparisonDialog(QWidget *parent = nullptr);

    void setLeftLayer(QgsRasterLayer *layer);
    void setRightLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseLeft();
    void onBrowseRight();
    void onLoadLayers();

private:
    void setupUi();
    void loadLayerToWidget(QgsRasterLayer *layer, bool isLeft);

    ComparisonWidget *m_comparisonWidget = nullptr;
    QComboBox *m_leftLayerCombo = nullptr;
    QComboBox *m_rightLayerCombo = nullptr;
    QPushButton *m_loadButton = nullptr;

    QgsRasterLayer *m_leftLayer = nullptr;
    QgsRasterLayer *m_rightLayer = nullptr;
};
