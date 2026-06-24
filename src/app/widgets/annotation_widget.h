// annotation_widget.h — Map Annotation Widget
#pragma once

#include <QWidget>
#include <QVector>
#include <QColor>

class QgsMapCanvas;
class QPushButton;
class QComboBox;
class QColorDialog;

/**
 * Widget for adding annotations (text, arrows, shapes) to the map canvas.
 * Provides tools for marking and labeling features on images.
 */
class AnnotationWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AnnotationWidget(QgsMapCanvas *canvas, QWidget *parent = nullptr);

    enum AnnotationType {
        Text,
        Arrow,
        Rectangle,
        Circle,
        Freehand
    };

    /** Clear all annotations. */
    void clearAnnotations();

    /** Export annotations as image overlay. */
    void exportAnnotations();

private slots:
    void onAddText();
    void onAddArrow();
    void onAddRectangle();
    void onAddCircle();
    void onAddFreehand();
    void onClear();
    void onColorChanged();

private:
    void setupUi();

    QgsMapCanvas *m_canvas = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QPushButton *m_colorBtn = nullptr;
    QColor m_currentColor = QColor(255, 0, 0);
    int m_lineWidth = 2;
};
