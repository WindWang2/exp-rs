// extract_band_dialog.h — Extract single band from multi-band raster
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QgsRasterLayer;
class AsyncGdalRunner;

class ExtractBandDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExtractBandDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);
    QString outputPath() const;

private slots:
    void onBrowseOutput();
    void onRun();
    void onLayerChanged();
    void populateBandCombo();
    void onCompleted(const QString &outputPath);
    void onFailed(const QString &errorMessage);

private:
    void setupUi();

    QComboBox *m_layerCombo = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QLabel *m_infoLabel = nullptr;
    QPushButton *m_runButton = nullptr;
    AsyncGdalRunner *m_runner = nullptr;
};
