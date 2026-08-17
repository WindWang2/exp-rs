// spectral_library_dialog.h — Spectral Library Matching dialog
#pragma once

#include <QDialog>
#include <QVector>

#include "processing/algorithms/spectral_library.h"

class QLineEdit;
class QPushButton;
class QTableWidget;
class QLabel;

/**
 * Spectral Analysis Workbench slice: match the current spectral profile
 * (from the Spectral Profile dock) against a spectral library and rank the
 * results by SAM angle, with SID as a second score. Also saves the current
 * spectrum back into the library (spectrum export).
 *
 * The dialog is a thin presentation adapter over the SpectralLibrary domain
 * and the spectral-angle / SID kernels — runMatch() is public so tests can
 * drive the headless widget state.
 */
class SpectralLibraryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpectralLibraryDialog( QWidget *parent = nullptr );

    /// Set the spectrum to match (per-band values, optional wavelength grid
    /// and labels from the spectral profile dock).
    void setSpectrum( const QVector<double> &values,
                      const QVector<double> &wavelengths = {},
                      const QVector<QString> &labels = {} );

    /// Load the library at @p path (SpectralLibrary JSON) and run the match.
    /// Returns false with @p errorMessage when the file cannot be loaded.
    bool loadAndMatch( const QString &path, QString *errorMessage = nullptr );

    /// Number of rows currently in the results table (0 when not matched).
    int matchRowCount() const { return m_tableRowCount; }

public slots:
    void runMatch();

protected:
    void showEvent( QShowEvent *event ) override;

private slots:
    void browseLibrary();
    void saveCurrentToLibrary();

private:
    void setupUi();
    void updateSpectrumSummary();

    QLabel *m_spectrumSummary = nullptr;
    QLineEdit *m_libraryPathEdit = nullptr;
    QTableWidget *m_matchTable = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_matchButton = nullptr;
    QPushButton *m_saveButton = nullptr;

    QVector<double> m_values;
    QVector<double> m_wavelengths;
    QVector<QString> m_labels;
    SpectralLibrary::Library m_library;
    QString m_loadedPath;
    bool m_libraryLoaded = false;
    int m_tableRowCount = 0;
};
