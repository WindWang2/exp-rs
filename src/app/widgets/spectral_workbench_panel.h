// spectral_workbench_panel.h — Spectral Workbench 11 endmember/library panel
// (Spectral Intelligence 11.0, work package F).
//
// An independent dock panel (NOT the D18 mission workbench, NOT the
// SpectralProfileWidget) that consumes exp-rs:spectral-table artifacts —
// e.g. rs:endmember_extraction / rs:endmember_analysis outputs — and shows:
//   - the endmember/spectra list (labels) with selection linkage:
//     spectrumSelected() lets a host sync a map selection or profile view;
//   - the pairwise SAM angle matrix of the loaded set
//     (EndmemberAnalysis::angleMatrix — the 11.0 kernel);
//   - the selected spectrum's values and the table's provenance/digest.
#pragma once

#include <QStringList>
#include <QWidget>

#include <vector>

class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;

class SpectralWorkbenchPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SpectralWorkbenchPanel( QWidget *parent = nullptr );

    /// Load an exp-rs:spectral-table artifact. Fails closed (false + error)
    /// on missing/invalid files — a broken artifact never renders as data.
    bool setTablePath( const QString &path, QString *errorMessage = nullptr );

    /// Path currently loaded (empty when none).
    QString tablePath() const { return m_tablePath; }

    /// Row count of the loaded table (0 when none).
    int spectrumCount() const { return m_count; }

    /// Select a row programmatically (clamped); emits spectrumSelected.
    void selectSpectrum( int index );

signals:
    /// Selection linkage seam for the host application (map highlight,
    /// profile view sync). Emitted with the row label and row index.
    void spectrumSelected( const QString &label, int index );

private:
    void refreshViews();
    void onRowActivated( int row );

    QString m_tablePath;
    int m_count = 0;
    int m_bandCount = 0;
    std::vector<std::vector<float>> m_spectra;
    QStringList m_labels;

    QLineEdit *m_pathEdit = nullptr;
    QPushButton *m_loadButton = nullptr;
    QListWidget *m_spectrumList = nullptr;
    QPlainTextEdit *m_matrixView = nullptr;
    QPlainTextEdit *m_detailsView = nullptr;
    QLabel *m_statusLabel = nullptr;
};
