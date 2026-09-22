#pragma once

#include "experiment_studio/studio_session.h"

#include <qgsdockwidget.h>

#include <QJsonObject>
#include <QPointer>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

namespace sicnu::app::experiment_studio
{
class SensitivityChartWidget;
}

namespace sicnu::app
{

/// Experiment Exploration Studio dock — teaching projection over study /
/// faultlab / debugger. Owns no compute; machine-readable VMs for Agent later.
class ExperimentStudioDock : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit ExperimentStudioDock( QWidget *parent = nullptr );

  public slots:
    void loadDemoThresholdStudy();
    void validateDesigner();
    void applySyntheticRunMatrix();
    void compareSelectedSpatial();
    void refreshSensitivity();
    void runFaultTeachingDemo();
    void loadFirstDivergenceDemo();
    void exportBundle();
    void cancelStudyPlaceholder();

  private:
    void rebuildMatrixTable();
    void rebuildChart();
    QJsonObject makeDemoOperatorSchema() const;
    QJsonObject makeSyntheticStudyReport( int pointCount ) const;

    QTabWidget *m_tabs = nullptr;

    // A Designer
    QLineEdit *m_algorithmEdit = nullptr;
    QComboBox *m_strategyCombo = nullptr;
    QLineEdit *m_paramPathEdit = nullptr;
    QDoubleSpinBox *m_minSpin = nullptr;
    QDoubleSpinBox *m_maxSpin = nullptr;
    QSpinBox *m_stepsSpin = nullptr;
    QSpinBox *m_maxRunsSpin = nullptr;
    QSpinBox *m_replicatesSpin = nullptr;
    QLineEdit *m_metricEdit = nullptr;
    QPlainTextEdit *m_designerLog = nullptr;

    // B Matrix
    QTableWidget *m_matrixTable = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLabel *m_matrixStatus = nullptr;

    // C Spatial
    QPlainTextEdit *m_spatialLog = nullptr;

    // D Sensitivity
    sicnu::app::experiment_studio::SensitivityChartWidget *m_chart = nullptr;
    QPlainTextEdit *m_sensitivityLog = nullptr;

    // E Fault
    QPlainTextEdit *m_faultLog = nullptr;
    QLineEdit *m_faultPrediction = nullptr;

    // F Divergence
    QPlainTextEdit *m_divergenceLog = nullptr;

    // G Export
    QPlainTextEdit *m_exportLog = nullptr;

    sicnu::experiment_studio::StudioSessionState m_session;
    QJsonObject m_lastDesignerVm;
    QJsonObject m_lastStudyReport;
    QJsonObject m_lastFaultVm;
    QJsonObject m_lastDivergenceVm;
    QJsonObject m_lastSpatialVm;
    QJsonObject m_lastMatrixVm;
    QJsonObject m_lastSensitivityVm;
};

} // namespace sicnu::app
