// rs_merge_classes_dialog.h — target-class chooser for merging sub-classes.
#pragma once

#include <QColor>
#include <QDialog>
#include <QList>
#include <QMap>

class QLabel;
class QLineEdit;
class QPushButton;

/**
 * \brief Modal dialog that collects a target class (name / color / id) for
 * merging several selected sub-classes into one category.
 *
 * The target id is fixed at the minimum selected source id (keep-lowest
 * convention) and shown read-only; the user edits name and color.
 */
class RsMergeClassesDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit RsMergeClassesDialog( QWidget *parent = nullptr );

    /// Populate the source list and seed default name/color from the lowest id.
    void setSourceClassIds( const QList<int> &ids, const QString &firstName, const QColor &firstColor );

    QString targetName() const;
    QColor targetColor() const;
    int targetClassId() const;

  private slots:
    void pickColor();

  private:
    QList<int> m_sourceIds;
    QColor m_color;

    QLabel *m_sourceLabel = nullptr;
    QLabel *m_targetIdLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPushButton *m_colorBtn = nullptr;

    void refreshColorButton();
};

/**
 * \brief Build the recode map for a merge: every source id maps to \a targetId,
 * and \a targetId maps to itself. Duplicates and targetId are handled.
 * Pure function — unit-testable without a window.
 */
QMap<int, int> buildRecodeMap( const QList<int> &sourceIds, int targetId );
