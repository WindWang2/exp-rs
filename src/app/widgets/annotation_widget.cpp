// annotation_widget.cpp — Map Annotation Widget
#include "annotation_widget.h"
#include "core/sicnu_logging.h"

#include <qgsmapcanvas.h>
#include <qgsrubberband.h>
#include <qgsmapmouseevent.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QColorDialog>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>

AnnotationWidget::AnnotationWidget(QgsMapCanvas *canvas, QWidget *parent)
    : QWidget(parent)
    , m_canvas(canvas)
{
    setupUi();
}

void AnnotationWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);

    // Annotation type
    auto *typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel(tr("Type:")));
    m_typeCombo = new QComboBox();
    m_typeCombo->addItem(tr("Text"), Text);
    m_typeCombo->addItem(tr("Arrow"), Arrow);
    m_typeCombo->addItem(tr("Rectangle"), Rectangle);
    m_typeCombo->addItem(tr("Circle"), Circle);
    m_typeCombo->addItem(tr("Freehand"), Freehand);
    typeLayout->addWidget(m_typeCombo);
    layout->addLayout(typeLayout);

    // Color selection
    auto *colorLayout = new QHBoxLayout();
    colorLayout->addWidget(new QLabel(tr("Color:")));
    m_colorBtn = new QPushButton();
    m_colorBtn->setFixedSize(30, 30);
    m_colorBtn->setStyleSheet(QString("background-color: %1").arg(m_currentColor.name()));
    colorLayout->addWidget(m_colorBtn);
    colorLayout->addStretch();
    layout->addLayout(colorLayout);

    // Line width
    auto *widthLayout = new QHBoxLayout();
    widthLayout->addWidget(new QLabel(tr("Width:")));
    auto *widthSpin = new QSpinBox();
    widthSpin->setRange(1, 10);
    widthSpin->setValue(2);
    widthLayout->addWidget(widthSpin);
    widthLayout->addStretch();
    layout->addLayout(widthLayout);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    auto *addTextBtn = new QPushButton(tr("Add Text"));
    auto *clearBtn = new QPushButton(tr("Clear All"));
    btnLayout->addWidget(addTextBtn);
    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    layout->addStretch();

    // Connections
    connect(m_colorBtn, &QPushButton::clicked, this, &AnnotationWidget::onColorChanged);
    connect(addTextBtn, &QPushButton::clicked, this, &AnnotationWidget::onAddText);
    connect(clearBtn, &QPushButton::clicked, this, &AnnotationWidget::onClear);
}

void AnnotationWidget::onAddText()
{
    if (!m_canvas) return;

    // Simple text annotation dialog
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Text Annotation"));
    auto *layout = new QFormLayout(&dlg);

    auto *textEdit = new QLineEdit();
    textEdit->setPlaceholderText(tr("Enter annotation text"));
    layout->addRow(tr("Text:"), textEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted && !textEdit->text().isEmpty()) {
        // Add text annotation to canvas
        // This would require a custom QgsMapCanvasItem for text rendering
        SICNU_LOG_INFO(SicnuLogTags::Widgets,
                       QString("Text annotation added: %1").arg(textEdit->text()));
    }
}

void AnnotationWidget::onAddArrow()
{
    SICNU_LOG_INFO(SicnuLogTags::Widgets, "Arrow annotation tool activated");
}

void AnnotationWidget::onAddRectangle()
{
    SICNU_LOG_INFO(SicnuLogTags::Widgets, "Rectangle annotation tool activated");
}

void AnnotationWidget::onAddCircle()
{
    SICNU_LOG_INFO(SicnuLogTags::Widgets, "Circle annotation tool activated");
}

void AnnotationWidget::onAddFreehand()
{
    SICNU_LOG_INFO(SicnuLogTags::Widgets, "Freehand annotation tool activated");
}

void AnnotationWidget::onClear()
{
    SICNU_LOG_INFO(SicnuLogTags::Widgets, "All annotations cleared");
}

void AnnotationWidget::onColorChanged()
{
    QColor color = QColorDialog::getColor(m_currentColor, this, tr("Select Color"));
    if (color.isValid()) {
        m_currentColor = color;
        m_colorBtn->setStyleSheet(QString("background-color: %1").arg(color.name()));
    }
}

void AnnotationWidget::clearAnnotations()
{
    onClear();
}

void AnnotationWidget::exportAnnotations()
{
    SICNU_LOG_INFO(SicnuLogTags::Widgets, "Export annotations");
}
