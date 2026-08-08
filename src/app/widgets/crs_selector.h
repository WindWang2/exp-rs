// crs_selector.h — shared CRS input widget (C5)
#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QgsCoordinateReferenceSystem;

/**
 * Shared CRS input (line edit + browse via the QGIS projection dialog) used
 * by product-aware dialogs (orthorectification, reprojection, ...) — one
 * canonical CRS picker instead of per-dialog bare line edits (C5).
 */
class CrsSelector : public QWidget
{
    Q_OBJECT

public:
    explicit CrsSelector( QWidget *parent = nullptr );

    /// The inner line edit (kept public so dialogs can name/style it and
    /// tests can find it by object name).
    QLineEdit *lineEdit() const { return m_edit; }

    /// Current CRS string as typed ("EPSG:4326", WKT, ...); trimmed.
    QString crsString() const;

    /// Set the CRS string (e.g. "EPSG:32650").
    void setCrsString( const QString &crs );

    /// Parsed CRS; invalid when empty or unparsable.
    QgsCoordinateReferenceSystem crs() const;

    /// True when crs() is valid.
    bool isValid() const;

signals:
    /// Emitted when the text changes (any edit).
    void crsChanged( const QString &crs );

private:
    void browse();

    QLineEdit *m_edit = nullptr;
    QPushButton *m_browseButton = nullptr;
};
