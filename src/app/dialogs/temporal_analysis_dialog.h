// src/app/dialogs/temporal_analysis_dialog.h
// Temporal Analysis dialog (goal §36/§37) — the UI entry for the temporal
// subsystem, built entirely on the standardized dialog system
// (RasterProcessingDialogBase + SicnuUi helpers + SicnuDialogHelp +
// TaskCenter execution with cancel). Never a custom dialog theme.
#pragma once

#include "raster_processing_dialog_base.h"

#include <QComboBox>

#include <json/json.h>

#include <QStringList>

class QCheckBox;
class QComboBox;
class QTableWidget;
class QStackedWidget;
class QLabel;
class QLineEdit;

/**
 * Multi-date analysis workflow: inspect scenes (acquisition time / platform /
 * grid / QA status), preflight the collection, pick a temporal algorithm
 * (summary / composite / index series / trend / anomaly / point-ROI series),
 * configure parameters and run through the Task Center.
 */
class TemporalAnalysisDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit TemporalAnalysisDialog( QWidget *parent = nullptr );

    /// False for the CSV-producing series extraction (nothing to load as a
    /// raster layer on accept).
    bool wantsRasterLoad() const { return m_algorithmCombo->currentData().toString() !=
                                         QStringLiteral( "rs:temporal_extract_series" ); }

    /// Every raster output the operator actually produced (primary "output"
    /// plus, for grouped composites, each period file), in production order.
    /// Empty until a run completes; the shell loads these on accept (#719).
    QStringList producedOutputs() const { return m_producedOutputs; }

protected:
    QString toolName() const override { return QStringLiteral( "temporal_analysis" ); }
    QString dialogTitle() const override { return tr( "时间序列分析" ); }
    bool validateInputs() override;
    void onRun() override;

private slots:
    void addScenes();
    void removeSelectedScenes();
    void runPreflight();
    void filterChanged( const QString &text );
    void algorithmChanged();

private:
    void setupUi();
    void refreshStatusColumn();
    Json::Value buildScenesJson() const;
    QStringList scenePaths() const;
    void collectProducedOutputs( const Json::Value &result );

    QTableWidget *m_sceneTable = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QComboBox *m_algorithmCombo = nullptr;
    QComboBox *m_bandRoleCombo = nullptr;
    QStackedWidget *m_paramStack = nullptr;
    QLabel *m_preflightLabel = nullptr;
    // per-algorithm parameter widgets
    QComboBox *m_indexCombo = nullptr;
    QComboBox *m_compositeMethodCombo = nullptr;
    QComboBox *m_periodCombo = nullptr;
    QComboBox *m_anomalyMethodCombo = nullptr;
    QCheckBox *m_medianCheck = nullptr;
    QLineEdit *m_pointEdit = nullptr;
    QLineEdit *m_polygonEdit = nullptr;
    QStringList m_producedOutputs;
};
